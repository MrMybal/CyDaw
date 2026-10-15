#include "RackView.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/SynthFamily.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

// The pads: sixteen keys from C1 (36), four by four.
constexpr int kFirstNote = 36;
constexpr int kColumns = 4;
constexpr int kRows = 4;

std::unique_ptr<juce::FileChooser> chooser;

juce::String noteName(const int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

// Where the rack shows which pad is chosen, by its track.
std::map<std::uint64_t, int>& chosenNotes()
{
    static std::map<std::uint64_t, int> chosen;
    return chosen;
}

} // namespace

float RackView::paneWidth()
{
    return dp(430.0f);
}

bool RackView::instrumentMenu(Studio& studio, const TrackRef& track, const int note, const std::optional<std::size_t> pad,
                              juce::String& error)
{
    struct Item {
        const char* french;
        const char* english;
        const char* key;
    };
    static constexpr Item drums[] {
        { "Kick (grosse caisse)", "Kick", "kick-instrument" },
        { "Silex (boîte à rythmes)", "Silex (drum machine)", "silex-instrument" },
        { "Échantillonneur simple", "Simple sampler", "simple-sampler-instrument" },
        { "Sampler", "Sampler", "sampler-instrument" },
        { "Nuée (granulaire)", "Nuée (granular)", "nuee-instrument" },
        { "Prisme", "Prisme", "prism-instrument" },
    };
    auto chosen = juce::String();
    for (const auto& item : drums)
        if (ImGui::MenuItem(L(item.french, item.english)))
            chosen = item.key;
    if (ImGui::BeginMenu(L("Synthés", "Synths"))) {
        namespace synth = engine::synth;
        for (int index = 1; index < synth::modelCount; ++index) {
            const auto& info = synth::info(static_cast<synth::Model>(index));
            if (ImGui::MenuItem((ui::french() ? info.french : info.english).c_str()))
                chosen = juce::String(info.instrument);
        }
        ImGui::EndMenu();
    }
    if (chosen.isEmpty())
        return false;
    if (pad)
        return studio.setRackPadInstrument(track, *pad, chosen, error);
    return studio.addRackPad(track, note, chosen, error).has_value();
}

void RackView::chooseSample(const TrackRef& track, const int note)
{
    chooser = std::make_unique<juce::FileChooser>(L("Un son pour ce pad", "A sound for this pad"), juce::File(),
                                                  "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    auto* studio = &studio_;
    auto* state = &state_;
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [studio, state, track, note](const juce::FileChooser& picked) {
                             const auto file = picked.getResult();
                             if (file == juce::File())
                                 return;
                             juce::String error;
                             if (!studio->loadRackPadSample(track, note, file, error)) {
                                 state->status = error;
                                 state->statusIsError = true;
                                 state->statusTime = ImGui::GetTime();
                             }
                         });
}

