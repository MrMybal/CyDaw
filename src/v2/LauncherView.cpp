#include "RoutingUi.h"
#include "LauncherView.h"
#include "AddTrackMenu.h"
#include "MixerView.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

const app::AudioTrackController::MidiClip* midiClipForScene(const app::AudioTrackController& engine, const std::uint64_t trackId,
                                                           const int scene)
{
    if (const auto* clips = engine.midiClipsView(trackId))
        for (const auto& clip : *clips)
            if (clip.launcherScene == scene)
                return &clip;
    return nullptr;
}

} // namespace

LauncherView::LauncherView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void LauncherView::draw()
{
    drawnFrame_ = ImGui::GetFrameCount();
    dropSlots_.clear();
    const auto& p = theme::palette();
    auto& engine = studio_.engine();

    const auto tracks = studio_.shownTracks();
    // The scenes in use and two more (eight at least): a large project's
    // hundreds scroll.
    const auto scenes = studio_.launcherSceneCount();

    // Toolbar: mode, quantisation, stop all.
    const auto arrangementMode = engine.arrangementPlaybackEnabled();
    if (ui::iconButton(icon::stop, L("Tout arrêter", "Stop all")))
        stopAll();
    ImGui::SameLine();
    ImGui::TextColored(theme::vec(arrangementMode ? p.textFaint : p.play), "%s",
                       arrangementMode ? L("L'arrangement joue — lancer un clip passe en mode lanceur",
                                           "The arrangement plays — launching a clip switches to the launcher")
                                       : L("Mode lanceur", "Launcher mode"));
    if (!arrangementMode) {
        ImGui::SameLine();
        if (ImGui::SmallButton(L("Revenir à l'arrangement", "Back to the arrangement"))) {
            stopAll();
            engine.setArrangementPlaybackEnabled(true);
        }
    }
    ui::toolbarSeparator();
    ImGui::TextDisabled("%s", L("Quantification", "Quantise"));
    ImGui::SameLine();
    const char* quantisations[] = { L("Aucune", "None"), "1/4", "1/2", L("1 temps", "1 beat"), L("1 mesure", "1 bar") };
    auto quantisation = static_cast<int>(engine.launchQuantization());
    ImGui::SetNextItemWidth(dp(96.0f));
    if (ImGui::Combo("##quantisation", &quantisation, quantisations, 5)) {
        engine.setLaunchQuantization(static_cast<app::AudioTrackController::LaunchQuantization>(quantisation));
        studio_.markDirty();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    ui::tooltip(L("Clic sur un nom de piste : la choisir. Clic sur un clip ou une case : la sélectionner (Ctrl, Maj : en "
                  "ajouter). Le triangle d'un clip le lance ; pendant qu'il joue, son carré l'arrête. Double-clic : ouvrir le "
                  "clip, ou en créer un dans une case MIDI vide. Le carré d'une case vide arrête la piste, et une scène lancée "
                  "arrête les pistes dont la case est vide ; Ctrl+E (ou le clic droit) retire ou remet ce stop sur les cases "
                  "vides sélectionnées. La rangée ■ sous les clips arrête chaque piste. La colonne de gauche lance les scènes.",
                  "Click a track's name: pick it. Click a clip or a slot: select it (Ctrl, Shift: add). A clip's triangle "
                  "launches it; while it plays, its square stops it. Double click: open the clip, or make one in an empty MIDI "
                  "slot. An empty slot's square stops the track, and a scene launched stops the tracks whose slot is empty; "
                  "Ctrl+E (or the right click) takes that stop off the selected empty slots, or puts it back. The ■ row under "
                  "the clips stops each track. The column on the left launches the scenes."));
    // Ctrl+E: the selected empty slots' stop taken off, or put back.
    if (state_.focused == Panel::launcher && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E))
        toggleSelectedStops();

    // As in Bitwig: the scenes' column on the left (each row launched from
    // there), the tracks' columns, each with its mixer strip under its clips,
    // and the master's column on the right, with its own slots. The names
    // stay on top and the stops and strips at the bottom; only the scenes
    // between them scroll up and down (their bar beside the master's
    // column), the tracks sideways.
    const auto available = ImGui::GetContentRegionAvail();
    const auto gap = dp(3.0f);
    // As narrow as a strip lets it (Bitwig's columns).
    const auto cellWidth = std::max(dp(86.0f), mixer_strip::naturalWidth());
    const auto headerHeight = dp(26.0f);
    const auto stopHeight = dp(24.0f);
    const auto scrollbar = ImGui::GetStyle().ScrollbarSize;
    const auto barWidth = dp(10.0f);
    const auto masterWidth = cellWidth;
    const auto scenesWidth = dp(92.0f);
    const auto optionsWidth = dp(30.0f);
    const auto slotsTop = headerHeight + gap;
    // What the options' column shows: the clips, and the sections over the
    // strips (each as tall as it needs).
    const auto showClips = state_.launcherShowClips;
    std::vector<TrackRef> buses;
    for (const auto& track : tracks)
        if (studio_.isEffectBus(track))
            buses.push_back(track);
    const auto devicesHeight = state_.launcherShowDevices ? dp(84.0f) : 0.0f;
    const auto sendsHeight = state_.launcherShowSends ? static_cast<float>(std::max<std::size_t>(1, buses.size())) * dp(26.0f) + dp(6.0f) : 0.0f;
    // A hybrid track has two inputs (its notes, its sound): a row more.
    const auto anyHybrid = std::any_of(tracks.begin(), tracks.end(), [this](const TrackRef& track) {
        return studio_.companionOf(track).has_value();
    });
    const auto ioHeight = state_.launcherShowIo ? dp(anyHybrid ? 83.0f : 58.0f) : 0.0f;
    auto sectionsHeight = 0.0f;
    for (const auto height : { devicesHeight, sendsHeight, ioHeight })
        sectionsHeight += height > 0.0f ? height + gap : 0.0f;
    // From the bottom: the sideways bar, the strips, the sections, the stop
    // row; the scenes have what is left (one row at least).
    const auto bottom = std::max(dp(200.0f), available.y - scrollbar - dp(1.0f));
    const auto aboveStrips = slotsTop + dp(24.0f) + gap + stopHeight + gap * 2.0f + sectionsHeight;
    const auto stripHeight = showClips ? std::max(dp(110.0f), std::min(std::clamp(available.y * 0.42f, dp(150.0f), dp(260.0f)),
                                                                       bottom - aboveStrips))
                                       : std::max(dp(110.0f), std::min(dp(300.0f), bottom - slotsTop - sectionsHeight - gap));
    const auto mixerTop = bottom - stripHeight;
    const auto sectionsTop = mixerTop - sectionsHeight;
    const auto stopRowTop = sectionsTop - gap * 2.0f - stopHeight;
    const auto scenesBottom = stopRowTop - gap;
    const auto viewport = showClips ? std::max(dp(24.0f), scenesBottom - slotsTop) : 0.0f;
    // Where each section starts, from the top of a column.
    const auto devicesTop = sectionsTop;
    const auto sendsTop = devicesTop + (devicesHeight > 0.0f ? devicesHeight + gap : 0.0f);
    const auto ioTop = sendsTop + (sendsHeight > 0.0f ? sendsHeight + gap : 0.0f);
    const auto cellHeight = std::clamp(viewport / static_cast<float>(scenes) - gap, dp(22.0f), dp(30.0f));
    const auto scenesHeight = static_cast<float>(scenes) * (cellHeight + gap) - gap;
    const auto maximumScroll = std::max(0.0f, scenesHeight - viewport);
    sceneScroll_ = std::clamp(sceneScroll_, 0.0f, maximumScroll);
    const auto rowStep = cellHeight + gap;
    const auto& io = ImGui::GetIO();
    // The wheel over the scenes (a knob or a fader keeps it): up and down
    // when they do not all fit, else sideways.
    const auto sceneWheel = [&](const float wheel) {
        if (wheel == 0.0f || io.KeyCtrl || ui::wheelTaken() || ImGui::IsAnyItemActive())
            return false;
        if (io.KeyShift || maximumScroll <= 0.0f)
            return false;
        sceneScroll_ = std::clamp(sceneScroll_ - wheel * rowStep, 0.0f, maximumScroll);
        return true;
    };

    // The scenes' column: each row launched from its button, every clip
    // stopped from the stop row's end.
    ImGui::BeginChild("##scenes", ImVec2(scenesWidth, available.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        auto* sceneList = ImGui::GetWindowDrawList();
        const auto top = ImGui::GetCursorScreenPos();
        const ImVec2 max(top.x + scenesWidth, top.y + headerHeight);
        sceneList->AddRectFilled(top, max, p.bg2, dp(3.0f));
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.88f);
        sceneList->AddText(ImVec2(top.x + dp(6.0f), top.y + dp(6.0f)), p.text, L("Scènes", "Scenes"));
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(max.x - dp(22.0f), top.y + dp(3.0f)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
        if (ui::iconButton(icon::stop, L("Tout arrêter", "Stop all"), false, 0, ImVec2(dp(20.0f), dp(20.0f))))
            stopAll();
        ImGui::PopFont();
        const auto scenesTop = top.y + slotsTop;
        ImGui::PushClipRect(ImVec2(top.x, scenesTop), ImVec2(top.x + scenesWidth, scenesTop + viewport), true);
        for (auto scene = 0; scene < (showClips ? scenes : 0); ++scene) {
            const ImVec2 min(top.x, scenesTop + scene * rowStep - sceneScroll_);
            if (min.y + cellHeight < scenesTop || min.y > scenesTop + viewport)
                continue;
            ImGui::PushID(scene + 1'000);
            ImGui::SetCursorScreenPos(min);
            char label[32];
            std::snprintf(label, sizeof(label), "%s  %s %d", icon::play, L("Scène", "Scene"), scene + 1);
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
            if (ImGui::Button(label, ImVec2(scenesWidth, cellHeight)))
                launchScene(scene);
            ImGui::PopStyleVar();
            ui::tooltip(L("Lancer la scène : les clips de cette rangée", "Launch the scene: the clips of this row"));
            ImGui::PopID();
        }
        ImGui::PopClipRect();
        if (ImGui::IsWindowHovered() && io.MousePos.y >= scenesTop && io.MousePos.y < scenesTop + viewport)
            (void) sceneWheel(io.MouseWheel);
        if (showClips) {
            ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + stopRowTop));
            char label[48];
            std::snprintf(label, sizeof(label), "%s  %s", icon::stop, L("Tout", "All"));
            if (ImGui::Button(label, ImVec2(scenesWidth, stopHeight)))
                stopAll();
            ui::tooltip(L("Arrêter tous les clips", "Stop every clip"));
        }
        // The sections' names, beside them.
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        const auto sectionName = [&](const float at, const float height, const char* name) {
            if (height <= 0.0f)
                return;
            sceneList->AddRectFilled(ImVec2(top.x, top.y + at), ImVec2(top.x + scenesWidth, top.y + at + height), p.bg1, dp(3.0f));
            sceneList->AddText(ImVec2(top.x + dp(6.0f), top.y + at + dp(4.0f)), p.textFaint, name);
        };
        sectionName(devicesTop, devicesHeight, L("Périphériques", "Devices"));
        sectionName(sendsTop, sendsHeight, L("Envois", "Sends"));
        sectionName(ioTop, ioHeight, L("Entrées / sorties", "Inputs / outputs"));
        ImGui::PopFont();
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, gap);

    // The tracks, then the buses beside the master (as Bitwig's effect
    // tracks), each column drawn the same way.
    std::vector<TrackRef> plain;
    for (const auto& track : tracks)
        if (!studio_.isEffectBus(track))
            plain.push_back(track);
    const auto busesWidth = buses.empty() ? 0.0f
                                          : std::min(static_cast<float>(buses.size()) * (cellWidth + gap) - gap,
                                                     std::max(cellWidth, available.x * 0.35f));
    const auto gridWidth = std::max(dp(60.0f), available.x - scenesWidth - masterWidth - barWidth - optionsWidth - busesWidth
                                                   - gap * (buses.empty() ? 5.0f : 6.0f));
    const auto drawColumns = [&](const std::vector<TrackRef>& shown, const bool grid, const float width) {
        auto* list = ImGui::GetWindowDrawList();
        const auto origin = ImGui::GetCursorScreenPos();
        const auto viewTop = origin.y + slotsTop;
        const auto viewBottom = viewTop + viewport;
        if (grid) {
            dropMin_ = ImVec2(ImGui::GetWindowPos().x, viewTop);
            dropMax_ = ImVec2(dropMin_.x + width, viewBottom);
        }
        if (grid && shown.empty())
            ui::emptyState(L("Aucune piste", "No shown"), L("Les clips du lanceur appartiennent aux pistes.", "Launcher clips belong to shown."));
        // Each column's group (a group track's own, its tracks'): its colour
        // tints them, a frame holds them together.
        std::vector<std::optional<TrackRef>> columnGroup(shown.size());
        std::vector<ImU32> columnGroupColour(shown.size());
        for (std::size_t column = 0; column < shown.size(); ++column) {
            columnGroup[column] = studio_.isGroupTrack(shown[column]) ? std::optional<TrackRef>(shown[column]) : studio_.groupOf(shown[column]);
            if (columnGroup[column])
                columnGroupColour[column] = theme::fromArgb(studio_.trackColour(*columnGroup[column]));
        }

        for (std::size_t column = 0; column < shown.size(); ++column) {
            const auto& track = shown[column];
            const ImVec2 min(origin.x + column * (cellWidth + gap), origin.y);
            const ImVec2 max(min.x + cellWidth, min.y + headerHeight);
            const auto selected = (state_.selectedTrack && *state_.selectedTrack == track) || state_.isTrackPicked(track);
            ImGui::PushID(static_cast<int>(column));
            // The header selects its track, which the device panel and the graph
            // then show; a double click brings the devices to the front. The stop
            // button over it keeps its own clicks.
            ImGui::SetCursorScreenPos(min);
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##header", ImVec2(cellWidth, headerHeight));
            const auto headerHovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) {
                // Ctrl: one more or one less; Shift: one more (a group takes them).
                if (io.KeyCtrl || io.KeyShift)
                    state_.pickTrack(track, io.KeyCtrl);
                else
                    state_.selectTrack(track);
                state_.focused = Panel::launcher;
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                if (!state_.isTrackPicked(track))
                    state_.selectTrack(track);
                state_.contextRequest = ContextRequest { ContextRequest::Kind::track, track };
            }
            if (headerHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                state_.bringToFront = Panel::devices;
            if (headerHovered)
                ui::tooltip(L("Clic : choisir la piste (Périphériques, Graphe) ; Ctrl, Maj : plusieurs (Ctrl+G les groupe). Double-clic : "
                              "ses périphériques. Clic droit : son menu.",
                              "Click: pick the track (Devices, Graph); Ctrl, Shift: several (Ctrl+G groups them). Double click: its devices. "
                              "Right click: its menu."));
            const auto group = studio_.isGroupTrack(track);
            const auto inGroup = studio_.groupOf(track);
            auto headerFill = selected ? p.bg4 : headerHovered ? p.bg3 : p.bg2;
            if (columnGroup[column])
                headerFill = theme::mix(headerFill, columnGroupColour[column] | 0xff000000u, group ? 0.3f : 0.14f);
            list->AddRectFilled(min, max, headerFill, dp(3.0f));
            // A group's tracks carry its colour over theirs.
            if (inGroup)
                list->AddRectFilled(min, ImVec2(min.x + cellWidth, min.y + dp(3.0f)), theme::fromArgb(studio_.trackColour(*inGroup)), dp(3.0f),
                                    ImDrawFlags_RoundCornersTop);
            const auto bandTop = inGroup ? min.y + dp(3.0f) : min.y;
            list->AddRectFilled(ImVec2(min.x, bandTop), ImVec2(min.x + cellWidth, bandTop + dp(3.0f)), theme::fromArgb(studio_.trackColour(track)),
                                inGroup ? 0.0f : dp(3.0f), inGroup ? ImDrawFlags_None : ImDrawFlags_RoundCornersTop);
            if (selected)
                list->AddRect(min, max, theme::fromArgb(studio_.trackColour(track)), dp(3.0f), dp(1.5f));
            auto nameLeft = min.x + dp(6.0f);
            auto nameRoom = cellWidth - dp(12.0f);
            if (group) {
                // A group: its folder, and its tracks folded or not.
                const auto folded = studio_.isFolded(track);
                ImGui::SetCursorScreenPos(ImVec2(max.x - dp(18.0f), min.y + dp(5.0f)));
                if (ImGui::InvisibleButton("##fold", ImVec2(dp(16.0f), headerHeight - dp(8.0f))))
                    studio_.setFolded(track, !folded);
                const auto foldHovered = ImGui::IsItemHovered();
                ui::tooltip(folded ? L("Montrer les pistes du groupe", "Show the group's tracks")
                                   : L("Replier les pistes du groupe", "Fold the group's tracks"));
                const ImVec2 c(max.x - dp(10.0f), (min.y + max.y) * 0.5f + dp(1.0f));
                const auto r = dp(3.5f);
                const auto ink = foldHovered ? p.accent : p.textDim;
                if (folded)
                    list->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r), ImVec2(c.x + r, c.y), ink);
                else
                    list->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x, c.y + r), ink);
                ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
                list->AddText(ImVec2(nameLeft, min.y + dp(6.0f)), p.text, icon::folder);
                nameLeft += ImGui::CalcTextSize(icon::folder).x + dp(4.0f);
                // How many tracks it holds, before its fold.
                const auto count = std::to_string(studio_.groupMembers(track).size());
                const auto countWidth = ImGui::CalcTextSize(count.c_str()).x;
                const ImVec2 badge(max.x - dp(22.0f) - countWidth - dp(6.0f), min.y + dp(5.0f));
                list->AddRectFilled(badge, ImVec2(badge.x + countWidth + dp(6.0f), badge.y + ImGui::GetFontSize() + dp(2.0f)),
                                    columnGroupColour[column] | 0xff000000u, dp(3.0f));
                list->AddText(ImVec2(badge.x + dp(3.0f), badge.y + dp(1.0f)), p.bg0 | 0xff000000u, count.c_str());
                ImGui::PopFont();
                nameRoom = badge.x - dp(3.0f) - nameLeft;
            }
            ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.88f);
            ui::clippedText(list, ImVec2(nameLeft, min.y + dp(6.0f)), nameRoom, p.text, studio_.trackName(track).toRawUTF8());
            ImGui::PopFont();
            ImGui::PopID();
        }
        // After the last track: a "+" that adds one.
        const auto addWidth = grid ? std::max(headerHeight, dp(34.0f)) : 0.0f;
        if (grid) {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + static_cast<float>(shown.size()) * (cellWidth + gap), origin.y));
            add_track::button(studio_, state_, "launcherAdd", ImVec2(addWidth, headerHeight));
        }

        // The slots, in the scrolled band between the names and the stops. A
        // clip: its triangle launches it (its square, while it plays, stops it),
        // the rest of it selects it. An empty slot: its square (when it has its
        // stop) stops the track, the rest selects it.
        const auto playWidth = dp(20.0f);
        const auto stopSize = dp(11.0f);
        const auto windowLeft = ImGui::GetWindowPos().x;
        const auto windowRight = windowLeft + ImGui::GetWindowSize().x;
        ImGui::PushClipRect(ImVec2(windowLeft, viewTop), ImVec2(windowRight, viewBottom), true);
        for (auto scene = 0; scene < (showClips ? scenes : 0); ++scene) {
            const auto y = viewTop + scene * rowStep - sceneScroll_;
            if (y + cellHeight < viewTop || y > viewBottom)
                continue;
            for (std::size_t column = 0; column < shown.size(); ++column) {
                const auto& track = shown[column];
                const SlotRef slot { track, scene };
                const ImVec2 min(origin.x + column * (cellWidth + gap), y);
                const ImVec2 max(min.x + cellWidth, y + cellHeight);
                juce::String name;
                juce::uint32 colour {};
                auto exists = false;
                auto progress = 0.0f;
                std::uint64_t midiClip {};
                // A group's slot: its tracks' clips there, launched together.
                const auto group = studio_.isGroupTrack(track);
                // A hybrid track's clip: on the track, or on its companion.
                const auto part = group ? std::optional<TrackRef> {} : studio_.slotPart(track, scene);
                if (group) {
                    exists = studio_.groupHasClips(track, scene);
                    colour = studio_.trackColour(track);
                } else if (part && !part->midi) {
                    if (const auto clip = engine.launcherClip(part->audioIndex, scene)) {
                        exists = true;
                        name = clip->name.isNotEmpty() ? clip->name : clip->sourceFile.getFileNameWithoutExtension();
                        colour = clip->colour;
                    }
                } else if (const auto* clip = part ? midiClipForScene(engine, part->midiId, scene) : nullptr) {
                    exists = true;
                    name = clip->name;
                    colour = clip->colour;
                    midiClip = clip->id;
                }
                const auto clipMidi = part ? part->midi : track.midi;
                const auto playing = exists && (group ? studio_.groupPlaysScene(track, scene) : studio_.playingScene(track) == scene);
                const auto pending = exists && (group ? studio_.groupWaitsScene(track, scene) : studio_.pendingScene(track) == scene);
                if (playing && part)
                    progress = part->midi ? engine.midiLauncherClipProgress(part->midiId) : engine.launcherClipProgress(part->audioIndex);
                const auto stops = !exists && studio_.slotStops(track, scene);
                // An armed track's empty slots record (a round button in
                // place of the stop); the one recording into, red.
                const auto armed = !studio_.isBusTrack(track) && studio_.trackMix(track).armed;
                const auto recordingHere = studio_.slotRecording() && studio_.shownTrackOf(studio_.slotRecording()->first) == track
                                           && studio_.slotRecording()->second == scene;
                const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
                const auto inPlay = [&](const ImVec2 point) { return exists && point.x < min.x + playWidth; };
                // An empty slot of a track: « + MIDI » on its left, « + audio »
                // on its right (shown on hover), clips of either kind.
                const auto canAdd = !exists && !group && !recordingHere && !studio_.isBusTrack(track);
                const auto addHeight = std::min(dp(17.0f), cellHeight - dp(4.0f));
                const auto addWidth = dp(25.0f);
                const ImVec2 addMidiMin(min.x + dp(3.0f), centre.y - addHeight * 0.5f);
                const ImVec2 addAudioMin(max.x - dp(3.0f) - addWidth, centre.y - addHeight * 0.5f);
                const auto inBox = [&](const ImVec2 point, const ImVec2 box) {
                    return point.x >= box.x && point.x < box.x + addWidth && point.y >= box.y && point.y < box.y + addHeight;
                };
                const auto inAddMidi = [&](const ImVec2 point) { return canAdd && inBox(point, addMidiMin); };
                const auto inAddAudio = [&](const ImVec2 point) { return canAdd && inBox(point, addAudioMin); };
                const auto inStop = [&](const ImVec2 point) {
                    return stops && std::abs(point.x - centre.x) <= stopSize && std::abs(point.y - centre.y) <= stopSize;
                };

                ImGui::PushID(static_cast<int>(column) * 1'024 + scene);
                ImGui::SetCursorScreenPos(min);
                ImGui::InvisibleButton("##cell", ImVec2(cellWidth, cellHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                // A clip dragged to another slot (Ctrl when let go: copied).
                if (exists && !group && ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload(kLauncherClipPayload, &slot, sizeof(slot));
                    ImGui::TextUnformatted(name.isNotEmpty() ? name.toRawUTF8() : L("Clip", "Clip"));
                    ImGui::TextDisabled("%s", L("Lâchez-le dans une case (Ctrl : une copie)", "Drop it in a slot (Ctrl: a copy)"));
                    ImGui::EndDragDropSource();
                }
                dropSlots_.push_back({slot, min, max});
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload && payload->DataSize > 1)
                        dropFiles(juce::StringArray(juce::String::fromUTF8(static_cast<const char*>(payload->Data))), io.MousePos);
                    if (!group)
                        if (const auto* payload = ImGui::AcceptDragDropPayload(kLauncherClipPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect);
                            payload != nullptr && payload->DataSize == sizeof(SlotRef)) {
                            SlotRef from;
                            std::memcpy(&from, payload->Data, sizeof(from));
                            if (const auto moved = studio_.moveLauncherClip(from.track, from.scene, track, scene, io.KeyCtrl); moved.failed()) {
                                state_.status = moved.getErrorMessage();
                                state_.statusIsError = true;
                                state_.statusTime = ImGui::GetTime();
                            } else {
                                state_.selectedSlots = { slot };
                            }
                        }
                    ImGui::EndDragDropTarget();
                }
                // Where a dragged clip would land: outlined.
                if (const auto* dragged = ImGui::GetDragDropPayload(); dragged != nullptr && dragged->IsDataType(kLauncherClipPayload)
                                                                      && ImGui::IsMouseHoveringRect(min, max) && !group)
                    list->AddRect(ImVec2(min.x - 1.0f, min.y - 1.0f), ImVec2(max.x + 1.0f, max.y + 1.0f), p.accent, dp(3.0f), dp(2.0f));
                const auto hovered = ImGui::IsItemHovered();
                const auto overPlay = hovered && inPlay(io.MousePos);
                const auto overStop = hovered && inStop(io.MousePos);
                const auto overAddMidi = hovered && inAddMidi(io.MousePos);
                const auto overAddAudio = hovered && inAddAudio(io.MousePos);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    state_.selectTrack(track);
                    if (!state_.isSlotSelected(slot))
                        state_.selectedSlots = { slot };
                    state_.contextRequest = ContextRequest { ContextRequest::Kind::launcherSlot, track, scene };
                }
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    state_.focused = Panel::launcher;
                    const auto at = io.MouseClickedPos[0];
                    if (recordingHere && (inPlay(at) || inStop(at) || !exists)) {
                        // Its recording stopped: the clip kept, launched.
                        if (const auto done = studio_.stopSlotRecording(true); done.failed())
                            state_.status = done.getErrorMessage();
                    } else if (inAddMidi(at)) {
                        // A new MIDI clip there, opened (an audio track's
                        // companion instrument holds it).
                        if (const auto made = studio_.newSlotMidiClip(track, scene)) {
                            state_.selectTrack(track);
                            state_.selectedSlots = { slot };
                            state_.editedClip = ClipRef { made->first, made->second };
                            state_.bringToFront = Panel::editor;
                        }
                    } else if (inAddAudio(at)) {
                        // An empty audio clip there, opened (an instrument
                        // track's companion holds it).
                        if (const auto made = studio_.newSlotAudioClip(track, scene)) {
                            state_.selectTrack(track);
                            state_.selectedSlots = { slot };
                            state_.editedClip = ClipRef { *made, 0, scene };
                            state_.bringToFront = Panel::editor;
                        }
                    } else if (inPlay(at)) {
                        // The clip's button: launch it, or stop it while it plays.
                        if (playing || pending)
                            studio_.stopTrackClips(track);
                        else
                            (void) studio_.launchSlot(track, scene);
                    } else if (inStop(at) && armed) {
                        // Armed: its round button records into it (Shift: the
                        // other kind, a hybrid track's).
                        if (const auto started = studio_.recordIntoSlot(track, scene, io.KeyShift ? std::optional<bool>(!track.midi)
                                                                                               : std::nullopt);
                            started.failed())
                            state_.status = started.getErrorMessage();
                    } else if (inStop(at)) {
                        studio_.stopTrackClips(track);
                    } else {
                        // A click selects (Ctrl toggles, Shift adds); it launches nothing.
                        state_.selectTrack(track);
                        if (io.KeyCtrl) {
                            if (state_.isSlotSelected(slot))
                                std::erase(state_.selectedSlots, slot);
                            else
                                state_.selectedSlots.push_back(slot);
                        } else if (io.KeyShift) {
                            if (!state_.isSlotSelected(slot))
                                state_.selectedSlots.push_back(slot);
                        } else {
                            state_.selectedSlots = { slot };
                        }
                        // The inspector shows the clip picked: a MIDI one by its id;
                        // an audio slot's through the slot (no MIDI clip left picked).
                        if (part && part->midi && exists)
                            state_.selectedClips = { ClipRef { *part, midiClip } };
                        else
                            state_.selectedClips.clear();
                    }
                }
                if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !track.midi && !exists && !overStop
                    && studio_.isEffectBus(track)) {
                    // A bus's slot: a clip of automation alone (its lanes move the
                    // bus's volume, pan, effects), four bars long.
                    const auto bar = 60.0 / std::max(1.0, engine.tempo()) * std::max(1.0, static_cast<double>(engine.beatsPerBar()));
                    if (engine.createEmptyLauncherClip(track.audioIndex, scene, ui::Ls("Automation", "Automation"), bar * 4.0,
                                                       studio_.trackColour(track))) {
                        state_.editedClip = ClipRef { track, 0, scene };
                        state_.bringToFront = Panel::editor;
                        studio_.markDirty();
                    }
                } else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && track.midi && !exists && !overStop
                           && !overAddMidi && !overAddAudio) {
                    // A new MIDI clip in this scene.
                    if (const auto made = studio_.newSlotMidiClip(track, scene)) {
                        state_.editedClip = ClipRef { made->first, made->second };
                        state_.bringToFront = Panel::editor;
                    }
                } else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && exists && !overPlay && !group && part) {
                    // The clip in the editor: a MIDI one by its id, an audio one by its slot.
                    state_.editedClip = part->midi ? ClipRef { *part, midiClip } : ClipRef { *part, 0, scene };
                    state_.bringToFront = Panel::editor;
                }
                if (recordingHere && hovered)
                    ui::tooltip(L("Arrêter l'enregistrement : le clip est gardé, en mesures entières, et joué en boucle",
                                  "Stop recording: the clip is kept, in whole bars, and played looping"));
                else if (overPlay)
                    ui::tooltip(playing || pending ? L("Arrêter", "Stop") : L("Lancer", "Launch"));
                else if (overAddMidi)
                    ui::tooltip(track.midi ? L("Nouveau clip MIDI dans cette case", "New MIDI clip in this slot")
                                           : L("Nouveau clip MIDI dans cette case : la piste devient hybride (un instrument joue ses notes, "
                                               "son son passe dans les effets de la piste)",
                                               "New MIDI clip in this slot: the track becomes hybrid (an instrument plays its notes, "
                                               "its sound goes through the track's effects)"));
                else if (overAddAudio)
                    ui::tooltip(track.midi ? L("Nouveau clip audio vide dans cette case. La piste devient hybride : ses clips audio jouent "
                                               "dans ses effets, après l'instrument.",
                                               "New empty audio clip in this slot. The track becomes hybrid: its audio clips play into its "
                                               "effects, after the instrument.")
                                           : L("Nouveau clip audio vide dans cette case", "New empty audio clip in this slot"));
                else if (overStop && armed)
                    ui::tooltip(track.midi ? L("Enregistrer des notes dans cette case (la piste est armée) : à la mesure suivante ; recliquez "
                                               "pour arrêter. Maj+clic : de l'audio (piste hybride). Clic droit : retirer ce bouton.",
                                               "Record notes into this slot (the track is armed): from the next bar; click again to stop. "
                                               "Shift+click: audio (a hybrid track). Right click: take this button off.")
                                           : L("Enregistrer de l'audio dans cette case (la piste est armée) : à la mesure suivante ; recliquez "
                                               "pour arrêter. Maj+clic : des notes (piste hybride). Clic droit : retirer ce bouton.",
                                               "Record audio into this slot (the track is armed): from the next bar; click again to stop. "
                                               "Shift+click: notes (a hybrid track). Right click: take this button off."));
                else if (overStop)
                    ui::tooltip(L("Arrêter la piste. Une scène lancée l'arrête aussi. Clic droit : retirer ce bouton (Ctrl+E).",
                                  "Stop the track. A scene launched stops it too. Right click: take this button off (Ctrl+E)."));
                else if (hovered && !exists && !track.midi && studio_.isBusTrack(track))
                    ui::tooltip(L("Double-clic : un clip d'automation du bus (son volume, son panoramique, ses effets) joué avec cette scène",
                                  "Double click: an automation clip of the bus (its level, pan, effects) played with this scene"));

                const auto selectedSlot = state_.isSlotSelected(slot);
                if (recordingHere) {
                    // Being recorded into: red, its round button lit.
                    const auto pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 6.0f);
                    list->AddRectFilled(min, max, theme::mix(p.record, p.bg0 | 0xff000000u, 0.6f), dp(3.0f));
                    list->AddRect(min, max, p.record, dp(3.0f), 1.5f);
                    list->AddCircleFilled(ImVec2(min.x + playWidth * 0.5f, centre.y), dp(4.5f), theme::alpha(p.record, pulse));
                    ui::clippedText(list, ImVec2(min.x + playWidth + dp(2.0f), min.y + (cellHeight - ImGui::GetFontSize()) * 0.5f),
                                    cellWidth - playWidth - dp(6.0f), p.text, L("Enreg.", "Rec."));
                } else if (exists) {
                    const auto fill = theme::fromArgb(colour != 0 ? colour : (clipMidi ? 0xff4fd3aeu : 0xffff9b2fu));
                    list->AddRectFilled(min, max, theme::mix(fill, p.bg0 | 0xff000000u, hovered ? 0.4f : 0.5f), dp(3.0f));
                    if (progress > 0.0f)
                        list->AddRectFilled(ImVec2(min.x, max.y - dp(3.0f)), ImVec2(min.x + cellWidth * std::clamp(progress, 0.0f, 1.0f), max.y),
                                            p.play);
                    // Its button: a triangle to launch, a square to stop.
                    const ImVec2 marker(min.x + playWidth * 0.5f, centre.y);
                    if (overPlay)
                        list->AddRectFilled(ImVec2(min.x + dp(2.0f), min.y + dp(2.0f)), ImVec2(min.x + playWidth, max.y - dp(2.0f)),
                                            (p.text & 0x00ffffffu) | 0x22000000u, dp(2.0f));
                    if (playing || pending)
                        list->AddRectFilled(ImVec2(marker.x - dp(4.0f), marker.y - dp(4.0f)), ImVec2(marker.x + dp(4.0f), marker.y + dp(4.0f)),
                                            playing ? p.play : p.accent);
                    else
                        list->AddTriangle(ImVec2(marker.x - dp(4.0f), marker.y - dp(5.0f)), ImVec2(marker.x - dp(4.0f), marker.y + dp(5.0f)),
                                          ImVec2(marker.x + dp(5.0f), marker.y), overPlay ? p.play : p.text, 1.4f);
                    ui::clippedText(list, ImVec2(min.x + playWidth + dp(2.0f), min.y + (cellHeight - ImGui::GetFontSize()) * 0.5f),
                                    cellWidth - playWidth - dp(6.0f), p.text, name.toRawUTF8());
                    if (pending)
                        list->AddRect(min, max, p.accent, dp(3.0f), 1.5f);
                } else {
                    const auto emptyFill = hovered ? p.bg3 : p.bg1;
                    list->AddRectFilled(min, max,
                                        columnGroup[column] ? theme::mix(emptyFill, columnGroupColour[column] | 0xff000000u, 0.1f) : emptyFill,
                                        dp(3.0f));
                    // Its stop, when it has it; an armed track's, a round
                    // button to record.
                    if (stops && armed)
                        list->AddCircle(centre, dp(4.5f), overStop ? p.record : theme::alpha(p.record, 0.7f), 0, overStop ? 1.8f : 1.3f);
                    else if (stops)
                        list->AddRect(ImVec2(centre.x - dp(4.0f), centre.y - dp(4.0f)), ImVec2(centre.x + dp(4.0f), centre.y + dp(4.0f)),
                                      overStop ? p.text : p.textFaint, 0.0f, overStop ? 1.6f : 1.0f);
                    // Hovered: « + MIDI » and « + audio ».
                    if (canAdd && hovered) {
                        // A plus, then a note (MIDI) or a waveform (audio).
                        const auto addButton = [&](const ImVec2 at, const bool midiKind, const ImU32 tint, const bool over) {
                            const ImVec2 to(at.x + addWidth, at.y + addHeight);
                            list->AddRectFilled(at, to, over ? theme::mix(p.bg2, tint, 0.35f) : p.bg2, dp(3.0f));
                            list->AddRect(at, to, over ? tint : theme::alpha(tint, 0.6f), dp(3.0f), 1.0f);
                            const auto ink = over ? p.text : tint;
                            const ImVec2 plus(at.x + dp(6.5f), (at.y + to.y) * 0.5f);
                            const auto arm = dp(3.5f);
                            list->AddLine(ImVec2(plus.x - arm, plus.y), ImVec2(plus.x + arm, plus.y), ink, dp(1.6f));
                            list->AddLine(ImVec2(plus.x, plus.y - arm), ImVec2(plus.x, plus.y + arm), ink, dp(1.6f));
                            const ImVec2 mark(at.x + dp(17.5f), (at.y + to.y) * 0.5f);
                            if (midiKind) {
                                ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
                                const auto glyphSize = ImGui::CalcTextSize(icon::note);
                                list->AddText(ImVec2(mark.x - glyphSize.x * 0.5f, mark.y - glyphSize.y * 0.5f), ink, icon::note);
                                ImGui::PopFont();
                            } else {
                                static constexpr float heights[] = { 0.3f, 0.75f, 0.45f, 1.0f, 0.55f, 0.8f, 0.35f };
                                const auto step = dp(1.25f);
                                const auto half = addHeight * 0.32f;
                                for (std::size_t index = 0; index < std::size(heights); ++index) {
                                    const auto x = mark.x + (static_cast<float>(index) - 3.0f) * step;
                                    list->AddLine(ImVec2(x, mark.y - half * heights[index]), ImVec2(x, mark.y + half * heights[index]), ink,
                                                  dp(0.9f));
                                }
                            }
                        };
                        addButton(addMidiMin, true, p.midi, overAddMidi);
                        addButton(addAudioMin, false, p.audio, overAddAudio);
                    }
                }
                if (selectedSlot)
                    list->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), p.text, dp(3.0f), 1.5f);
                ImGui::PopID();
            }
        }
        ImGui::PopClipRect();
        const auto blankX = origin.x + static_cast<float>(shown.size()) * (cellWidth + gap);
        if (grid && blankX < dropMax_.x && ImGui::BeginDragDropTargetCustom(
                ImRect(ImVec2(std::max(blankX, dropMin_.x), viewTop), dropMax_), ImGui::GetID("##newSamplerDrop"))) {
            if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload && payload->DataSize > 1)
                dropFiles(juce::StringArray(juce::String::fromUTF8(static_cast<const char*>(payload->Data))), io.MousePos);
            ImGui::EndDragDropTarget();
        }


        // Under the slots: each track's stop, its clips stopped.
        for (std::size_t column = 0; column < (showClips ? shown.size() : 0); ++column) {
            const auto& track = shown[column];
            const ImVec2 min(origin.x + column * (cellWidth + gap), origin.y + stopRowTop);
            ImGui::PushID(static_cast<int>(column) + 5'000);
            ImGui::SetCursorScreenPos(min);
            const auto active = studio_.playingScene(track) >= 0 || studio_.pendingScene(track) >= 0;
            ImGui::PushStyleColor(ImGuiCol_Button, theme::vec(active ? theme::mix(p.play, p.bg2 | 0xff000000u, 0.65f) : p.bg2));
            char label[32];
            std::snprintf(label, sizeof(label), "%s", icon::stop);
            if (ImGui::Button(label, ImVec2(cellWidth, stopHeight)))
                studio_.stopTrackClips(track);
            ImGui::PopStyleColor();
            ui::tooltip(L("Arrêter les clips de la piste", "Stop the track's clips"));
            ImGui::PopID();
        }

        // The sections the options' column shows, each track's cell over its
        // strip.
        for (std::size_t column = 0; column < shown.size(); ++column) {
            const auto x = origin.x + static_cast<float>(column) * (cellWidth + gap);
            ImGui::PushID(static_cast<int>(column) + 20'000);
            if (devicesHeight > 0.0f)
                devicesCell(shown[column], ImVec2(x, origin.y + devicesTop), ImVec2(cellWidth, devicesHeight));
            if (sendsHeight > 0.0f)
                sendsCell(shown[column], buses, ImVec2(x, origin.y + sendsTop), ImVec2(cellWidth, sendsHeight));
            if (ioHeight > 0.0f)
                ioCell(shown[column], ImVec2(x, origin.y + ioTop), ImVec2(cellWidth, ioHeight));
            ImGui::PopID();
        }

        // Each track's mixer strip, under its clips.
        std::vector<float> lefts;
        for (std::size_t column = 0; column < shown.size(); ++column) {
            lefts.push_back(origin.x + static_cast<float>(column) * (cellWidth + gap));
            ImGui::SetCursorScreenPos(ImVec2(lefts.back(), origin.y + mixerTop));
            mixer_strip::track(studio_, state_, shown[column], static_cast<int>(column), cellWidth, stripHeight);
        }
        // A group and its tracks: one frame of its colour, names to strips.
        mixer_strip::groupFrames(studio_, list, shown, lefts, cellWidth, origin.y, origin.y + mixerTop + stripHeight, gap);
        ImGui::SetCursorScreenPos(origin);
        // As wide as its columns (the tracks' group: and its "+").
        ImGui::Dummy(ImVec2(grid ? static_cast<float>(shown.size()) * (cellWidth + gap) + addWidth + gap
                                 : static_cast<float>(shown.size()) * (cellWidth + gap) - gap,
                            bottom));
        // The wheel: over the scenes, up and down when they do not all fit;
        // elsewhere (and with Shift), sideways. A knob or a fader under the
        // pointer keeps it.
        if (ImGui::IsWindowHovered()) {
            const auto overScenes = io.MousePos.y >= viewTop && io.MousePos.y < viewBottom;
            if (!(overScenes && sceneWheel(io.MouseWheel)) && io.MouseWheel != 0.0f && !io.KeyCtrl && !ui::wheelTaken()
                && !ImGui::IsAnyItemActive())
                ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheel * dp(120.0f));
            if (io.MouseWheelH != 0.0f)
                ImGui::SetScrollX(ImGui::GetScrollX() - io.MouseWheelH * dp(120.0f));
        }
    };
    ImGui::BeginChild("##grid", ImVec2(gridWidth, available.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawColumns(plain, true, gridWidth);
    ImGui::EndChild();
    if (!buses.empty()) {
        ImGui::SameLine(0.0f, gap);
        ImGui::BeginChild("##buses", ImVec2(busesWidth, available.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        drawColumns(buses, false, busesWidth);
        ImGui::EndChild();
    }

    // The scenes' bar: beside them only, between the grid and the master.
    ImGui::SameLine(0.0f, gap);
    {
        const auto top = ImGui::GetCursorScreenPos();
        const ImVec2 barMin(top.x, top.y + slotsTop);
        const ImVec2 barMax(top.x + barWidth, top.y + slotsTop + viewport);
        auto* barList = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(barMin);
        ImGui::InvisibleButton("##sceneScroll", ImVec2(barWidth, std::max(1.0f, viewport)));
        const auto height = barMax.y - barMin.y;
        const auto thumbHeight = std::clamp(viewport / std::max(scenesHeight, viewport) * height, dp(24.0f), height);
        const auto travel = height - thumbHeight;
        auto thumbTop = barMin.y + (maximumScroll > 0.0f ? sceneScroll_ / maximumScroll * travel : 0.0f);
        if (ImGui::IsItemActivated() && maximumScroll > 0.0f && (io.MousePos.y < thumbTop || io.MousePos.y > thumbTop + thumbHeight))
            sceneScroll_ = std::clamp((io.MousePos.y - barMin.y - thumbHeight * 0.5f) / std::max(1.0f, travel) * maximumScroll, 0.0f,
                                      maximumScroll);
        else if (ImGui::IsItemActive() && travel > 0.0f)
            sceneScroll_ = std::clamp(sceneScroll_ + io.MouseDelta.y / travel * maximumScroll, 0.0f, maximumScroll);
        if (ImGui::IsItemHovered())
            (void) sceneWheel(io.MouseWheel);
        thumbTop = barMin.y + (maximumScroll > 0.0f ? sceneScroll_ / maximumScroll * travel : 0.0f);
        const auto active = ImGui::IsItemActive() || ImGui::IsItemHovered();
        barList->AddRectFilled(barMin, barMax, p.bg1, dp(3.0f));
        barList->AddRectFilled(ImVec2(barMin.x, thumbTop), ImVec2(barMax.x, thumbTop + thumbHeight),
                               maximumScroll <= 0.0f ? p.bg3 : active ? p.accentDim : theme::mix(p.bg5, p.textDim, 0.35f), dp(3.0f));
        if (ImGui::IsItemHovered())
            ui::tooltip(maximumScroll > 0.0f ? L("Les scènes : glisser, ou la molette dessus", "The scenes: drag, or the wheel over them")
                                             : L("Toutes les scènes sont visibles", "Every scene is in view"));
        ImGui::SetCursorScreenPos(top);
        ImGui::Dummy(ImVec2(barWidth, available.y));
    }

    // The master's column: stop all, the scenes, its strip.
    ImGui::SameLine(0.0f, gap);
    ImGui::BeginChild("##master", ImVec2(masterWidth, available.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
        auto* masterList = ImGui::GetWindowDrawList();
        const auto top = ImGui::GetCursorScreenPos();
        const ImVec2 max(top.x + masterWidth, top.y + headerHeight);
        // The header picks the master, as a track's picks its track.
        ImGui::SetCursorScreenPos(top);
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##masterHeader", ImVec2(masterWidth, headerHeight)))
            state_.selectTrack(Studio::masterTrack());
        ui::tooltip(L("Clic : le master et ses périphériques (Périphériques, Graphe)",
                      "Click: the master and its devices (Devices, Graph)"));
        const auto masterPicked = state_.selectedTrack && state_.selectedTrack->isMaster();
        masterList->AddRectFilled(top, max, masterPicked ? p.bg4 : p.bg2, dp(3.0f));
        masterList->AddRectFilled(top, ImVec2(max.x, top.y + dp(3.0f)), p.accent, dp(3.0f), ImDrawFlags_RoundCornersTop);
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.88f);
        masterList->AddText(ImVec2(top.x + dp(6.0f), top.y + dp(6.0f)), p.text, "Master");
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(max.x - dp(22.0f), top.y + dp(3.0f)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
        if (ui::iconButton(icon::stop, L("Arrêter le clip du master", "Stop the master's clip"), false, 0, ImVec2(dp(20.0f), dp(20.0f))))
            studio_.stopMasterClip();
        ImGui::PopFont();
        // Its slots, scrolled with the grid's: its automation clips, one a
        // scene (masterSlot draws and plays them).
        const auto masterViewTop = top.y + slotsTop;
        ImGui::PushClipRect(ImVec2(top.x, masterViewTop), ImVec2(top.x + masterWidth, masterViewTop + viewport), true);
        for (auto scene = 0; scene < (showClips ? scenes : 0); ++scene) {
            const ImVec2 min(top.x, masterViewTop + scene * rowStep - sceneScroll_);
            if (min.y + cellHeight < masterViewTop || min.y > masterViewTop + viewport)
                continue;
            ImGui::PushID(scene + 1'000);
            masterSlot(scene, min, ImVec2(min.x + masterWidth, min.y + cellHeight));
            ImGui::PopID();
        }
        ImGui::PopClipRect();
        if (ImGui::IsWindowHovered() && io.MousePos.y >= masterViewTop && io.MousePos.y < masterViewTop + viewport)
            (void) sceneWheel(io.MouseWheel);
        // The stop row's end: the master's clip stopped.
        if (showClips) {
            ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + stopRowTop));
            char label[48];
            std::snprintf(label, sizeof(label), "%s", icon::stop);
            if (ImGui::Button(label, ImVec2(masterWidth, stopHeight)))
                studio_.stopMasterClip();
            ui::tooltip(L("Arrêter le clip du master", "Stop the master's clip"));
        }
        // Its sections: its devices (no sends, its outputs are the card's).
        if (devicesHeight > 0.0f)
            devicesCell(Studio::masterTrack(), ImVec2(top.x, top.y + devicesTop), ImVec2(masterWidth, devicesHeight));
        for (const auto& [at, height] : { std::pair { sendsTop, sendsHeight }, std::pair { ioTop, ioHeight } })
            if (height > 0.0f)
                masterList->AddRectFilled(ImVec2(top.x, top.y + at), ImVec2(top.x + masterWidth, top.y + at + height), p.bg1, dp(3.0f));
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + mixerTop));
        mixer_strip::master(studio_, state_, masterWidth, stripHeight);
    }
    ImGui::EndChild();
    // The options' column: what the launcher shows.
    ImGui::SameLine(0.0f, gap);
    optionsColumn(available.y);
}

bool LauncherView::dropFiles(const juce::StringArray& files, const ImVec2 point)
{
    if (state_.mode != AppMode::studio || ImGui::GetFrameCount() - drawnFrame_ > 1) return false;
    if (point.x < dropMin_.x || point.x >= dropMax_.x || point.y < dropMin_.y || point.y >= dropMax_.y)
        return false;
    std::optional<SlotRef> target;
    for (const auto& cell : dropSlots_)
        if (point.x >= cell.min.x && point.x < cell.max.x && point.y >= cell.min.y && point.y < cell.max.y) {
            target = cell.slot;
            break;
        }
    bool added = false;
    for (const auto& path : files) {
        const juce::File file(path);
        if (!file.existsAsFile() || !file.hasFileExtension("wav;aif;aiff;flac;ogg;mp3;wv"))
            continue;
        if (target && !studio_.isGroupTrack(target->track) && (!target->track.midi || !studio_.isBusTrack(target->track))) {
            // A slot: the file plays there (an instrument track's companion
            // holds it: the track is hybrid).
            if (const auto placed = studio_.placeSlotAudio(target->track, target->scene, file); placed.failed()) {
                state_.status = placed.getErrorMessage();
                state_.statusIsError = true;
                state_.statusTime = ImGui::GetTime();
                continue;
            }
            state_.selectTrack(target->track);
            state_.selectedSlots = {*target};
            ++target->scene;
            added = true;
        } else {
            juce::String error;
            if (const auto track = studio_.newSamplerTrack({file}, error)) {
                state_.selectTrack(*track);
                added = true;
            } else {
                state_.status = error;
                state_.statusIsError = true;
                state_.statusTime = ImGui::GetTime();
            }
        }
    }
    if (added) studio_.markDirty();
    return added;
}

void LauncherView::masterSlot(const int scene, const ImVec2 min, const ImVec2 max)
{
    const auto& p = theme::palette();
    const auto& io = ImGui::GetIO();
    auto* list = ImGui::GetWindowDrawList();
    const auto width = max.x - min.x;
    const auto height = max.y - min.y;
    const auto playWidth = dp(20.0f);
    const auto open = [&] {
        state_.editedMasterScene = scene;
        state_.editedClip.reset();
        state_.bringToFront = Panel::editor;
    };
    const auto* clip = studio_.masterClip(scene);
    const auto playing = clip != nullptr && studio_.playingMasterScene() == scene;
    const auto pending = clip != nullptr && studio_.pendingMasterScene() == scene;
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##masterCell", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const auto hovered = ImGui::IsItemHovered();
    const auto overPlay = hovered && clip != nullptr && io.MousePos.x < min.x + playWidth;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        state_.focused = Panel::launcher;
        if (clip != nullptr && io.MouseClickedPos[0].x < min.x + playWidth) {
            // Its button: launch it, or stop it while it plays.
            if (playing || pending)
                studio_.stopMasterClip();
            else
                (void) studio_.launchMasterClip(scene);
        } else if (clip != nullptr) {
            // A click shows it in the editor (without bringing it forward).
            state_.editedMasterScene = scene;
            state_.editedClip.reset();
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !overPlay) {
        if (clip == nullptr)
            (void) studio_.createMasterClip(scene);
        open();
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##masterSlotMenu");
    if (ImGui::BeginPopup("##masterSlotMenu")) {
        if (clip == nullptr) {
            if (ImGui::MenuItem(L("Créer un clip d'automation", "Create an automation clip"))) {
                (void) studio_.createMasterClip(scene);
                open();
            }
        } else {
            if (ImGui::MenuItem(playing || pending ? L("Arrêter", "Stop") : L("Lancer", "Launch"))) {
                if (playing || pending)
                    studio_.stopMasterClip();
                else
                    (void) studio_.launchMasterClip(scene);
            }
            if (ImGui::MenuItem(L("Éditer", "Edit")))
                open();
            if (ImGui::MenuItem(L("Dupliquer vers la scène suivante", "Duplicate to the next scene"), nullptr, false,
                                studio_.masterClip(scene + 1) == nullptr)) {
                auto copy = *clip;
                copy.name = clip->name + " 2";
                studio_.setMasterClip(scene + 1, std::move(copy));
            }
            ImGui::Separator();
            if (ImGui::MenuItem(L("Supprimer", "Delete"))) {
                if (state_.editedMasterScene && *state_.editedMasterScene == scene)
                    state_.editedMasterScene.reset();
                studio_.removeMasterClip(scene);
            }
        }
        ImGui::EndPopup();
    }
    // What a menu did: the clip as it is now.
    clip = studio_.masterClip(scene);
    if (overPlay)
        ui::tooltip(playing || pending ? L("Arrêter le clip du master", "Stop the master's clip")
                                       : L("Lancer le clip du master", "Launch the master's clip"));
    else if (hovered && clip == nullptr)
        ui::tooltip(L("Double-clic : un clip d'automation du master (volume, ses effets) joué avec cette scène",
                      "Double click: an automation clip of the master (level, its effects) played with this scene"));
    else if (hovered)
        ui::tooltip(L("Clic : l'éditer. Double-clic : l'ouvrir. Clic droit : son menu.",
                      "Click: edit it. Double click: open it. Right click: its menu."));

    const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    if (clip != nullptr) {
        const auto fill = theme::fromArgb(clip->colour != 0 ? clip->colour : 0xffffb84du);
        list->AddRectFilled(min, max, theme::mix(fill, p.bg0 | 0xff000000u, hovered ? 0.4f : 0.5f), dp(3.0f));
        if (playing)
            list->AddRectFilled(ImVec2(min.x, max.y - dp(3.0f)),
                                ImVec2(min.x + width * std::clamp(studio_.masterClipProgress(), 0.0f, 1.0f), max.y), p.play);
        const ImVec2 marker(min.x + playWidth * 0.5f, centre.y);
        if (overPlay)
            list->AddRectFilled(ImVec2(min.x + dp(2.0f), min.y + dp(2.0f)), ImVec2(min.x + playWidth, max.y - dp(2.0f)),
                                (p.text & 0x00ffffffu) | 0x22000000u, dp(2.0f));
        if (playing || pending)
            list->AddRectFilled(ImVec2(marker.x - dp(4.0f), marker.y - dp(4.0f)), ImVec2(marker.x + dp(4.0f), marker.y + dp(4.0f)),
                                playing ? p.play : p.accent);
        else
            list->AddTriangle(ImVec2(marker.x - dp(4.0f), marker.y - dp(5.0f)), ImVec2(marker.x - dp(4.0f), marker.y + dp(5.0f)),
                              ImVec2(marker.x + dp(5.0f), marker.y), overPlay ? p.play : p.text, 1.4f);
        ui::clippedText(list, ImVec2(min.x + playWidth + dp(2.0f), min.y + (height - ImGui::GetFontSize()) * 0.5f),
                        width - playWidth - dp(6.0f), p.text, clip->name.toRawUTF8());
        if (pending)
            list->AddRect(min, max, p.accent, dp(3.0f), 1.5f);
    } else {
        // An empty slot, seen: a double click makes its clip.
        list->AddRectFilled(min, max, hovered ? p.bg3 : p.bg2, dp(3.0f));
        list->AddRect(min, max, theme::alpha(p.accent, hovered ? 0.6f : 0.22f), dp(3.0f));
        ui::centredText(list, min, max, hovered ? p.text : p.textFaint, "+");
    }
    if (clip != nullptr && state_.editedMasterScene && *state_.editedMasterScene == scene)
        list->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), p.text, dp(3.0f), 1.5f);
}

