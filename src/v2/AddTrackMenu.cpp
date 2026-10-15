#include "AddTrackMenu.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/SynthFamily.h>

namespace cydaw::v2::add_track {

using ui::dp;
using ui::L;

namespace {

void made(UiState& state, const std::optional<TrackRef>& track)
{
    if (!track)
        return;
    state.selectTrack(*track);
}

void instrument(Studio& studio, UiState& state, const char* french, const char* english, const char* key, const char* hint = nullptr)
{
    if (ImGui::MenuItem(L(french, english)))
        made(state, studio.addMidiTrack(juce::String::fromUTF8(L(french, english)), key));
    if (hint != nullptr)
        ui::tooltip(hint);
}

} // namespace

void items(Studio& studio, UiState& state)
{
    if (ImGui::MenuItem(L("Piste instrument (Opale)", "Instrument track (Opale)")))
        made(state, studio.addMidiTrack({}, "opale-instrument"));
    ui::tooltip(L("Une piste qui joue des notes, avec Opale, le synthé polyphonique (+ dans ses périphériques pour en changer).",
                  "A track that plays notes, with Opale, the polysynth (+ in its devices to change it)."));
    if (ImGui::MenuItem(L("Piste audio", "Audio track")))
        made(state, studio.addAudioTrack());
    ui::tooltip(L("Une piste d'enregistrements et d'échantillons.", "A track of recordings and samples."));
    if (ImGui::MenuItem(L("Piste bus", "Bus track")))
        made(state, studio.addBusTrack());
    ui::tooltip(L("Une piste sans clips où router d'autres pistes : groupe, réverbération partagée…",
                  "A track without clips to route other tracks into: a group, a shared reverb…"));
    ImGui::Separator();
    if (ImGui::BeginMenu(L("Piste instrument avec…", "Instrument track with…"))) {
        // The synth family, each by its name; then the others.
        namespace synth = engine::synth;
        for (int index = 1; index < synth::modelCount; ++index) {
            const auto& info = synth::info(static_cast<synth::Model>(index));
            const auto name = juce::String::fromUTF8((ui::french() ? info.french : info.english).c_str());
            if (ImGui::MenuItem(name.toRawUTF8()))
                made(state, studio.addMidiTrack(name, juce::String(info.instrument)));
            ui::tooltip(ui::french() ? info.frenchHint.c_str() : info.englishHint.c_str());
        }
        ImGui::Separator();
        instrument(studio, state, "Prisme", "Prisme", "prism-instrument", L("Le synthé à tables d'ondes.", "The wavetable synth."));
        instrument(studio, state, "Kick", "Kick", "kick-instrument", L("Le synthé de grosse caisse.", "The kick drum synth."));
        instrument(studio, state, "Rack de pads", "Pad rack", "rack-instrument",
                   L("Seize pads, chacun son instrument et ses effets : boîte à rythmes, kits, sons à soi.",
                     "Sixteen pads, each its instrument and effects: drum machine, kits, sounds of your own."));
        if (ImGui::MenuItem("Batterie")) {
            juce::String error;
            made(state, studio.addBatterieTrack(0, error));
        }
        ui::tooltip(L("La batterie : ses pièces, son mix, ses grooves.", "The drum kit: its pieces, its mix, its grooves."));
        instrument(studio, state, "Échantillonneur simple", "Simple sampler", "simple-sampler-instrument",
                   L("Un son joué sur tout le clavier.", "One sound played over the whole keyboard."));
        instrument(studio, state, "Sampler", "Sampler", "sampler-instrument",
                   L("Des échantillons sur leurs touches, un SFZ.", "Samples over their keys, an SFZ."));
        instrument(studio, state, "Nuée (granulaire)", "Nuée (granular)", "nuee-instrument",
                   L("Un son à vous lu en grains : nappes, textures, nuages.", "A sound of yours read as grains: pads, textures, clouds."));
        ImGui::EndMenu();
    }
}

void button(Studio& studio, UiState& state, const char* id, const ImVec2 size)
{
    const auto& p = theme::palette();
    ImGui::PushID(id);
    const auto at = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("##add", size))
        ImGui::OpenPopup("##addTrack");
    const auto hovered = ImGui::IsItemHovered();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), hovered ? p.bg3 : p.bg2, dp(3.0f));
    list->AddRect(at, ImVec2(at.x + size.x, at.y + size.y), hovered ? p.accent : p.line, dp(3.0f));
    ui::centredText(list, at, ImVec2(at.x + size.x, at.y + size.y), hovered ? p.text : p.textDim, icon::add);
    ui::tooltip(L("Ajouter une piste : instrument, audio, bus…", "Add a track: instrument, audio, bus…"));
    if (ImGui::BeginPopup("##addTrack")) {
        items(studio, state);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

} // namespace cydaw::v2::add_track