void RackView::drawPane(const TrackRef& track, const ImVec2 at, const ImVec2 size, const ImU32 colour)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(at.x + size.x, at.y + size.y);
    list->AddRectFilled(at, end, p.bg1, dp(4.0f));
    ImGui::PushID("rackPane");
    auto& chosen = chosenNotes()[track.midiId];
    if (chosen < kFirstNote || chosen >= kFirstNote + kColumns * kRows)
        chosen = kFirstNote;
    // The grid at the left, square cells.
    const auto controlsWidth = dp(150.0f);
    const auto cell = std::min((size.x - controlsWidth - dp(14.0f)) / static_cast<float>(kColumns), (size.y - dp(8.0f)) / static_cast<float>(kRows));
    const ImVec2 gridAt(at.x + dp(4.0f), at.y + dp(4.0f));
    juce::String error;
    for (int row = 0; row < kRows; ++row)
        for (int column = 0; column < kColumns; ++column) {
            const auto note = kFirstNote + (kRows - 1 - row) * kColumns + column;
            const ImVec2 min(gridAt.x + cell * static_cast<float>(column), gridAt.y + cell * static_cast<float>(row));
            const ImVec2 max(min.x + cell - dp(3.0f), min.y + cell - dp(3.0f));
            const auto pad = studio_.rackPadOfNote(track, note);
            const auto* state = pad ? studio_.rackPad(track, *pad) : nullptr;
            ImGui::PushID(note);
            ImGui::SetCursorScreenPos(min);
            ImGui::InvisibleButton("##pad", ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
            const auto hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                chosen = note;
            // Double click: its chain in the device panel (an empty one: its instrument chosen).
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                if (pad)
                    state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(*pad));
                else
                    ImGui::OpenPopup("##padMenu");
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                chosen = note;
                ImGui::OpenPopup("##padMenu");
            }
            // A sound dropped onto it (from the browser or the explorer).
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1) {
                    if (!studio_.loadRackPadSample(track, note, juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data))), error)) {
                        state_.status = error;
                        state_.statusIsError = true;
                        state_.statusTime = ImGui::GetTime();
                    }
                    chosen = note;
                }
                ImGui::EndDragDropTarget();
            }
            // Its face: lit by its sound, its name, its key.
            const auto peak = pad ? std::min(1.0f, studio_.rackPadPeak(track, *pad)) : 0.0f;
            const auto selected = chosen == note;
            const auto base = state != nullptr ? (state->muted ? p.bg2 : theme::mix(colour, p.bg2, 0.72f)) : p.bg2;
            list->AddRectFilled(min, max, theme::mix(base, colour, std::sqrt(peak) * 0.8f), dp(4.0f));
            list->AddRect(min, max, selected ? p.accent : hovered ? p.line : theme::alpha(p.line, 0.6f), dp(4.0f), selected ? dp(2.0f) : dp(1.0f));
            list->PushClipRect(min, max, true);
            if (state != nullptr) {
                ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
                // Its name over two lines if it must (the key's corner kept free on the first).
                list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(min.x + dp(4.0f), min.y + dp(12.0f)), p.text, state->name.toRawUTF8(),
                              nullptr, max.x - min.x - dp(6.0f));
                if (state->solo || state->muted)
                    list->AddText(ImVec2(max.x - dp(12.0f), max.y - ImGui::GetFontSize() - dp(2.0f)), state->solo ? p.accent : p.textFaint,
                                  state->solo ? "S" : "M");
                if (state->choke > 0)
                    list->AddText(ImVec2(min.x + dp(4.0f), max.y - ImGui::GetFontSize() - dp(2.0f)), p.textFaint,
                                  (juce::String("C") + juce::String(state->choke)).toRawUTF8());
                ImGui::PopFont();
            } else {
                ui::centredText(list, min, max, p.textFaint, "+");
            }
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.62f);
            const auto key = noteName(note);
            const auto keySize = ImGui::CalcTextSize(key.toRawUTF8());
            list->AddText(ImVec2(max.x - keySize.x - dp(3.0f), min.y + dp(3.0f)), p.textFaint, key.toRawUTF8());
            ImGui::PopFont();
            list->PopClipRect();
            if (hovered)
                ui::tooltip(state != nullptr ? L("Clic : le choisir. Double-clic : sa chaîne (instrument et effets). Clic droit : son instrument, un son, "
                                                 "muet, solo, retirer. Glissez-y un son.",
                                                 "Click: choose it. Double click: its chain (instrument and effects). Right click: its instrument, a "
                                                 "sound, mute, solo, remove. Drop a sound on it.")
                                             : L("Un pad vide : clic droit pour lui donner un instrument, ou glissez-y un son.",
                                                 "An empty pad: right click to give it an instrument, or drop a sound on it."));
            // Its menu.
            if (ImGui::BeginPopup("##padMenu")) {
                ImGui::TextDisabled("%s %s", L("Pad", "Pad"), noteName(note).toRawUTF8());
                ImGui::Separator();
                if (pad && ImGui::MenuItem(L("Ouvrir sa chaîne", "Open its chain")))
                    state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(*pad));
                if (ImGui::BeginMenu(pad ? L("Changer d'instrument", "Change instrument") : L("Instrument", "Instrument"))) {
                    if (instrumentMenu(studio_, track, note, pad, error))
                        chosen = note;
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem(L("Charger un son\xe2\x80\xa6", "Load a sound\xe2\x80\xa6")))
                    chooseSample(track, note);
                if (pad && state != nullptr) {
                    ImGui::Separator();
                    Studio::RackPadMix mix { state->note, state->name, state->playAs, state->gainDb, state->pan, state->muted, state->solo, state->choke };
                    if (ImGui::MenuItem(L("Muet", "Mute"), nullptr, state->muted)) {
                        mix.muted = !mix.muted;
                        (void) studio_.setRackPadMix(track, *pad, mix);
                    }
                    if (ImGui::MenuItem(L("Solo", "Solo"), nullptr, state->solo)) {
                        mix.solo = !mix.solo;
                        (void) studio_.setRackPadMix(track, *pad, mix);
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem(L("Retirer le pad", "Remove the pad")))
                        (void) studio_.removeRackPad(track, *pad);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    if (error.isNotEmpty()) {
        state_.status = error;
        state_.statusIsError = true;
        state_.statusTime = ImGui::GetTime();
    }
    // The chosen pad's settings at the right.
    const ImVec2 controlsAt(gridAt.x + cell * static_cast<float>(kColumns) + dp(6.0f), at.y + dp(4.0f));
    if (const auto pad = studio_.rackPadOfNote(track, chosen))
        drawPadControls(track, *pad, controlsAt, end.x - controlsAt.x - dp(4.0f), colour);
    else {
        ImGui::SetCursorScreenPos(controlsAt);
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        ImGui::TextColored(theme::vec(p.textDim), "%s %s", L("Pad", "Pad"), noteName(chosen).toRawUTF8());
        ImGui::SetCursorScreenPos(ImVec2(controlsAt.x, ImGui::GetCursorScreenPos().y + dp(4.0f)));
        if (ImGui::Button(L("Instrument\xe2\x80\xa6", "Instrument\xe2\x80\xa6")))
            ImGui::OpenPopup("##emptyMenu");
        ImGui::SetCursorScreenPos(ImVec2(controlsAt.x, ImGui::GetCursorScreenPos().y + dp(2.0f)));
        if (ImGui::Button(L("Un son\xe2\x80\xa6", "A sound\xe2\x80\xa6")))
            chooseSample(track, chosen);
        if (ImGui::BeginPopup("##emptyMenu")) {
            juce::String menuError;
            (void) instrumentMenu(studio_, track, chosen, std::nullopt, menuError);
            ImGui::EndPopup();
        }
        ImGui::PopFont();
    }
    ImGui::PopID();
}