// ---- The sections over the strips -------------------------------------------------------------

void LauncherView::devicesCell(const TrackRef& track, const ImVec2 min, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), p.bg2, dp(3.0f));
    juce::StringArray names;
    if (const auto* slot = studio_.graphFor(track))
        for (const auto& node : slot->graph().editorNodes())
            if (!node.boundary)
                names.add(node.title.isNotEmpty() ? node.title : node.typeKey);
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.75f);
    const auto row = ImGui::GetFontSize() + dp(3.0f);
    const auto addSize = dp(16.0f);
    const auto room = static_cast<int>((size.y - addSize - dp(6.0f)) / row);
    auto y = min.y + dp(3.0f);
    const auto open = [&] {
        state_.selectTrack(track);
        state_.bringToFront = Panel::devices;
    };
    for (auto index = 0; index < names.size() && index < room; ++index) {
        const auto last = index == room - 1 && names.size() > room;
        const auto text = last ? "+" + juce::String(names.size() - index) + ui::Ls(" autres", " more") : names[index];
        ImGui::SetCursorScreenPos(ImVec2(min.x + dp(3.0f), y));
        ImGui::PushID(index);
        if (ImGui::InvisibleButton("##device", ImVec2(size.x - dp(6.0f), row - dp(1.0f))))
            open();
        const auto hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        list->AddRectFilled(ImVec2(min.x + dp(3.0f), y), ImVec2(min.x + size.x - dp(3.0f), y + row - dp(1.0f)), hovered ? p.bg4 : p.bg3, dp(2.0f));
        ui::clippedText(list, ImVec2(min.x + dp(6.0f), y + dp(1.0f)), size.x - dp(12.0f), hovered ? p.text : p.textDim, text.toRawUTF8());
        if (hovered)
            ui::tooltip(L("Clic : ses périphériques", "Click: its devices"));
        y += row;
    }
    if (names.isEmpty())
        list->AddText(ImVec2(min.x + dp(6.0f), y), p.textFaint, L("aucun", "none"));
    ImGui::PopFont();
    // A device added: the catalogue, for this track.
    ImGui::SetCursorScreenPos(ImVec2(min.x + (size.x - addSize) * 0.5f, min.y + size.y - addSize - dp(3.0f)));
    if (ui::iconButton(icon::add, L("Ajouter un périphérique", "Add a device"), false, 0, ImVec2(addSize, addSize))) {
        open();
        state_.deviceInsertBefore.reset();
        state_.openDeviceCatalogue = true;
    }
}

