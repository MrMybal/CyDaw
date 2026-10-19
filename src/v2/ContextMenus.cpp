#include "ContextMenus.h"

#include "RoutingUi.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

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

ContextMenus::ContextMenus(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void ContextMenus::draw()
{
    if (auto request = std::exchange(state_.contextRequest, std::nullopt)) {
        if (request->kind == ContextRequest::Kind::outputs) {
            outputsTrack_ = request->track;
            ImGui::OpenPopup("##trackOutputs");
        } else {
            shown_ = request;
            ImGui::OpenPopup("##contextMenu");
        }
    }
    // A track's outputs and sends, edited where the pointer is.
    if (ImGui::BeginPopup("##trackOutputs")) {
        if (studio_.isValid(outputsTrack_)) {
            ImGui::PushFont(theme::boldFont(), 0.0f);
            ImGui::TextUnformatted((ui::Ls("Sorties de ", "Outputs of ") + studio_.trackName(outputsTrack_)).toRawUTF8());
            ImGui::PopFont();
            ImGui::Separator();
            ImGui::BeginChild("##outputsBody", ImVec2(dp(380.0f), dp(150.0f)), ImGuiChildFlags_None);
            juce::String error;
            routing_ui::outputsEditor(studio_, outputsTrack_, dp(64.0f), error);
            if (error.isNotEmpty()) {
                state_.status = error;
                state_.statusIsError = true;
                state_.statusTime = ImGui::GetTime();
            }
            ImGui::EndChild();
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##contextMenu")) {
        if (shown_ && studio_.isValid(shown_->track)) {
            if (shown_->kind == ContextRequest::Kind::launcherSlot)
                slotMenu(shown_->track, shown_->scene);
            else
                trackMenu(shown_->track);
        } else {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    renameDialog();
}

void ContextMenus::colourMenu(const std::function<void(juce::uint32)>& apply)
{
    if (!ImGui::BeginMenu(L("Couleur", "Colour")))
        return;
    for (auto index = 0; index < 12; ++index) {
        ImGui::PushID(index);
        const auto colour = theme::trackColour(index);
        if (ui::colourChip("##colour", colour, ImVec2(dp(18.0f), dp(18.0f)))) {
            apply(theme::toArgb(colour));
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
        if (index % 6 != 5)
            ImGui::SameLine(0.0f, dp(3.0f));
    }
    ImGui::EndMenu();
}

void ContextMenus::askRename(const juce::String& current)
{
    std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", current.toRawUTF8());
    openRename_ = true;
    focusRename_ = true;
}

void ContextMenus::trackMenu(const TrackRef& track)
{
    auto& engine = studio_.engine();
    ImGui::TextDisabled("%s", studio_.trackName(track).toRawUTF8());
    ImGui::Separator();
    if (ImGui::MenuItem(L("Renommer…", "Rename…"))) {
        renaming_ = Renaming::track;
        renameTrack_ = track;
        askRename(studio_.trackName(track));
    }
    colourMenu([this, track](const juce::uint32 colour) { studio_.setTrackColour(track, colour); });
    auto mix = studio_.trackMix(track);
    if (ImGui::MenuItem(L("Muet", "Mute"), "M", mix.muted)) {
        mix.muted = !mix.muted;
        studio_.setTrackMix(track, mix);
    }
    if (ImGui::MenuItem("Solo", "S", mix.solo)) {
        mix.solo = !mix.solo;
        studio_.setTrackMix(track, mix);
    }
    if (ImGui::MenuItem(L("Armer pour l'enregistrement", "Arm for recording"), nullptr, mix.armed)) {
        mix.armed = !mix.armed;
        studio_.setTrackMix(track, mix);
    }
    if (ImGui::MenuItem(L("Automation du volume", "Volume automation"), nullptr, studio_.automationVisible(track)))
        studio_.setAutomationVisible(track, !studio_.automationVisible(track));
    ImGui::Separator();
    // Where it is edited.
    if (ImGui::MenuItem(L("Ses périphériques", "Its devices"), "F3")) {
        state_.selectTrack(track);
        state_.bringToFront = Panel::devices;
    }
    if (ImGui::MenuItem(L("Son graphe", "Its graph"), "F4")) {
        state_.selectTrack(track);
        state_.bringToFront = Panel::graph;
    }
    if (track.midi) {
        if (ImGui::MenuItem(L("Ouvrir son premier clip", "Open its first clip"))) {
            if (const auto* clips = engine.midiClipsView(track.midiId); clips != nullptr && !clips->empty()) {
                state_.selectTrack(track);
                state_.editedClip = ClipRef { track, clips->front().id };
                state_.bringToFront = Panel::editor;
            }
        }
        if (ImGui::MenuItem(L("Éditer l'instrument dans l'atelier Plugin", "Edit the instrument in the Plugin workshop"))) {
            state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::trackDevice, track, 0, {} };
            state_.bringToFront = Panel::workshop;
        }
    }
    ImGui::Separator();
    // Groups: the picked tracks gathered; a group folded, undone.
    {
        const auto picked = state_.pickedTracks.empty() ? std::vector<TrackRef> { track } : state_.pickedTracks;
        const auto label = picked.size() > 1 ? (ui::Ls("Grouper les ", "Group the ") + juce::String(static_cast<int>(picked.size()))
                                                + ui::Ls(" pistes choisies", " picked tracks"))
                                              : ui::Ls("Mettre dans un groupe", "Put in a group");
        if (!studio_.isGroupTrack(track) && ImGui::MenuItem(label.toRawUTF8(), "Ctrl+G"))
            if (const auto group = studio_.groupTracks(picked))
                state_.selectTrack(*group);
        if (studio_.isGroupTrack(track)) {
            const auto folded = studio_.isFolded(track);
            if (ImGui::MenuItem(folded ? L("Montrer les pistes du groupe", "Show the group's tracks")
                                       : L("Replier les pistes du groupe", "Fold the group's tracks")))
                studio_.setFolded(track, !folded);
            if (ImGui::MenuItem(L("Dégrouper", "Ungroup")))
                (void) studio_.ungroup(track);
        }
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Monter", "Move up")))
        (void) studio_.moveTrack(track, -1);
    if (ImGui::MenuItem(L("Descendre", "Move down")))
        (void) studio_.moveTrack(track, 1);
    if (ImGui::MenuItem(L("Nouvelle piste instrument", "New instrument track")))
        if (const auto added = studio_.addMidiTrack())
            state_.selectTrack(*added);
    if (ImGui::MenuItem(L("Nouvelle piste bus", "New bus track")))
        if (const auto added = studio_.addBusTrack())
            state_.selectTrack(*added);
    ImGui::Separator();
    // Where it goes: its output at once, or all its outputs and sends.
    if (ImGui::BeginMenu(L("Sortie", "Output"))) {
        juce::String error;
        routing_ui::mainOutputItems(studio_, track, error);
        if (error.isNotEmpty()) {
            state_.status = error;
            state_.statusIsError = true;
            state_.statusTime = ImGui::GetTime();
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem(L("Sorties et envois…", "Outputs and sends…")))
        state_.contextRequest = ContextRequest { ContextRequest::Kind::outputs, track };
    if (ImGui::MenuItem(L("Envoyer vers un nouveau bus", "Send to a new bus")))
        if (const auto bus = studio_.addBusTrack()) {
            auto outputs = studio_.trackOutputs(track);
            outputs.push_back({ studio_.routingKey(*bus), -6.0f });
            (void) studio_.setTrackOutputs(track, outputs);
        }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Supprimer la piste", "Delete the track"))) {
        state_.selectedClips.clear();
        state_.editedClip.reset();
        if (studio_.removeTrack(track))
            state_.selectedTrack.reset();
    }
}

void ContextMenus::slotMenu(const TrackRef& owner, const int scene)
{
    auto& engine = studio_.engine();
    char title[96];
    std::snprintf(title, sizeof(title), "%s · %s %d", studio_.trackName(owner).toRawUTF8(), L("scène", "scene"), scene + 1);
    ImGui::TextDisabled("%s", title);
    ImGui::Separator();
    const auto stopTrack = [&] { studio_.stopTrackClips(owner); };
    // An empty slot's stop (the selected empty slots' with it): a scene
    // launched stops the track there, unless it is taken off.
    const auto stopItem = [&] {
        const auto stops = studio_.slotStops(owner, scene);
        // Its button: a stop, or on an armed track the record button.
        const auto armed = !studio_.isBusTrack(owner) && studio_.trackMix(owner).armed;
        const auto* label = armed ? (stops ? L("Retirer le bouton d'enregistrement", "Remove the record button")
                                           : L("Remettre le bouton d'enregistrement", "Put the record button back"))
                                  : (stops ? L("Retirer le bouton stop", "Remove the stop button")
                                           : L("Remettre le bouton stop", "Put the stop button back"));
        if (ImGui::MenuItem(label, "Ctrl+E")) {
            std::vector<SlotRef> slots;
            for (const auto& slot : state_.selectedSlots)
                if (studio_.isValid(slot.track) && !studio_.slotHasClip(slot.track, slot.scene))
                    slots.push_back(slot);
            if (std::find(slots.begin(), slots.end(), SlotRef { owner, scene }) == slots.end())
                slots = { SlotRef { owner, scene } };
            for (const auto& slot : slots)
                studio_.setSlotStops(slot.track, slot.scene, !stops);
        }
        ui::tooltip(L("Avec ce stop, lancer une scène arrête la piste quand sa case est vide ; sans lui, la piste continue.",
                      "With this stop, launching a scene stops the track where its slot is empty; without it, the track goes on."));
    };
    // The slot's clip: on the track, or on its companion (a hybrid track's
    // other kind). An empty slot takes either kind.
    const auto part = studio_.slotPart(owner, scene);
    if (!part) {
        if (!studio_.isBusTrack(owner)) {
            if (ImGui::MenuItem(L("Nouveau clip MIDI", "New MIDI clip"), owner.midi ? L("Double-clic", "Double click") : nullptr))
                if (const auto made = studio_.newSlotMidiClip(owner, scene)) {
                    state_.editedClip = ClipRef { made->first, made->second };
                    state_.bringToFront = Panel::editor;
                }
            if (!owner.midi)
                ui::tooltip(L("La piste devient hybride : un instrument joue les notes, son son passe dans les effets de la piste.",
                              "The track becomes hybrid: an instrument plays the notes, its sound goes through the track's effects."));
            if (ImGui::MenuItem(L("Nouveau clip audio", "New audio clip")))
                if (const auto made = studio_.newSlotAudioClip(owner, scene)) {
                    state_.editedClip = ClipRef { *made, 0, scene };
                    state_.bringToFront = Panel::editor;
                }
            if (owner.midi)
                ui::tooltip(L("Un clip audio vide. La piste devient hybride : ses clips audio jouent dans ses effets, après l'instrument.",
                              "An empty audio clip. The track becomes hybrid: its audio clips play into its effects, after the instrument."));
            // Armed: either kind recorded here (the other one into its
            // companion: it becomes hybrid).
            if (studio_.trackMix(owner).armed) {
                ImGui::Separator();
                const auto record = [&](const bool midi) {
                    if (const auto started = studio_.recordIntoSlot(owner, scene, midi); started.failed()) {
                        state_.status = started.getErrorMessage();
                        state_.statusIsError = true;
                        state_.statusTime = ImGui::GetTime();
                    }
                };
                if (ImGui::MenuItem(L("Enregistrer des notes ici", "Record notes here")))
                    record(true);
                if (ImGui::MenuItem(L("Enregistrer de l'audio ici", "Record audio here")))
                    record(false);
                ui::tooltip(L("Depuis l'entrée audio de la piste (Entrées / sorties)", "From the track's audio input (Inputs / outputs)"));
            }
        }
        if (ImGui::MenuItem(L("Arrêter la piste", "Stop the track")))
            stopTrack();
        stopItem();
        if (!studio_.isBusTrack(owner))
            ImGui::TextDisabled("%s", L("Ou glissez un sample du Navigateur ici.", "Or drag a sample from the Browser here."));
        return;
    }
    const auto track = *part;
    const auto nextFree = !studio_.slotHasClip(owner, scene + 1);
    if (track.midi) {
        const auto* clip = midiClipForScene(engine, track.midiId, scene);
        if (clip == nullptr)
            return;
        const auto clipId = clip->id;
        if (ImGui::MenuItem(L("Lancer", "Launch"), "\xe2\x96\xb6"))
            (void) studio_.launchSlot(owner, scene);
        if (ImGui::MenuItem(L("Arrêter la piste", "Stop the track")))
            stopTrack();
        if (ImGui::MenuItem(L("Appliquer un traitement MIDI…", "Apply a MIDI processing…")))
            state_.processingRequest = ClipRef { track, clipId };
        if (ImGui::MenuItem(L("Ouvrir dans l'éditeur", "Open in the editor"), L("Double-clic", "Double click"))) {
            state_.editedClip = ClipRef { track, clipId };
            state_.bringToFront = Panel::editor;
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Renommer…", "Rename…"))) {
            renaming_ = Renaming::midiClip;
            renameTrack_ = track;
            renameClip_ = clipId;
            askRename(clip->name);
        }
        colourMenu([&engine, this, track, clipId](const juce::uint32 colour) {
            (void) engine.setMidiClipPresentation(track.midiId, clipId, {}, colour);
            studio_.markDirty();
        });
        if (ImGui::MenuItem(L("Dupliquer dans la scène suivante", "Duplicate into the next scene"), nullptr, false, nextFree)) {
            const auto copy = *clip;
            const auto id = engine.addMidiClip(track.midiId, copy.name, 0.0, copy.lengthBeats, copy.looping, copy.colour, scene + 1);
            if (id != 0) {
                (void) engine.setMidiClipTimelineBounds(track.midiId, id, copy.lengthBeats, copy.playStartBeats, copy.playEndBeats,
                                                        copy.loopStartBeats, copy.loopEndBeats, copy.looping);
                for (const auto& note : copy.notes)
                    (void) engine.copyMidiNote(track.midiId, id, note, note.startBeats);
                studio_.markDirty();
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Supprimer le clip", "Delete the clip"))) {
            (void) engine.stopMidiClipPreview(track.midiId);
            if (engine.removeMidiClip(track.midiId, clipId)) {
                if (state_.editedClip && state_.editedClip->clipId == clipId)
                    state_.editedClip.reset();
                state_.selectedClips.clear();
                studio_.markDirty();
            }
        }
        return;
    }
    const auto clip = engine.launcherClip(track.audioIndex, scene);
    if (!clip)
        return;
    if (ImGui::MenuItem(L("Lancer", "Launch"), "\xe2\x96\xb6"))
        (void) studio_.launchSlot(owner, scene);
    if (ImGui::MenuItem(L("Arrêter la piste", "Stop the track")))
        stopTrack();
    if (ImGui::MenuItem(L("Ouvrir dans l'éditeur", "Open in the editor"), L("Double-clic", "Double click"))) {
        state_.editedClip = ClipRef { track, 0, scene };
        state_.bringToFront = Panel::editor;
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Inverser le clip (Reverse)", "Reverse clip"))) {
        const auto result = studio_.reverseLauncherClip(track, scene);
        state_.status = result.failed() ? result.getErrorMessage() : ui::Ls("Clip inversé", "Clip reversed");
        state_.statusIsError = result.failed();
        state_.statusTime = ImGui::GetTime();
    }
    if (ImGui::MenuItem(L("Dupliquer dans la scène suivante", "Duplicate into the next scene"), nullptr, false, nextFree))
        if (engine.transferLauncherClip(track.audioIndex, scene, track.audioIndex, scene + 1, true))
            studio_.markDirty();
    ImGui::Separator();
    if (ImGui::MenuItem(L("Supprimer le clip", "Delete the clip"))) {
        (void) engine.stopLauncherTrack(track.audioIndex, false);
        if (engine.clearLauncherClip(track.audioIndex, scene)) {
            if (state_.editedClip && *state_.editedClip == ClipRef { track, 0, scene })
                state_.editedClip.reset();
            studio_.markDirty();
        }
    }
}

void ContextMenus::renameDialog()
{
    if (openRename_) {
        ImGui::OpenPopup("##renameDialog");
        openRename_ = false;
    }
    if (!ImGui::BeginPopupModal("##renameDialog", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize))
        return;
    ImGui::TextUnformatted(renaming_ == Renaming::track ? L("Nom de la piste", "Track name") : L("Nom du clip", "Clip name"));
    if (focusRename_) {
        ImGui::SetKeyboardFocusHere();
        focusRename_ = false;
    }
    ImGui::SetNextItemWidth(dp(260.0f));
    const auto entered = ImGui::InputText("##name", renameBuffer_, sizeof(renameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
    if (entered || ui::primaryButton(L("Renommer", "Rename"))) {
        const auto name = juce::String::fromUTF8(renameBuffer_).trim();
        if (name.isNotEmpty()) {
            if (renaming_ == Renaming::track && studio_.isValid(renameTrack_))
                studio_.setTrackName(renameTrack_, name);
            else if (renaming_ == Renaming::midiClip && renameTrack_.midi) {
                (void) studio_.engine().setMidiClipPresentation(renameTrack_.midiId, renameClip_, name, 0);
                studio_.markDirty();
            }
        }
        renaming_ = Renaming::none;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Annuler", "Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        renaming_ = Renaming::none;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace cydaw::v2
