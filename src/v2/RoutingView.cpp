#include "RoutingView.h"

#include "RoutingUi.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/ChannelLayout.h>

#include <imgui.h>

#include <optional>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

// A heading inside one of the two boxes.
void heading(const char* text, const ImU32 colour)
{
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 1.05f);
    ImGui::TextColored(theme::vec(colour), "%s", text);
    ImGui::PopFont();
}

// A small dim line under a control.
void note(const char* text)
{
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.82f);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
}

} // namespace

RoutingView::RoutingView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void RoutingView::report(const juce::String& error)
{
    if (error.isEmpty())
        return;
    state_.status = error;
    state_.statusIsError = true;
    state_.statusTime = ImGui::GetTime();
}

void RoutingView::draw()
{
    const auto& p = theme::palette();
    const auto picked = state_.selectedTrack;
    if (!picked || (!picked->master && !studio_.isValid(*picked)) || (picked->isBus() && !studio_.hardwareBus(picked->bus))) {
        ui::emptyState(L("Aucune piste choisie", "No track picked"),
                       L("Choisissez une piste, ou le master : son entrée et sa sortie s'affichent ici.",
                         "Pick a track, or the master: its input and output show here."));
        return;
    }
    const auto track = *picked;
    ImGui::PushFont(nullptr, ui::currentFontBase() * 1.2f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(10.0f), dp(7.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(8.0f), dp(8.0f)));

    // Which track: its colour, its name, its kind and format.
    const auto colour = track.isMaster() ? p.accent : theme::fromArgb(studio_.trackColour(track));
    const auto origin = ImGui::GetCursorScreenPos();
    const auto titleHeight = ui::currentFontBase() * 1.6f;
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + dp(6.0f), origin.y + titleHeight), colour, dp(2.0f));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(14.0f), origin.y));
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 1.45f);
    ImGui::TextUnformatted(track.isMaster() ? "Master" : studio_.trackName(track).toRawUTF8());
    ImGui::PopFont();
    ImGui::SameLine(0.0f, dp(14.0f));
    ImGui::AlignTextToFramePadding();
    const auto busInput = track.isBus() && studio_.hardwareBus(track.bus)->direction == app::VirtualBusDirection::input;
    const auto kind = track.isBus()                ? juce::String(busInput ? L("entrée virtuelle", "virtual input")
                                                                           : L("sortie virtuelle", "virtual output"))
                      : track.master               ? ui::Ls("le mix de toutes les pistes", "the mix of every track")
                      : track.midi                 ? ui::Ls("piste instrument", "instrument track")
                      : studio_.isBusTrack(track) ? ui::Ls("piste bus", "bus track")
                                                  : ui::Ls("piste audio", "audio track");
    ImGui::TextDisabled("%s  \xc2\xb7  %s", kind.toRawUTF8(), Studio::formatName(studio_.trackChannels(track)).toRawUTF8());

    // Its input, then its output, side by side.
    const auto room = ImGui::GetContentRegionAvail();
    const auto arrow = dp(44.0f);
    const auto width = std::max(dp(220.0f), (room.x - arrow) * 0.5f);
    const auto height = std::max(dp(160.0f), room.y);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, dp(6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(14.0f), dp(12.0f)));
    constexpr auto childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (ImGui::BeginChild("##routingInput", ImVec2(width, height), childFlags)) {
        heading(L("ENTRÉE", "INPUT"), p.audio);
        drawInput(track);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    {
        // The way the sound goes: input, track, output.
        const auto at = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        const auto y = at.y + std::min(height * 0.5f, dp(120.0f));
        const auto left = at.x + dp(8.0f);
        const auto right = at.x + arrow - dp(8.0f);
        list->AddLine(ImVec2(left, y), ImVec2(right - dp(6.0f), y), colour, dp(3.0f));
        list->AddTriangleFilled(ImVec2(right, y), ImVec2(right - dp(12.0f), y - dp(8.0f)), ImVec2(right - dp(12.0f), y + dp(8.0f)),
                                colour);
        ImGui::Dummy(ImVec2(arrow, height));
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::BeginChild("##routingOutput", ImVec2(std::max(dp(220.0f), ImGui::GetContentRegionAvail().x), height), childFlags)) {
        heading(L("SORTIE", "OUTPUT"), p.play);
        drawOutput(track);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
}

// ---- The input ----------------------------------------------------------------------------------

void RoutingView::drawSenders(const TrackRef& track)
{
    const auto& p = theme::palette();
    const auto key = studio_.routingKey(track);
    auto shown = 0;
    for (const auto& candidate : studio_.tracks())
        for (const auto& output : studio_.trackOutputs(candidate)) {
            if (output.hardware != 0 || (track.master ? output.destination != 0 : output.destination != key))
                continue;
            ImGui::PushID(shown++);
            (void) ui::colourChip("##sender", theme::fromArgb(studio_.trackColour(candidate)),
                                  ImVec2(dp(12.0f), ImGui::GetFrameHeight() * 0.6f));
            ImGui::SameLine(0.0f, dp(8.0f));
            ImGui::AlignTextToFramePadding();
            auto line = studio_.trackName(candidate);
            if (output.input != 0)
                line << juce::String::fromUTF8("  \xe2\x86\x92 ") << studio_.trackInputName(track, output.input);
            ImGui::TextUnformatted(line.toRawUTF8());
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(p.textDim), "%+.1f dB%s", output.gainDb, output.preFader ? "  PRE" : "");
            ImGui::PopID();
        }
    if (shown == 0)
        ImGui::TextDisabled("%s", L("aucune piste", "no track"));
}