void LauncherView::sendsCell(const TrackRef& track, const std::vector<TrackRef>& buses, const ImVec2 min, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), p.bg2, dp(3.0f));
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
    if (buses.empty())
        list->AddText(ImVec2(min.x + dp(6.0f), min.y + dp(6.0f)), p.textFaint, L("aucun bus", "no bus"));
    auto outputs = studio_.trackOutputs(track);
    auto changed = false;
    for (std::size_t index = 0; index < buses.size(); ++index) {
        const auto& bus = buses[index];
        const auto y = min.y + dp(3.0f) + static_cast<float>(index) * dp(26.0f);
        const auto key = studio_.routingKey(bus);
        ImGui::PushID(static_cast<int>(index));
        if (bus == track || !studio_.canRoute(track, bus)) {
            ui::clippedText(list, ImVec2(min.x + dp(28.0f), y + dp(6.0f)), size.x - dp(32.0f), p.textFaint, studio_.trackName(bus).toRawUTF8());
            ImGui::PopID();
            continue;
        }
        auto found = std::find_if(outputs.begin(), outputs.end(),
                                  [key](const TrackOutput& output) { return output.hardware == 0 && output.input == 0 && output.destination == key; });
        auto level = found != outputs.end() ? found->gainDb : -60.0f;
        ImGui::SetCursorScreenPos(ImVec2(min.x + dp(4.0f), y));
        ui::KnobStyle style;
        style.diameter = 18.0f;
        style.format = "%.1f dB";
        style.flags = ui::knobNoLabel;
        style.colour = theme::fromArgb(studio_.trackColour(bus));
        auto sending = found != outputs.end();
        if (ui::knob("##send", &level, -60.0f, 6.0f, -60.0f, style)) {
            if (found != outputs.end())
                found->gainDb = level;
            else {
                TrackOutput output;
                output.destination = key;
                output.gainDb = level;
                outputs.push_back(output);
                sending = true;
            }
            changed = true;
        }
        ui::clippedText(list, ImVec2(min.x + dp(26.0f), y + dp(5.0f)), size.x - dp(30.0f), sending ? p.text : p.textDim,
                        studio_.trackName(bus).toRawUTF8());
        ImGui::PopID();
    }
    ImGui::PopFont();
    if (changed)
        if (const auto done = studio_.setTrackOutputs(track, outputs); done.failed()) {
            state_.status = done.getErrorMessage();
            state_.statusIsError = true;
            state_.statusTime = ImGui::GetTime();
        }
}

