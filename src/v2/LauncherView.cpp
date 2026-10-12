#include "LauncherView.h"
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
            if (juce::roundToInt(clip.startBeats / 16.0) == scene)
                return &clip;
    return nullptr;
}

} // namespace

LauncherView::LauncherView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void LauncherView::draw()
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto tracks = studio_.tracks();
    constexpr auto scenes = app::AudioTrackController::maximumLauncherScenes;

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
                  "vides sélectionnées. La rangée ■ sous les clips arrête chaque piste. La colonne Master lance les scènes.",
                  "Click a track's name: pick it. Click a clip or a slot: select it (Ctrl, Shift: add). A clip's triangle "
                  "launches it; while it plays, its square stops it. Double click: open the clip, or make one in an empty MIDI "
                  "slot. An empty slot's square stops the track, and a scene launched stops the tracks whose slot is empty; "
                  "Ctrl+E (or the right click) takes that stop off the selected empty slots, or puts it back. The ■ row under "
                  "the clips stops each track. The Master column launches the scenes."));
    // Ctrl+E: the selected empty slots' stop taken off, or put back.
    if (state_.focused == Panel::launcher && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_E))
        toggleSelectedStops();

    // As in Bitwig: the tracks' columns, each with its mixer strip under its
    // clips, and the master's column on the right, whose slots launch the
    // scenes. The names stay on top and the stops and strips at the bottom;
    // only the scenes between them scroll up and down (their bar beside the
    // master's column), everything sideways together.
    const auto available = ImGui::GetContentRegionAvail();
    const auto gap = dp(3.0f);
    const auto cellWidth = dp(100.0f);
    const auto headerHeight = dp(26.0f);
    const auto stopHeight = dp(24.0f);
    const auto scrollbar = ImGui::GetStyle().ScrollbarSize;
    const auto barWidth = dp(10.0f);
    const auto masterWidth = cellWidth;
    const auto slotsTop = headerHeight + gap;
    // From the bottom: the sideways bar, the strips, the stop row; the scenes
    // have what is left (one row at least).
    const auto bottom = std::max(dp(200.0f), available.y - scrollbar - dp(1.0f));
    const auto aboveStrips = slotsTop + dp(24.0f) + gap + stopHeight + gap * 2.0f;
    const auto stripHeight = std::max(dp(110.0f), std::min(std::clamp(available.y * 0.42f, dp(150.0f), dp(260.0f)),
                                                           bottom - aboveStrips));
    const auto mixerTop = bottom - stripHeight;
    const auto stopRowTop = mixerTop - gap * 2.0f - stopHeight;
    const auto scenesBottom = stopRowTop - gap;
    const auto viewport = std::max(dp(24.0f), scenesBottom - slotsTop);
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

    const auto gridWidth = std::max(dp(60.0f), available.x - masterWidth - barWidth - gap * 3.0f);
    ImGui::BeginChild("##grid", ImVec2(gridWidth, available.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto viewTop = origin.y + slotsTop;
    const auto viewBottom = viewTop + viewport;
    if (tracks.empty())
        ui::emptyState(L("Aucune piste", "No tracks"), L("Les clips du lanceur appartiennent aux pistes.", "Launcher clips belong to tracks."));

    for (std::size_t column = 0; column < tracks.size(); ++column) {
        const auto& track = tracks[column];
        const ImVec2 min(origin.x + column * (cellWidth + gap), origin.y);
        const ImVec2 max(min.x + cellWidth, min.y + headerHeight);
        const auto selected = state_.selectedTrack && *state_.selectedTrack == track;
        ImGui::PushID(static_cast<int>(column));
        // The header selects its track, which the device panel and the graph
        // then show; a double click brings the devices to the front. The stop
        // button over it keeps its own clicks.
        ImGui::SetCursorScreenPos(min);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##header", ImVec2(cellWidth, headerHeight));
        const auto headerHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            state_.selectTrack(track);
            state_.focused = Panel::launcher;
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            state_.selectTrack(track);
            state_.contextRequest = ContextRequest { ContextRequest::Kind::track, track };
        }
        if (headerHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            state_.bringToFront = Panel::devices;
        if (headerHovered)
            ui::tooltip(L("Clic : choisir la piste (Périphériques, Graphe). Double-clic : ses périphériques. Clic droit : son menu.",
                          "Click: pick the track (Devices, Graph). Double click: its devices. Right click: its menu."));
        list->AddRectFilled(min, max, selected ? p.bg4 : headerHovered ? p.bg3 : p.bg2, dp(3.0f));
        list->AddRectFilled(min, ImVec2(min.x + cellWidth, min.y + dp(3.0f)), theme::fromArgb(studio_.trackColour(track)), dp(3.0f),
                            ImDrawFlags_RoundCornersTop);
        if (selected)
            list->AddRect(min, max, theme::fromArgb(studio_.trackColour(track)), dp(3.0f), dp(1.5f));
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.88f);
        ui::clippedText(list, ImVec2(min.x + dp(6.0f), min.y + dp(6.0f)), cellWidth - dp(12.0f), p.text, studio_.trackName(track).toRawUTF8());
        ImGui::PopFont();
        ImGui::PopID();
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
    for (auto scene = 0; scene < scenes; ++scene) {
        const auto y = viewTop + scene * rowStep - sceneScroll_;
        if (y + cellHeight < viewTop || y > viewBottom)
            continue;
        for (std::size_t column = 0; column < tracks.size(); ++column) {
            const auto& track = tracks[column];
            const SlotRef slot { track, scene };
            const ImVec2 min(origin.x + column * (cellWidth + gap), y);
            const ImVec2 max(min.x + cellWidth, y + cellHeight);
            juce::String name;
            juce::uint32 colour {};
            auto exists = false;
            auto progress = 0.0f;
            std::uint64_t midiClip {};
            if (!track.midi) {
                if (const auto clip = engine.launcherClip(track.audioIndex, scene)) {
                    exists = true;
                    name = clip->name.isNotEmpty() ? clip->name : clip->sourceFile.getFileNameWithoutExtension();
                    colour = clip->colour;
                }
            } else if (const auto* clip = midiClipForScene(engine, track.midiId, scene)) {
                exists = true;
                name = clip->name;
                colour = clip->colour;
                midiClip = clip->id;
            }
            const auto playing = exists && studio_.playingScene(track) == scene;
            const auto pending = exists && studio_.pendingScene(track) == scene;
            if (playing)
                progress = track.midi ? engine.midiLauncherClipProgress(track.midiId) : engine.launcherClipProgress(track.audioIndex);
            const auto stops = !exists && studio_.slotStops(track, scene);
            const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
            const auto inPlay = [&](const ImVec2 point) { return exists && point.x < min.x + playWidth; };
            const auto inStop = [&](const ImVec2 point) {
                return stops && std::abs(point.x - centre.x) <= stopSize && std::abs(point.y - centre.y) <= stopSize;
            };

            ImGui::PushID(static_cast<int>(column) * 64 + scene);
            ImGui::SetCursorScreenPos(min);
            ImGui::InvisibleButton("##cell", ImVec2(cellWidth, cellHeight), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
            const auto hovered = ImGui::IsItemHovered();
            const auto overPlay = hovered && inPlay(io.MousePos);
            const auto overStop = hovered && inStop(io.MousePos);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                state_.selectTrack(track);
                if (!state_.isSlotSelected(slot))
                    state_.selectedSlots = { slot };
                state_.contextRequest = ContextRequest { ContextRequest::Kind::launcherSlot, track, scene };
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                state_.focused = Panel::launcher;
                const auto at = io.MouseClickedPos[0];
                if (inPlay(at)) {
                    // The clip's button: launch it, or stop it while it plays.
                    if (playing || pending)
                        studio_.stopTrackClips(track);
                    else
                        (void) studio_.launchSlot(track, scene);
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
                    if (track.midi && exists)
                        state_.selectedClips = { ClipRef { track, midiClip } };
                }
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && track.midi && !exists && !overStop) {
                // A new MIDI clip in this scene.
                const auto id = engine.addMidiClip(track.midiId, {}, scene * 16.0, 4.0, true, studio_.trackColour(track));
                if (id != 0) {
                    state_.editedClip = ClipRef { track, id };
                    state_.bringToFront = Panel::editor;
                    studio_.markDirty();
                }
            } else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && track.midi && exists && !overPlay) {
                state_.editedClip = ClipRef { track, midiClip };
                state_.bringToFront = Panel::editor;
            }
            if (overPlay)
                ui::tooltip(playing || pending ? L("Arrêter", "Stop") : L("Lancer", "Launch"));
            else if (overStop)
                ui::tooltip(L("Arrêter la piste. Une scène lancée l'arrête aussi (Ctrl+E retire ce stop).",
                              "Stop the track. A scene launched stops it too (Ctrl+E takes this stop off)."));

            const auto selectedSlot = state_.isSlotSelected(slot);
            if (exists) {
                const auto fill = theme::fromArgb(colour != 0 ? colour : (track.midi ? 0xff4fd3aeu : 0xffff9b2fu));
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
                list->AddRectFilled(min, max, hovered ? p.bg3 : p.bg1, dp(3.0f));
                // Its stop, when it has it.
                if (stops)
                    list->AddRect(ImVec2(centre.x - dp(4.0f), centre.y - dp(4.0f)), ImVec2(centre.x + dp(4.0f), centre.y + dp(4.0f)),
                                  overStop ? p.text : p.textFaint, 0.0f, overStop ? 1.6f : 1.0f);
            }
            if (selectedSlot)
                list->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), p.text, dp(3.0f), 1.5f);
            ImGui::PopID();
        }
    }
    ImGui::PopClipRect();

    // Under the slots: each track's stop, its clips stopped.
    for (std::size_t column = 0; column < tracks.size(); ++column) {
        const auto& track = tracks[column];
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

    // Each track's mixer strip, under its clips.
    for (std::size_t column = 0; column < tracks.size(); ++column) {
        ImGui::SetCursorScreenPos(ImVec2(origin.x + column * (cellWidth + gap), origin.y + mixerTop));
        mixer_strip::track(studio_, state_, tracks[column], static_cast<int>(column), cellWidth, stripHeight);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(tracks.size() * (cellWidth + gap), bottom));
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
    ImGui::EndChild();

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
        if (ui::iconButton(icon::stop, L("Tout arrêter", "Stop all"), false, 0, ImVec2(dp(20.0f), dp(20.0f))))
            stopAll();
        ImGui::PopFont();
        // Its scenes, scrolled with the grid's.
        const auto masterViewTop = top.y + slotsTop;
        ImGui::PushClipRect(ImVec2(top.x, masterViewTop), ImVec2(top.x + masterWidth, masterViewTop + viewport), true);
        for (auto scene = 0; scene < scenes; ++scene) {
            const ImVec2 min(top.x, masterViewTop + scene * rowStep - sceneScroll_);
            if (min.y + cellHeight < masterViewTop || min.y > masterViewTop + viewport)
                continue;
            ImGui::PushID(scene + 1'000);
            ImGui::SetCursorScreenPos(min);
            char label[32];
            std::snprintf(label, sizeof(label), "%s  %s %d", icon::play, L("Scène", "Scene"), scene + 1);
            if (ImGui::Button(label, ImVec2(masterWidth, cellHeight)))
                launchScene(scene);
            ui::tooltip(L("Lancer la scène : les clips de cette rangée", "Launch the scene: the clips of this row"));
            ImGui::PopID();
        }
        ImGui::PopClipRect();
        if (ImGui::IsWindowHovered() && io.MousePos.y >= masterViewTop && io.MousePos.y < masterViewTop + viewport)
            (void) sceneWheel(io.MouseWheel);
        // The stop row's end: every track stopped.
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + stopRowTop));
        {
            char label[48];
            std::snprintf(label, sizeof(label), "%s  %s", icon::stop, L("Tout", "All"));
            if (ImGui::Button(label, ImVec2(masterWidth, stopHeight)))
                stopAll();
            ui::tooltip(L("Arrêter tous les clips", "Stop every clip"));
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + mixerTop));
        mixer_strip::master(studio_, state_, masterWidth, stripHeight);
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