void RackView::drawPadControls(const TrackRef& track, const std::size_t pad, const ImVec2 at, const float width, const ImU32 colour)
{
    const auto& p = theme::palette();
    const auto* state = studio_.rackPad(track, pad);
    if (state == nullptr)
        return;
    Studio::RackPadMix mix { state->note, state->name, state->playAs, state->gainDb, state->pan, state->muted, state->solo, state->choke };
    auto changed = false;
    ImGui::PushID(static_cast<int>(pad));
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    const auto line = ImGui::GetFrameHeight() + dp(3.0f);
    auto y = at.y;
    // Its name and key.
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(at, ImVec2(at.x + width, at.y + dp(30.0f)), true);
    list->AddText(at, p.text, state->name.toRawUTF8());
    const auto key = juce::String(L("touche ", "key ")) + noteName(state->note);
    list->AddText(ImVec2(at.x, at.y + ImGui::GetFontSize() + dp(2.0f)), p.textFaint, key.toRawUTF8());
    list->PopClipRect();
    y += ImGui::GetFontSize() * 2.0f + dp(6.0f);
    // Its chain: its instrument and effects, in the device panel.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ImGui::Button(L("Ouvrir sa chaîne", "Open its chain"), ImVec2(width, 0.0f)))
        state_.openRackPad = std::make_pair(track.owner(), static_cast<int>(pad));
    ui::tooltip(L("Son instrument et ses effets, dans les périphériques (◀ pour revenir au rack).",
                  "Its instrument and effects, in the devices (◀ to come back to the rack)."));
    y += line + dp(2.0f);
    // Level and pan.
    auto gain = mix.gainDb;
    ui::KnobStyle style;
    style.diameter = 26.0f;
    style.colour = colour;
    style.label = L("Volume", "Volume");
    style.unit = "dB";
    style.format = "%.1f";
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ui::knob("##gain", &gain, -60.0f, 12.0f, 0.0f, style)) {
        mix.gainDb = gain;
        changed = true;
    }
    auto pan = mix.pan;
    style.label = L("Pan", "Pan");
    style.unit = "";
    style.format = "%.2f";
    style.flags = ui::knobBipolar;
    ImGui::SetCursorScreenPos(ImVec2(at.x + width * 0.5f, y));
    if (ui::knob("##pan", &pan, -1.0f, 1.0f, 0.0f, style)) {
        mix.pan = pan;
        changed = true;
    }
    y += dp(26.0f) + ImGui::GetFontSize() * 2.2f;
    // Mute and solo side by side.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    if (ImGui::Checkbox(L("Muet", "Mute"), &mix.muted))
        changed = true;
    ImGui::SetCursorScreenPos(ImVec2(at.x + width * 0.5f, y));
    if (ImGui::Checkbox("Solo", &mix.solo))
        changed = true;
    y += line;
    // Its choke group.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    ImGui::SetNextItemWidth(width);
    const auto chokeLabel = mix.choke == 0 ? juce::String(L("Sans coupure", "No choke"))
                                           : juce::String(L("Coupe le groupe ", "Chokes group ")) + juce::String(mix.choke);
    if (ImGui::BeginCombo("##choke", chokeLabel.toRawUTF8())) {
        for (int group = 0; group <= 4; ++group) {
            const auto label = group == 0 ? juce::String(L("Sans coupure", "No choke")) : juce::String(L("Groupe ", "Group ")) + juce::String(group);
            if (ImGui::Selectable(label.toRawUTF8(), group == mix.choke)) {
                mix.choke = group;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Les pads d'un même groupe se coupent : un charleston fermé coupe l'ouvert.",
                  "Pads of one group cut each other: a closed hi-hat cuts the open one."));
    y += line;
    // The note its instrument receives.
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
    ImGui::SetNextItemWidth(width);
    const auto playsLabel = mix.playAs < 0 ? juce::String(L("Joue sa touche", "Plays its key"))
                                           : juce::String(L("Joue ", "Plays ")) + noteName(mix.playAs);
    auto plays = mix.playAs < 0 ? state->note : mix.playAs;
    if (ImGui::DragInt("##plays", &plays, 0.15f, 0, 127, playsLabel.toRawUTF8())) {
        mix.playAs = plays == state->note ? -1 : plays;
        changed = true;
    }
    ui::tooltip(L("La note que son instrument reçoit : la touche du pad, ou une autre (C4 pour un instrument accordé).",
                  "The note its instrument receives: the pad's key, or another (C4 for a pitched instrument)."));
    if (changed)
        (void) studio_.setRackPadMix(track, pad, mix);
    ImGui::PopFont();
    ImGui::PopID();
}

} // namespace cydaw::v2