void LauncherView::ioCell(const TrackRef& track, const ImVec2 min, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(min, ImVec2(min.x + size.x, min.y + size.y), p.bg2, dp(3.0f));
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(4.0f), dp(2.0f)));
    const auto pictureWidth = dp(14.0f);
    const auto width = size.x - dp(8.0f) - pictureWidth - dp(2.0f);
    const auto bus = studio_.isBusTrack(track);
    // A row's picture: a note (MIDI), a microphone (audio), a plug (a bus),
    // a speaker (where it goes).
    const auto picture = [&](const char* glyph, const ImU32 colour, const char* tip) {
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(pictureWidth, ImGui::GetFrameHeight()));
        ui::tooltip(tip);
        ui::centredText(list, at, ImVec2(at.x + pictureWidth, at.y + ImGui::GetFrameHeight()), colour, glyph);
        ImGui::SameLine(0.0f, dp(2.0f));
    };
    // What comes in: a hybrid track's notes and sound, each into its part.
    const auto rowY = [&](const int row) { return min.y + dp(4.0f) + static_cast<float>(row) * (ImGui::GetFrameHeight() + dp(4.0f)); };
    const auto companion = studio_.companionOf(track);
    ImGui::SetCursorScreenPos(ImVec2(min.x + dp(4.0f), rowY(0)));
    if (companion) {
        const auto midiPart = track.midi ? track : *companion;
        const auto audioPart = track.midi ? *companion : track;
        picture(icon::note, p.midi, L("Entrée MIDI : les notes qu'elle joue et enregistre", "MIDI input: the notes it plays and records"));
        routing_ui::midiInputCombo(studio_, midiPart, width);
        ImGui::SetCursorScreenPos(ImVec2(min.x + dp(4.0f), rowY(1)));
        picture(icon::microphone, p.audio, L("Entrée audio : ce qu'elle enregistre dans ses clips audio",
                                             "Audio input: what it records into its audio clips"));
        ImGui::PushID("hybridAudio");
        if (track.midi)
            routing_ui::hybridAudioInputCombo(studio_, track, audioPart, width);
        else
            routing_ui::recordInputCombo(studio_, audioPart, width);
        ImGui::PopID();
    } else if (track.midi) {
        picture(icon::note, p.midi, L("Entrée MIDI", "MIDI input"));
        routing_ui::midiInputCombo(studio_, track, width);
    } else if (bus) {
        picture(icon::connect, p.textDim, L("Ce qui y est routé", "What is routed into it"));
        const auto senders = routing_ui::sendersText(studio_, track);
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(width, ImGui::GetFrameHeight()));
        ui::tooltip(senders.isEmpty() ? L("Aucune piste n'y est routée", "No track is routed into it") : senders.toRawUTF8());
        ui::clippedText(list, ImVec2(at.x + dp(2.0f), at.y + (ImGui::GetFrameHeight() - ImGui::GetFontSize()) * 0.5f), width - dp(2.0f),
                        senders.isEmpty() ? p.textFaint : p.textDim,
                        senders.isEmpty() ? L("aucune piste", "no track") : senders.toRawUTF8());
    } else {
        picture(icon::microphone, p.audio, L("Entrée audio : ce qu'elle enregistre", "Audio input: what it records"));
        routing_ui::recordInputCombo(studio_, track, width);
    }
    // Where it goes: on the last row.
    ImGui::SetCursorScreenPos(ImVec2(min.x + dp(4.0f), rowY(size.y > dp(70.0f) ? 2 : 1)));
    picture(icon::volume, p.play, L("Sortie : où va son son", "Output: where its sound goes"));
    juce::String error;
    routing_ui::mainOutputCombo(studio_, track, width, error);
    ImGui::PopStyleVar();
    ImGui::PopFont();
    if (error.isNotEmpty()) {
        state_.status = error;
        state_.statusIsError = true;
        state_.statusTime = ImGui::GetTime();
    }
}

