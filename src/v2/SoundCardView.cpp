#include "SoundCardView.h"

#include "RoutingUi.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/ChannelLayout.h>

#include <imgui.h>

#include <array>
#include <optional>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

SoundCardView::SoundCardView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void SoundCardView::report(const juce::String& error)
{
    if (error.isEmpty())
        return;
    state_.status = error;
    state_.statusIsError = true;
    state_.statusTime = ImGui::GetTime();
}

void SoundCardView::draw()
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto device = engine.deviceChannels();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Périphérique", "Device"));
    ImGui::SameLine();
    ImGui::TextUnformatted(studio_.audioStatus().toRawUTF8());

    // The channels open: a bus counts among them, from the first.
    const auto countField = [&](const char* id, const char* label, int& edit, const int active, const int available,
                                const int minimum) {
        ImGui::SameLine(0.0f, dp(18.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "%s", label);
        ImGui::SameLine();
        if (edit < 0)
            edit = active;
        ImGui::SetNextItemWidth(dp(70.0f));
        (void) ImGui::DragInt(id, &edit, 0.1f, minimum, std::max(minimum, available), "%d");
        const auto done = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine(0.0f, dp(3.0f));
        ImGui::TextDisabled("/ %d", available);
        return done;
    };
    const auto inputsDone = countField("##inputsOpen", L("Entrées ouvertes", "Inputs open"), inputsEdit_, device.activeInputs,
                                       device.availableInputs, 0);
    ui::tooltip(L("Combien des entrées de la carte sont ouvertes (les premières). Un bus d'entrée lit parmi elles.",
                  "How many of the card's inputs are open (the first ones). An input bus reads among them."));
    const auto outputsDone = countField("##outputsOpen", L("Sorties ouvertes", "Outputs open"), outputsEdit_,
                                        device.activeOutputs, device.availableOutputs, 1);
    ui::tooltip(L("Combien des sorties de la carte sont ouvertes (les premières). Un bus de sortie joue parmi elles.",
                  "How many of the card's outputs are open (the first ones). An output bus plays among them."));
    if (inputsDone || outputsDone) {
        report(studio_.setDeviceChannelCounts(inputsEdit_, outputsEdit_).getErrorMessage());
        inputsEdit_ = outputsEdit_ = -1;
    }
    if (!ImGui::IsAnyItemActive()) {
        inputsEdit_ = device.activeInputs;
        outputsEdit_ = device.activeOutputs;
    }

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", L("Les entrées et sorties virtuelles sont celles du logiciel, pas d'un projet : chaque projet les "
                                "assigne à ses pistes. Une entrée a ses effets (un compresseur, un limiteur pour un micro…) : le son "
                                "arrive traité, et chaque piste le prend après eux ou avant (brut).",
                                "The virtual inputs and outputs are the software's, not a project's: each project assigns them to "
                                "its tracks. An input has its effects (a compressor, a limiter for a microphone…): the sound comes "
                                "treated, and each track takes it after them or before (raw)."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ui::sectionTitle(L("ENTRÉES VIRTUELLES : CE QUE LES PISTES ENREGISTRENT", "VIRTUAL INPUTS: WHAT TRACKS RECORD"));
    drawBuses(true);
    ImGui::Spacing();
    ui::sectionTitle(L("SORTIES VIRTUELLES : LE MASTER ET LES PISTES ENVOYÉES DIRECTEMENT",
                       "VIRTUAL OUTPUTS: THE MASTER AND THE TRACKS SENT DIRECTLY"));
    drawBuses(false);
    ImGui::TextDisabled("%s", L("Une piste sort directement sur une sortie de la carte en la choisissant comme sortie (panneau Routage, F8).",
                                "A track goes straight to an output of the card when it is picked as its output (Routing panel, F8)."));
}

