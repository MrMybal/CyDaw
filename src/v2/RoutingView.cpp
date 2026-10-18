#include "RoutingView.h"

#include "RoutingUi.h"

#include "Icons.h"
#include "MidiDevices.h"
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
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.82f);
    ImGui::TextColored(theme::vec(colour), "%s", text);
    ImGui::PopFont();
}

// The width of the labels on the left of each line.
float labelColumn()
{
    return dp(64.0f);
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
                       L("Choisissez une piste, ou le master : ses entrées et ses sorties s'affichent ici.",
                         "Pick a track, or the master: its inputs and outputs show here."));
        return;
    }
    const auto track = *picked;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(6.0f), dp(5.0f)));

    // Which track: its colour, its name, its kind and format.
    const auto colour = track.isMaster() ? p.accent : theme::fromArgb(studio_.trackColour(track));
    const auto origin = ImGui::GetCursorScreenPos();
    const auto titleHeight = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + dp(4.0f), origin.y + titleHeight), colour, dp(2.0f));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(10.0f), origin.y));
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 1.1f);
    ImGui::TextUnformatted(track.isMaster() ? "Master" : studio_.trackName(track).toRawUTF8());
    ImGui::PopFont();
    ImGui::SameLine(0.0f, dp(10.0f));
    const auto busInput = track.isBus() && studio_.hardwareBus(track.bus)->direction == app::VirtualBusDirection::input;
    const auto kind = track.isBus()                ? juce::String(busInput ? L("entrée virtuelle", "virtual input")
                                                                           : L("sortie virtuelle", "virtual output"))
                      : track.master               ? ui::Ls("le mix de toutes les pistes", "the mix of every track")
                      : track.midi                 ? ui::Ls("piste instrument", "instrument track")
                      : studio_.isBusTrack(track) ? ui::Ls("piste bus", "bus track")
                                                  : ui::Ls("piste audio", "audio track");
    ImGui::TextDisabled("%s  \xc2\xb7  %s", kind.toRawUTF8(), Studio::formatName(studio_.trackChannels(track)).toRawUTF8());

    // Its inputs, then its outputs, side by side, as tall as they need.
    const auto room = ImGui::GetContentRegionAvail();
    const auto arrow = dp(28.0f);
    const auto width = std::max(dp(240.0f), (room.x - arrow) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg2);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, dp(5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(10.0f), dp(8.0f)));
    constexpr auto childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY;
    const auto top = ImGui::GetCursorScreenPos().y;
    if (ImGui::BeginChild("##routingInput", ImVec2(width, 0.0f), childFlags)) {
        heading(L("ENTRÉES", "INPUTS"), p.audio);
        drawInput(track);
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    {
        // The way the sound goes: inputs, track, outputs.
        const auto at = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        const auto y = top + dp(22.0f);
        const auto left = at.x + dp(6.0f);
        const auto right = at.x + arrow - dp(6.0f);
        list->AddLine(ImVec2(left, y), ImVec2(right - dp(5.0f), y), colour, dp(2.0f));
        list->AddTriangleFilled(ImVec2(right, y), ImVec2(right - dp(8.0f), y - dp(5.0f)), ImVec2(right - dp(8.0f), y + dp(5.0f)), colour);
        ImGui::Dummy(ImVec2(arrow, dp(30.0f)));
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::BeginChild("##routingOutput", ImVec2(std::max(dp(240.0f), ImGui::GetContentRegionAvail().x), 0.0f), childFlags)) {
        heading(L("SORTIES", "OUTPUTS"), p.play);
        drawOutput(track);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
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

void RoutingView::drawMidiInputs(const TrackRef& track)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto none = engine.midiTrackInputs(track.midiId).empty();
    const auto hears = engine.midiTrackHearsInputs(track.midiId);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "MIDI");
    ImGui::SameLine(labelColumn());
    routing_ui::midiInputCombo(studio_, track, -1.0f);
    ImGui::Dummy(ImVec2(labelColumn() - ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::SameLine(labelColumn());
    note(none    ? L("Elle n'entend aucun clavier.", "It hears no keyboard.")
         : hears ? L("Elle l'entend : elle est armée, ou choisie.", "It hears it: it is armed, or picked.")
                 : L("Elle l'entendra armée, ou choisie (sans autre piste armée).",
                     "It will hear it armed, or picked (no other track armed)."));
}

void RoutingView::drawMidiOutput(const TrackRef& track)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto current = engine.midiTrackOutput(track.midiId);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "MIDI");
    ImGui::SameLine(labelColumn());
    ImGui::SetNextItemWidth(-1.0f);
    const auto shown = current.isEmpty() ? ui::Ls("Aucune sortie MIDI", "No MIDI output") : midi_devices::outputName(current);
    if (ImGui::BeginCombo("##midiOutput", shown.toRawUTF8())) {
        if (ImGui::Selectable(L("Aucune sortie MIDI", "No MIDI output"), current.isEmpty())) {
            engine.setMidiTrackOutput(track.midiId, {});
            studio_.markDirty();
        }
        if (midi_devices::outputs().isEmpty())
            ImGui::TextDisabled("%s", L("Aucune sortie MIDI branchée", "No MIDI output plugged in"));
        for (const auto& device : midi_devices::outputs())
            if (ImGui::Selectable(device.name.toRawUTF8(), device.identifier == current)) {
                engine.setMidiTrackOutput(track.midiId, device.identifier);
                studio_.markDirty();
            }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Ses notes (après ses effets MIDI) aussi envoyées à un appareil MIDI : un synthé, une boîte à rythmes.",
                  "Its notes (after its MIDI effects) also sent to a MIDI device: a synth, a drum machine."));
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
        note(L("Ce qui sort d'une piste vers le master le rejoint ici, puis passe par ses périphériques et son volume.",
               "What a track sends to the master joins it here, then goes through its devices and its volume."));
        return;
    }
    if (track.midi) {
        drawMidiInputs(track);
    } else if (studio_.isBusTrack(track)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Reçoit", "Receives"));
        ImGui::SameLine(labelColumn());
        const auto senders = routing_ui::sendersText(studio_, track);
        ImGui::TextUnformatted(senders.isEmpty() ? L("aucune piste", "no track") : senders.toRawUTF8());
    } else {
        // What it records: an input of the card, its level, before or after
        // the input's effects.
        auto& engine = studio_.engine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "Audio");
        ImGui::SameLine(labelColumn());
        const auto meterWidth = dp(46.0f);
        routing_ui::recordInputCombo(studio_, track, std::max(dp(80.0f), ImGui::GetContentRegionAvail().x - meterWidth - dp(6.0f)));
        const auto busId = engine.trackMixState(track.audioIndex).inputBusId;
        ImGui::SameLine(0.0f, dp(6.0f));
        {
            const auto levels = engine.virtualBusMeterLevels(busId);
            const auto at = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + ImGui::GetFrameHeight() * 0.25f));
            ui::meter("##recordMeter", ImVec2(meterWidth, ImGui::GetFrameHeight() * 0.5f), levels[0], levels[1], levels[0], levels[1], false);
            ui::tooltip(L("Le niveau de l'entrée qu'elle enregistre", "The level of the input it records"));
        }
        if (engine.virtualBus(busId)) {
            ImGui::Dummy(ImVec2(labelColumn() - ImGui::GetStyle().ItemSpacing.x, 0.0f));
            ImGui::SameLine(labelColumn());
            afterEffectsChoice(track);
        }
        if (!studio_.trackSenders(track).empty()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(theme::vec(p.textDim), "%s", L("Reçoit", "Receives"));
            ImGui::SameLine(labelColumn());
            ImGui::TextUnformatted(routing_ui::sendersText(studio_, track).toRawUTF8());
        }
    }
    // Its other audio inputs: a sidechain, a second source.
    routing_ui::inputsEditor(studio_, track, labelColumn(), error);
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
    if (track.midi)
        drawMidiOutput(track);
    // Where its sound goes, and its sends: a line each.
    routing_ui::outputsEditor(studio_, track, labelColumn(), error);
    note(L("Une sortie « Carte \xe2\x80\xba … » va droit sur la carte son, sans passer par le master.",
           "A \xe2\x80\x9c" "Card \xe2\x80\xba \xe2\x80\xa6\xe2\x80\x9d output goes straight to the sound card, not through the master."));
    // Its format.
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "Format");
    ImGui::SameLine(labelColumn());
    routing_ui::formatCombo(studio_, track, std::min(dp(160.0f), ImGui::GetContentRegionAvail().x), error);
    ui::tooltip(L("Ses canaux : mono, stéréo, jusqu'au 7.1 (celui d'une piste audio ; une piste instrument est stéréo)",
                  "Its channels: mono, stereo, up to 7.1 (an audio track's; an instrument track is stereo)"));
    report(error);
}

} // namespace cydaw::v2