// A virtual bus's channels on the card: "L  ←  1  Micro".
void RoutingView::drawCardChannels(const app::VirtualAudioBus& bus)
{
    const auto& p = theme::palette();
    const auto input = bus.direction == app::VirtualBusDirection::input;
    const auto device = studio_.engine().deviceChannels();
    const auto& names = input ? device.inputNames : device.outputNames;
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.9f);
    for (auto channel = 0; channel < bus.channels; ++channel) {
        const auto at = bus.deviceChannel(channel);
        const auto speaker = std::string(cydaw::engine::speakerShortName(cydaw::engine::speakerRole(bus.channels, channel)));
        const auto open = at < names.size();
        const auto text = juce::String(speaker) + juce::String::fromUTF8(input ? "  \xe2\x86\x90  " : "  \xe2\x86\x92  ")
                          + juce::String(at + 1) + "  " + (open ? names[at] : ui::Ls("(fermée)", "(closed)"));
        ImGui::TextColored(theme::vec(open ? p.text : p.warn), "%s", text.toRawUTF8());
    }
    ImGui::PopFont();
}

// A virtual bus's effects in one line, and a button to edit them in the
// panels.
void RoutingView::drawBusEffects(const std::uint64_t busId)
{
    const auto& p = theme::palette();
    juce::StringArray names;
    if (const auto* slot = studio_.graphFor(TrackRef::ofBus(busId)))
        for (const auto& node : slot->graph().editorNodes())
            if (!node.boundary)
                names.add(node.title.isNotEmpty() ? node.title : node.typeKey);
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Ses effets", "Its effects"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(names.isEmpty() ? L("aucun", "none") : names.joinIntoString(juce::String::fromUTF8(" \xe2\x86\x92 ")).toRawUTF8());
    ImGui::PopTextWrapPos();
    if (ImGui::Button(L("Modifier ses effets", "Edit its effects"))) {
        state_.selectTrack(TrackRef::ofBus(busId));
        state_.bringToFront = Panel::devices;
    }
    ui::tooltip(L("Ses effets dans Périphériques et Graphe, comme ceux d'une piste (un compresseur, un limiteur…).",
                  "Its effects in Devices and Graph, as a track's (a compressor, a limiter…)."));
}

void RoutingView::drawBusInput(const app::VirtualAudioBus& bus)
{
    const auto& p = theme::palette();
    if (bus.direction == app::VirtualBusDirection::input) {
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Les entrées de la carte", "The card's inputs"));
        drawCardChannels(bus);
        note(L("Ses canaux et son niveau se règlent dans Options \xe2\x80\xba Audio.",
               "Its channels and level are set in Options \xe2\x80\xba Audio."));
        return;
    }
    // An output: the master, when it plays it, and the tracks sent to it.
    if (bus.carriesMaster)
        ImGui::TextUnformatted(L("Le master (à travers ses effets)", "The master (through its effects)"));
    auto shown = 0;
    for (const auto& candidate : studio_.tracks())
        for (const auto& output : studio_.trackOutputs(candidate)) {
            if (output.hardware != bus.id)
                continue;
            ImGui::PushID(shown++);
            (void) ui::colourChip("##sender", theme::fromArgb(studio_.trackColour(candidate)),
                                  ImVec2(dp(12.0f), ImGui::GetFrameHeight() * 0.6f));
            ImGui::SameLine(0.0f, dp(8.0f));
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(studio_.trackName(candidate).toRawUTF8());
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(p.textDim), "%+.1f dB  %s", output.gainDb,
                               output.hardwareAfterEffects ? L("après ses effets", "after its effects")
                                                           : L("à travers ses effets", "through its effects"));
            ImGui::PopID();
        }
    if (!bus.carriesMaster && shown == 0)
        ImGui::TextDisabled("%s", L("rien ne lui est envoyé", "nothing is sent to it"));
}