void SoundCardView::drawBuses(const bool inputs)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto device = engine.deviceChannels();
    const auto& names = inputs ? device.inputNames : device.outputNames;
    const auto direction = inputs ? app::VirtualBusDirection::input : app::VirtualBusDirection::output;
    juce::String error;
    std::optional<std::uint64_t> removed;
    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
    ImGui::PushID(inputs ? "inputs" : "outputs");
    if (ImGui::BeginTable("##buses", 9, flags)) {
        ImGui::TableSetupColumn(L("Nom", "Name"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, dp(84.0f));
        ImGui::TableSetupColumn(L("Canaux de la carte", "Card channels"), ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableSetupColumn(L("Niveau", "Level"), ImGuiTableColumnFlags_WidthFixed, dp(70.0f));
        ImGui::TableSetupColumn(L("Muet", "Mute"), ImGuiTableColumnFlags_WidthFixed, dp(40.0f));
        ImGui::TableSetupColumn(inputs ? L("Écoute", "Monitor") : "Master", ImGuiTableColumnFlags_WidthFixed, dp(56.0f));
        ImGui::TableSetupColumn(L("Effets", "Effects"), ImGuiTableColumnFlags_WidthFixed, dp(112.0f));
        ImGui::TableSetupColumn(L("Niveaux", "Levels"), ImGuiTableColumnFlags_WidthFixed, dp(80.0f));
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();
        for (const auto& original : engine.virtualBuses()) {
            if (original.direction != direction)
                continue;
            auto bus = original;
            auto changed = false;
            ImGui::PushID(static_cast<int>(bus.id & 0x7fffffff));
            ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight() + dp(4.0f));

            ImGui::TableNextColumn();
            // Typed into a copy of its own, kept while the field is in use.
            auto typed = names_.find(bus.id);
            if (typed == names_.end())
                typed = names_.emplace(bus.id, bus.name.toStdString()).first;
            std::array<char, 64> buffer {};
            juce::String::fromUTF8(typed->second.c_str()).copyToUTF8(buffer.data(), buffer.size());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##name", buffer.data(), buffer.size()))
                typed->second = buffer.data();
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const auto name = juce::String::fromUTF8(typed->second.c_str()).trim();
                if (name.isNotEmpty() && name != bus.name) {
                    bus.name = name;
                    changed = true;
                }
                names_.erase(typed);
            } else if (!ImGui::IsItemActive()) {
                names_.erase(typed);
            }

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##format", Studio::formatName(bus.channels).toRawUTF8())) {
                for (auto count = 1; count <= app::VirtualAudioBus::maximumChannels; ++count)
                    if (ImGui::Selectable(Studio::formatName(count).toRawUTF8(), count == bus.channels) && count != bus.channels) {
                        // New channels follow the last one on the card.
                        for (auto channel = bus.channels; channel < count; ++channel)
                            bus.setDeviceChannel(channel, bus.deviceChannel(channel - 1) + 1);
                        bus.channels = count;
                        changed = true;
                    }
                ImGui::EndCombo();
            }

            ImGui::TableNextColumn();
            // Each of its channels on a channel of the card.
            const auto cell = ImGui::GetContentRegionAvail().x;
            const auto width = std::max(dp(46.0f), (cell - dp(3.0f) * static_cast<float>(bus.channels - 1)) / static_cast<float>(bus.channels));
            for (auto channel = 0; channel < bus.channels; ++channel) {
                if (channel > 0)
                    ImGui::SameLine(0.0f, dp(3.0f));
                ImGui::PushID(channel);
                const auto speaker = std::string(cydaw::engine::speakerShortName(cydaw::engine::speakerRole(bus.channels, channel)));
                const auto at = bus.deviceChannel(channel);
                const auto open = at < names.size();
                const auto preview = speaker + " " + std::to_string(at + 1);
                ImGui::SetNextItemWidth(width);
                if (!open)
                    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.warn));
                if (ImGui::BeginCombo("##channel", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
                    for (auto choice = 0; choice < std::max(names.size(), at + 1); ++choice) {
                        const auto label = juce::String(choice + 1) + "  "
                                           + (choice < names.size() ? names[choice] : ui::Ls("(fermée)", "(closed)"));
                        if (ImGui::Selectable(label.toRawUTF8(), choice == at) && choice != at) {
                            bus.setDeviceChannel(channel, choice);
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (!open)
                    ImGui::PopStyleColor();
                ui::tooltip(open ? (inputs ? L("L'entrée de la carte que ce canal lit", "The card input this channel reads")
                                           : L("La sortie de la carte que ce canal joue", "The card output this channel plays"))
                                 : L("Ce canal de la carte n'est pas ouvert : ouvrez-en plus en haut.",
                                     "This channel of the card is not open: open more at the top."));
                ImGui::PopID();
            }

            ImGui::TableNextColumn();
            if (ui::dragNumber("##gain", &bus.gainDb, 0.1f, -60.0f, 24.0f, "%.1f dB", -1.0f))
                changed = true;

            ImGui::TableNextColumn();
            changed |= ui::toggle("M", &bus.muted, p.mute, ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()),
                                  L("Couper ce bus", "Mute this bus"));

            ImGui::TableNextColumn();
            if (inputs)
                changed |= ui::toggle(L("Écoute", "Mon"), &bus.monitor, p.accent,
                                      ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()),
                                      L("Entendre cette entrée sur ses deux premières sorties (repliée en stéréo).",
                                        "Hear this input on its first two outputs (folded to stereo)."));
            else
                changed |= ui::toggle("Master", &bus.carriesMaster, p.accent,
                                      ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()),
                                      L("Cette sortie joue le master ; sinon seulement les pistes envoyées directement.",
                                        "This output plays the master; else only the tracks sent to it directly."));

            ImGui::TableNextColumn();
            {
                // Its effects: their count, and the button that shows them in
                // Devices and Graph to edit, as a track's.
                auto devices = 0;
                if (const auto* slot = studio_.graphFor(TrackRef::ofBus(bus.id)))
                    for (const auto& node : slot->graph().editorNodes())
                        if (!node.boundary)
                            ++devices;
                const auto label = (devices == 0 ? ui::Ls("+ Effets", "+ Effects")
                                                 : juce::String(devices) + ui::Ls(" effet", " effect") + (devices > 1 ? "s" : ""))
                                   + juce::String::fromUTF8("  \xe2\x80\xba");
                if (ImGui::Button(label.toRawUTF8(), ImVec2(-1.0f, 0.0f))) {
                    state_.selectTrack(TrackRef::ofBus(bus.id));
                    state_.bringToFront = Panel::devices;
                    state_.closeAudioSettings = true;
                }
                ui::tooltip(inputs ? L("Ses effets dans Périphériques et Graphe : ils traitent le son avant qu'une piste le prenne.",
                                       "Its effects in Devices and Graph: they treat the sound before a track takes it.")
                                   : L("Ses effets dans Périphériques et Graphe : ils traitent ce qui sort.",
                                       "Its effects in Devices and Graph: they treat what leaves."));
            }

            ImGui::TableNextColumn();
            const auto levels = engine.virtualBusMeterLevels(bus.id);
            ui::meter("##meter", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight() * 0.6f), levels[0], levels[1],
                      levels[0], levels[1], false);

            ImGui::TableNextColumn();
            if (ui::iconButton(icon::close, L("Retirer ce bus", "Remove this bus")))
                removed = bus.id;
            ImGui::PopID();
            if (changed)
                error = studio_.updateHardwareBus(bus).getErrorMessage();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button(inputs ? L("+ Entrée", "+ Input") : L("+ Sortie", "+ Output"))) {
        if (studio_.addHardwareBus(direction) == 0)
            error = ui::Ls("Plus de bus possible (16 au plus)", "No more buses (16 at most)");
    }
    ImGui::PopID();
    if (removed)
        error = studio_.removeHardwareBus(*removed).getErrorMessage();
    report(error);
}

} // namespace cydaw::v2