namespace {

// The options' pictures.
void pictureClips(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto w = (max.x - min.x - dp(2.0f)) / 2.0f;
    const auto h = (max.y - min.y - dp(4.0f)) / 3.0f;
    for (auto column = 0; column < 2; ++column)
        for (auto row = 0; row < 3; ++row) {
            const ImVec2 at(min.x + static_cast<float>(column) * (w + dp(2.0f)), min.y + static_cast<float>(row) * (h + dp(2.0f)));
            list->AddRectFilled(at, ImVec2(at.x + w, at.y + h), colour, dp(1.0f));
        }
}

void pictureDevices(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto h = (max.y - min.y - dp(4.0f)) / 3.0f;
    for (auto row = 0; row < 3; ++row) {
        const auto y = min.y + static_cast<float>(row) * (h + dp(2.0f));
        list->AddRect(ImVec2(min.x, y), ImVec2(max.x, y + h), colour, dp(1.0f));
    }
}

void pictureSends(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto stroke = std::max(1.2f, (max.y - min.y) * 0.12f);
    list->AddLine(ImVec2(min.x + dp(1.0f), max.y - dp(1.0f)), ImVec2(max.x - dp(1.0f), min.y + dp(1.0f)), colour, stroke);
    list->AddLine(ImVec2(max.x - dp(1.0f), min.y + dp(1.0f)), ImVec2(max.x - (max.x - min.x) * 0.45f, min.y + dp(1.0f)), colour, stroke);
    list->AddLine(ImVec2(max.x - dp(1.0f), min.y + dp(1.0f)), ImVec2(max.x - dp(1.0f), min.y + (max.y - min.y) * 0.45f), colour, stroke);
}

void pictureIo(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto stroke = std::max(1.2f, (max.y - min.y) * 0.11f);
    const auto upper = min.y + (max.y - min.y) * 0.3f;
    const auto lower = min.y + (max.y - min.y) * 0.7f;
    const auto head = (max.y - min.y) * 0.22f;
    list->AddLine(ImVec2(min.x, upper), ImVec2(max.x, upper), colour, stroke);
    list->AddLine(ImVec2(max.x, upper), ImVec2(max.x - head, upper - head), colour, stroke);
    list->AddLine(ImVec2(min.x, lower), ImVec2(max.x, lower), colour, stroke);
    list->AddLine(ImVec2(min.x, lower), ImVec2(min.x + head, lower + head), colour, stroke);
}

} // namespace