void RoutingView::drawBusOutput(const app::VirtualAudioBus& bus)
{
    const auto& p = theme::palette();
    drawBusEffects(bus.id);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (bus.direction == app::VirtualBusDirection::output) {
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Les sorties de la carte", "The card's outputs"));
        drawCardChannels(bus);
        return;
    }
    // An input: the tracks that record it, each before or after its effects.
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Les pistes qui l'enregistrent", "The tracks that record it"));
    auto& engine = studio_.engine();
    auto shown = 0;
    for (const auto& candidate : studio_.tracks()) {
        if (candidate.midi || engine.trackMixState(candidate.audioIndex).inputBusId != bus.id)
            continue;
        ImGui::PushID(shown++);
        (void) ui::colourChip("##track", theme::fromArgb(studio_.trackColour(candidate)),
                              ImVec2(dp(12.0f), ImGui::GetFrameHeight() * 0.6f));
        ImGui::SameLine(0.0f, dp(8.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(studio_.trackName(candidate).toRawUTF8());
        ImGui::SameLine();
        afterEffectsChoice(candidate);
        ImGui::PopID();
    }
    if (shown == 0)
        ImGui::TextDisabled("%s", L("aucune piste", "no track"));
    if (bus.monitor)
        note(L("Écoutée (après ses effets) sur ses deux premières sorties.", "Monitored (after its effects) on its first two outputs."));
}

// What an audio track records of its input: after the input's effects, or
// before them (the card's sound as it comes).
void RoutingView::afterEffectsChoice(const TrackRef& track)
{
    auto& engine = studio_.engine();
    const auto after = engine.trackMixState(track.audioIndex).inputAfterEffects;
    if (ImGui::RadioButton(L("après ses effets", "after its effects"), after) && !after) {
        engine.setTrackInputAfterEffects(track.audioIndex, true);
        studio_.markDirty();
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(L("avant (brut)", "before (raw)"), !after) && after) {
        engine.setTrackInputAfterEffects(track.audioIndex, false);
        studio_.markDirty();
    }
}

void RoutingView::drawInput(const TrackRef& track)
{
    const auto& p = theme::palette();
    juce::String error;
    if (track.isBus()) {
        if (const auto bus = studio_.hardwareBus(track.bus))
            drawBusInput(*bus);
        return;
    }
    if (track.master) {
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Les pistes mixées", "The tracks mixed"));
        drawSenders(track);
        ImGui::Spacing();
        note(L("Ce qui sort d'une piste vers le master le rejoint ici, puis passe par ses périphériques et son volume.",
               "What a track sends to the master joins it here, then goes through its devices and its volume."));
        return;
    }
    if (track.midi) {
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Notes", "Notes"));
        ImGui::TextUnformatted(L("Les claviers MIDI branchés", "The MIDI keyboards plugged in"));
        note(L("Elle les joue quand elle est choisie, ou armée.", "It plays them when it is picked, or armed."));
    } else if (studio_.isBusTrack(track)) {
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Ce qui y est routé", "What is routed into it"));
        drawSenders(track);
    } else {
        // What it records: an input bus of the card, and its channels there.
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Enregistre depuis", "Records from"));
        routing_ui::recordInputCombo(studio_, track, -1.0f);
        auto& engine = studio_.engine();
        const auto busId = engine.trackMixState(track.audioIndex).inputBusId;
        if (const auto bus = engine.virtualBus(busId)) {
            drawCardChannels(*bus);
            afterEffectsChoice(track);
            drawBusEffects(busId);
        }
        note(L("Les entrées virtuelles se créent dans Options \xe2\x80\xba Audio ; leurs effets se règlent comme ceux "
               "d'une piste.",
               "Virtual inputs are made in Options \xe2\x80\xba Audio; their effects are set as a track's."));
        if (!studio_.trackSenders(track).empty()) {
            ImGui::Spacing();
            ImGui::TextColored(theme::vec(p.textDim), "%s", L("Reçoit aussi", "Also receives"));
            drawSenders(track);
        }
    }
    // Its other inputs: a sidechain, a second source.
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    routing_ui::inputsEditor(studio_, track, dp(120.0f), error);
    report(error);
}

// ---- The output ---------------------------------------------------------------------------------

void RoutingView::drawOutput(const TrackRef& track)
{
    const auto& p = theme::palette();
    if (track.isBus()) {
        if (const auto bus = studio_.hardwareBus(track.bus))
            drawBusOutput(*bus);
        return;
    }
    if (track.master) {
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Les sorties de la carte son qui le jouent", "The sound card outputs that play it"));
        for (const auto& original : studio_.engine().virtualBuses()) {
            if (original.direction != app::VirtualBusDirection::output)
                continue;
            auto bus = original;
            ImGui::PushID(static_cast<int>(bus.id & 0x7fffffff));
            const auto label = bus.name + "   (" + Studio::formatName(bus.channels) + ", " + routing_ui::deviceChannelsText(bus) + ")";
            if (ImGui::Checkbox(label.toRawUTF8(), &bus.carriesMaster))
                report(studio_.updateHardwareBus(bus).getErrorMessage());
            ImGui::PopID();
        }
        note(L("Les sorties de la carte se règlent dans Options \xe2\x80\xba Audio.",
               "The sound card's outputs are set in Options \xe2\x80\xba Audio."));
        return;
    }
    juce::String error;
    auto outputs = studio_.trackOutputs(track);
    auto changed = false;
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        if (index > 0) {
            ImGui::Spacing();
            ImGui::Separator();
        }
        const auto right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           index == 0 ? L("Sortie", "Output") : (ui::Ls("Envoi ", "Send ") + juce::String(static_cast<int>(index))).toRawUTF8());
        if (outputs.size() > 1) {
            ImGui::SameLine(right);
            if (ui::iconButton(icon::close, L("Retirer cette sortie", "Remove this output")))
                removed = index;
        }
        changed |= routing_ui::destinationCombo("##destination", studio_, track, outputs[index], -1.0f);
        const auto preWidth = dp(64.0f);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - preWidth - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::SliderFloat("##level", &outputs[index].gainDb, -60.0f, 12.0f, "%.1f dB"))
            changed = true;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            outputs[index].gainDb = 0.0f;
            changed = true;
        }
        ui::tooltip(L("Le niveau envoyé (double-clic : 0 dB)", "The level sent (double click: 0 dB)"));
        ImGui::SameLine();
        changed |= ui::toggle("PRE", &outputs[index].preFader, p.accent, ImVec2(preWidth, ImGui::GetFrameHeight()),
                              L("Avant le fader : ce qu'il envoie ne suit pas son fader (il suit encore muet et solo).",
                                "Before the fader: what it sends does not follow its fader (it still follows mute and solo)."));
        if (outputs[index].hardware != 0) {
            // A virtual output: through its effects, or after them.
            auto through = !outputs[index].hardwareAfterEffects;
            if (ImGui::RadioButton(L("à travers ses effets", "through its effects"), through) && !through) {
                outputs[index].hardwareAfterEffects = false;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(L("après ses effets", "after its effects"), !through) && through) {
                outputs[index].hardwareAfterEffects = true;
                changed = true;
            }
        }
        ImGui::PopID();
    }
    if (removed) {
        outputs.erase(outputs.begin() + static_cast<std::ptrdiff_t>(*removed));
        changed = true;
    }
    ImGui::Spacing();
    if (ImGui::Button(L("+ Envoi", "+ Send"))) {
        // Into the first bus it does not feed yet, else the master.
        TrackOutput output;
        for (const auto& candidate : studio_.tracks())
            if (studio_.isBusTrack(candidate) && studio_.canRoute(track, candidate)
                && std::none_of(outputs.begin(), outputs.end(), [&](const TrackOutput& existing) {
                       return existing.destination == studio_.routingKey(candidate);
                   })) {
                output.destination = studio_.routingKey(candidate);
                output.gainDb = -6.0f;
                break;
            }
        outputs.push_back(output);
        changed = true;
    }
    ui::tooltip(L("Une sortie de plus : vers un bus (une réverbération partagée…), une autre piste, son sidechain ou une "
                  "sortie de la carte son.",
                  "One more output: to a bus (a shared reverb…), another track, its sidechain or an output of the sound card."));
    if (changed)
        if (const auto applied = studio_.setTrackOutputs(track, outputs); applied.failed())
            error = applied.getErrorMessage();
    note(L("Une sortie « Carte \xe2\x80\xba … » va droit sur la carte son, sans passer par le master.",
           "A \xe2\x80\x9c" "Card \xe2\x80\xba \xe2\x80\xa6\xe2\x80\x9d output goes straight to the sound card, not through the master."));
    report(error);
}

} // namespace cydaw::v2