void LauncherView::optionsColumn(const float height)
{
    const auto& p = theme::palette();
    ImGui::BeginChild("##launcherOptions", ImVec2(dp(30.0f), height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // At the foot of the column, as Bitwig has them.
    struct Option {
        const char* id;
        bool* shown;
        ui::Picture picture;
        const char* tip;
    };
    const Option options[] {
        { "##showClips", &state_.launcherShowClips, pictureClips,
          L("Les clips : les scènes et leurs cases", "The clips: the scenes and their slots") },
        { "##showDevices", &state_.launcherShowDevices, pictureDevices,
          L("Les périphériques de chaque piste (un clic : les voir, + : en ajouter)", "Each track's devices (a click: see them, +: add one)") },
        { "##showSends", &state_.launcherShowSends, pictureSends,
          L("Les envois vers les bus, un bouton chacun", "The sends to the buses, a knob each") },
        { "##showIo", &state_.launcherShowIo, pictureIo,
          L("Ce qui entre (entrées MIDI, entrée audio) et où le son va", "What comes in (MIDI inputs, audio input) and where the sound goes") },
    };
    const auto size = dp(26.0f);
    const auto count = static_cast<float>(sizeof(options) / sizeof(options[0]));
    const auto top = ImGui::GetCursorScreenPos();
    auto y = top.y + height - count * (size + dp(3.0f)) - dp(4.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(top.x, y - dp(4.0f)), ImVec2(top.x + dp(30.0f), top.y + height), p.bg1, dp(3.0f));
    for (const auto& option : options) {
        ImGui::SetCursorScreenPos(ImVec2(top.x + dp(2.0f), y));
        (void) ui::pictureToggle(option.id, option.shown, p.accent, option.picture, option.tip, size, size);
        y += size + dp(3.0f);
    }
    ImGui::EndChild();
}

void LauncherView::launchScene(const int scene)
{
    studio_.launchScene(scene);
}

void LauncherView::stopAll()
{
    studio_.stopAllClips();
}

void LauncherView::toggleSelectedStops()
{
    // The selected empty slots: their stop taken off if any has it, else put back.
    std::vector<SlotRef> empty;
    for (const auto& slot : state_.selectedSlots)
        if (studio_.isValid(slot.track) && !studio_.slotHasClip(slot.track, slot.scene))
            empty.push_back(slot);
    if (empty.empty())
        return;
    const auto anyStops = std::any_of(empty.begin(), empty.end(), [this](const SlotRef& slot) {
        return studio_.slotStops(slot.track, slot.scene);
    });
    for (const auto& slot : empty)
        studio_.setSlotStops(slot.track, slot.scene, !anyStops);
    state_.status = anyStops ? ui::Ls("Stop retiré des cases vides sélectionnées", "Stop taken off the selected empty slots")
                             : ui::Ls("Stop remis sur les cases vides sélectionnées", "Stop put back on the selected empty slots");
    state_.statusIsError = false;
    state_.statusTime = ImGui::GetTime();
}

} // namespace cydaw::v2
