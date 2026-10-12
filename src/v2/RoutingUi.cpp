#include "RoutingUi.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/ChannelLayout.h>

#include <imgui.h>

#include <array>

namespace cydaw::v2::routing_ui {

using ui::dp;
using ui::L;

juce::String destinationName(const Studio& studio, const std::uint64_t key, const std::uint64_t input)
{
    if (key == 0)
        return "Master";
    const auto track = studio.trackForKey(key);
    if (!track)
        return ui::Ls("(disparue)", "(gone)");
    auto name = studio.trackName(*track);
    if (input != 0)
        name << juce::String::fromUTF8(" \xe2\x80\xba ") << studio.trackInputName(*track, input);
    return name;
}

juce::String deviceChannelsText(const app::VirtualAudioBus& bus)
{
    juce::StringArray parts;
    auto first = bus.deviceChannel(0);
    auto last = first;
    for (auto channel = 1; channel <= bus.channels; ++channel) {
        const auto next = channel < bus.channels ? bus.deviceChannel(channel) : -10;
        if (next == last + 1) {
            last = next;
            continue;
        }
        parts.add(first == last ? juce::String(first + 1) : juce::String(first + 1) + "-" + juce::String(last + 1));
        first = last = next;
    }
    return parts.joinIntoString(",");
}

juce::String hardwareName(const Studio& studio, const std::uint64_t busId)
{
    const auto bus = studio.engine().virtualBus(busId);
    if (!bus)
        return ui::Ls("Carte \xe2\x80\xba (disparue)", "Card \xe2\x80\xba (gone)");
    return ui::Ls("Carte", "Card") + juce::String::fromUTF8(" \xe2\x80\xba ") + bus->name + " (" + deviceChannelsText(*bus) + ")";
}

juce::String outputName(const Studio& studio, const TrackOutput& output)
{
    return output.hardware != 0 ? hardwareName(studio, output.hardware)
                                : destinationName(studio, output.destination, output.input);
}

juce::String mainOutputLabel(const Studio& studio, const TrackRef& track)
{
    const auto outputs = studio.trackOutputs(track);
    auto label = juce::String::fromUTF8("\xe2\x86\x92 ") + outputName(studio, outputs.front());
    if (outputs.size() > 1)
        label << " +" << juce::String(static_cast<int>(outputs.size() - 1));
    return label;
}

namespace {

struct Destination {
    std::uint64_t key {};
    std::uint64_t input {};
    std::uint64_t hardware {};
};

// The destinations an output of `track` may take: the master, the outputs
// of the sound card, then the buses, then the other tracks it can go into
// without a loop, each followed by its other inputs.
std::vector<Destination> destinationsFor(const Studio& studio, const TrackRef& track)
{
    std::vector<Destination> buses { Destination {} };
    for (const auto& bus : studio.engine().virtualBuses())
        if (bus.direction == app::VirtualBusDirection::output)
            buses.push_back({ 0, 0, bus.id });
    std::vector<Destination> others;
    for (const auto& candidate : studio.tracks()) {
        if (!studio.canRoute(track, candidate))
            continue;
        auto& into = studio.isBusTrack(candidate) ? buses : others;
        const auto key = studio.routingKey(candidate);
        into.push_back({ key, 0 });
        for (const auto& input : studio.trackInputs(candidate))
            into.push_back({ key, input.node });
    }
    buses.insert(buses.end(), others.begin(), others.end());
    return buses;
}

bool pickDestination(const char* id, const Studio& studio, const TrackRef& track, TrackOutput& output, const float width)
{
    auto changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, outputName(studio, output).toRawUTF8())) {
        for (const auto& destination : destinationsFor(studio, track)) {
            const auto to = destination.key != 0 ? studio.trackForKey(destination.key) : std::nullopt;
            // Another input, under its track: "› Sidechain".
            const auto label = destination.hardware != 0 ? hardwareName(studio, destination.hardware)
                               : destination.input != 0
                                   ? juce::String::fromUTF8("    \xe2\x80\xba ") + studio.trackInputName(*to, destination.input)
                                   : destinationName(studio, destination.key) + (to && studio.isBusTrack(*to) ? "  (bus)" : "");
            const auto selected = destination.hardware == output.hardware && destination.key == output.destination
                                  && destination.input == output.input;
            ImGui::PushID(static_cast<int>((destination.input ^ (destination.hardware << 20)) & 0x7fffffff));
            if (ImGui::Selectable(label.toRawUTF8(), selected) && !selected) {
                output.destination = destination.key;
                output.input = destination.input;
                output.hardware = destination.hardware;
                changed = true;
            }
            if (destination.hardware != 0)
                ui::tooltip(L("Directement sur ces sorties de la carte son, sans passer par le master.",
                              "Straight to these outputs of the sound card, not through the master."));
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

bool destinationCombo(const char* id, const Studio& studio, const TrackRef& track, TrackOutput& output, const float width)
{
    return pickDestination(id, studio, track, output, width);
}

void outputsEditor(Studio& studio, const TrackRef& track, const float labelWidth, juce::String& error)
{
    const auto& p = theme::palette();
    auto outputs = studio.trackOutputs(track);
    auto changed = false;
    std::optional<std::size_t> removed;
    for (std::size_t index = 0; index < outputs.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textDim), "%s", index == 0 ? L("Sortie", "Output") : L("Envoi", "Send"));
        ImGui::SameLine(labelWidth);
        const auto room = ImGui::GetContentRegionAvail().x;
        const auto levelWidth = dp(64.0f);
        const auto preWidth = dp(32.0f);
        const auto removeWidth = outputs.size() > 1 ? ImGui::GetFrameHeight() + dp(2.0f) : 0.0f;
        // A narrow panel: the destination on its line, its level under it.
        const auto twoLines = room - preWidth - levelWidth - removeWidth - dp(6.0f) < dp(90.0f);
        changed |= pickDestination("##destination", studio, track, outputs[index],
                                    twoLines ? std::max(dp(40.0f), room - removeWidth)
                                             : room - preWidth - levelWidth - removeWidth - dp(6.0f));
        ui::tooltip(L("Où va son son : le master, une sortie de la carte son, un bus, une autre piste ou l'une de ses autres "
                      "entrées (un sidechain).",
                      "Where its sound goes: the master, an output of the sound card, a bus, another track or one of its "
                      "other inputs (a sidechain)."));
        const auto removeButton = [&] {
            if (outputs.size() > 1) {
                ImGui::SameLine(0.0f, dp(2.0f));
                if (ui::iconButton(icon::close, L("Retirer cette sortie", "Remove this output")))
                    removed = index;
            }
        };
        if (twoLines) {
            removeButton();
            ImGui::Dummy(ImVec2(labelWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f));
            ImGui::SameLine(labelWidth);
        } else {
            ImGui::SameLine(0.0f, dp(2.0f));
        }
        changed |= ui::toggle("PRE", &outputs[index].preFader, p.accent, ImVec2(preWidth, ImGui::GetFrameHeight()),
                              L("Avant le fader : ce qu'il envoie ne suit pas son fader (il suit encore muet et solo).",
                                "Before the fader: what it sends does not follow its fader (it still follows mute and solo)."));
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ui::dragNumber("##level", &outputs[index].gainDb, 0.1f, -60.0f, 12.0f, "%.1f dB",
                           twoLines ? std::max(dp(48.0f), room - preWidth - dp(2.0f)) : levelWidth))
            changed = true;
        ui::tooltip(L("Le niveau envoyé", "The level sent"));
        if (!twoLines)
            removeButton();
        ImGui::PopID();
    }
    if (removed) {
        outputs.erase(outputs.begin() + static_cast<std::ptrdiff_t>(*removed));
        changed = true;
    }
    ImGui::Dummy(ImVec2(labelWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::SameLine(labelWidth);
    if (ImGui::Button(L("+ Sortie", "+ Output"))) {
        // Into the first bus it can feed, else the master.
        auto next = std::uint64_t {};
        for (const auto& destination : destinationsFor(studio, track))
            if (destination.key != 0 && destination.input == 0
                && std::none_of(outputs.begin(), outputs.end(),
                                [&destination](const auto& output) { return output.destination == destination.key; })) {
                next = destination.key;
                break;
            }
        TrackOutput output;
        output.destination = next;
        output.gainDb = next == 0 ? 0.0f : -6.0f;
        outputs.push_back(output);
        changed = true;
    }
    ui::tooltip(L("Une sortie de plus : un envoi vers un bus (réverbération partagée…), une autre piste, son sidechain ou "
                  "une sortie de la carte son.",
                  "One more output: a send to a bus (a shared reverb…), another track, its sidechain or an output of "
                  "the sound card."));
    if (changed)
        if (const auto applied = studio.setTrackOutputs(track, outputs); applied.failed())
            error = applied.getErrorMessage();
}

void mainOutputCombo(Studio& studio, const TrackRef& track, const float width, juce::String& error)
{
    auto outputs = studio.trackOutputs(track);
    if (pickDestination("##mainOutput", studio, track, outputs.front(), width))
        if (const auto applied = studio.setTrackOutputs(track, outputs); applied.failed())
            error = applied.getErrorMessage();
}

void inputsEditor(Studio& studio, const TrackRef& track, const float labelWidth, juce::String& error)
{
    const auto& p = theme::palette();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Entrées", "Inputs"));
    ImGui::SameLine(labelWidth);
    ImGui::TextDisabled("%s", track.midi                   ? L("principale : son instrument", "main: its instrument")
                              : studio.isBusTrack(track) ? L("principale : ce qui y est routé", "main: what is routed into it")
                                                         : L("principale : ses clips", "main: its clips"));
    ui::tooltip(L("Ce qui est routé dans son entrée principale rejoint son son avant ses effets.",
                  "What is routed into its main input joins its sound before its devices."));
    const auto inputs = studio.trackInputs(track);
    std::optional<std::uint64_t> removed;
    for (const auto& input : inputs) {
        ImGui::PushID(static_cast<int>(input.node & 0x7fffffff));
        ImGui::Dummy(ImVec2(labelWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f));
        ImGui::SameLine(labelWidth);
        // Its name, edited in place: the graph's input card shows it too.
        std::array<char, 64> buffer {};
        input.name.copyToUTF8(buffer.data(), buffer.size());
        ImGui::SetNextItemWidth(std::max(dp(60.0f), ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - dp(4.0f)));
        if (ImGui::InputText("##name", buffer.data(), buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)
            || ImGui::IsItemDeactivatedAfterEdit()) {
            const auto name = juce::String::fromUTF8(buffer.data()).trim();
            if (auto* slot = studio.graphFor(track); slot != nullptr && name.isNotEmpty() && name != input.name)
                (void) slot->graph().editorSetAlias(input.node, name);
        }
        ui::tooltip(L("Une entrée de plus : dans le graphe de la piste, reliez-la où elle sert (le sidechain d'un compresseur).",
                      "One more input: in the track's graph, wire it where it is needed (the sidechain of a compressor)."));
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ui::iconButton(icon::close, L("Retirer cette entrée (et ce qui y est routé)", "Remove this input (and what is routed into it)")))
            removed = input.node;
        ImGui::PopID();
    }
    if (removed)
        if (const auto applied = studio.removeTrackInput(track, *removed); applied.failed())
            error = applied.getErrorMessage();
    ImGui::Dummy(ImVec2(labelWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f));
    ImGui::SameLine(labelWidth);
    ImGui::BeginDisabled(inputs.size() >= Studio::maximumTrackInputs);
    if (ImGui::Button(L("+ Entrée", "+ Input")))
        if (!studio.addTrackInput(track))
            error = ui::Ls("Cette entrée n'a pas pu être ajoutée", "This input could not be added");
    ImGui::EndDisabled();
    ui::tooltip(L("Une entrée audio de plus (jusqu'à 3) où d'autres pistes se routent : un sidechain, une seconde source. "
                  "Elle apparaît dans son graphe ; reliez-la où elle sert.",
                  "One more audio input (up to 3) other tracks are routed into: a sidechain, a second source. "
                  "It appears in its graph; wire it where it is needed."));
}

juce::String sendersText(const Studio& studio, const TrackRef& track)
{
    juce::StringArray names;
    const auto key = studio.routingKey(track);
    if (key != 0 || track.master)
        for (const auto& candidate : studio.tracks())
            for (const auto& output : studio.trackOutputs(candidate)) {
                if (output.hardware != 0 || (track.master ? output.destination != 0 : output.destination != key))
                    continue;
                auto name = studio.trackName(candidate);
                if (output.input != 0)
                    name << juce::String::fromUTF8(" \xe2\x86\x92 ") << studio.trackInputName(track, output.input);
                names.addIfNotAlreadyThere(name);
            }
    return names.joinIntoString(", ");
}

void sendersLine(const Studio& studio, const TrackRef& track, const float labelWidth)
{
    const auto& p = theme::palette();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Reçoit", "Receives"));
    ImGui::SameLine(labelWidth);
    const auto names = sendersText(studio, track);
    if (names.isEmpty()) {
        ImGui::TextDisabled("%s", L("aucune piste", "no track"));
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(names.toRawUTF8());
    ImGui::PopTextWrapPos();
}

void mainOutputItems(Studio& studio, const TrackRef& track, juce::String& error)
{
    auto outputs = studio.trackOutputs(track);
    for (const auto& destination : destinationsFor(studio, track)) {
        if (destination.input != 0)
            continue;
        const auto selected = outputs.front().hardware == destination.hardware && outputs.front().destination == destination.key
                              && outputs.front().input == 0;
        const auto label = destination.hardware != 0 ? hardwareName(studio, destination.hardware)
                                                     : destinationName(studio, destination.key);
        ImGui::PushID(static_cast<int>(destination.hardware & 0x7fffffff));
        if (ImGui::MenuItem(label.toRawUTF8(), nullptr, selected) && !selected) {
            outputs.front().destination = destination.key;
            outputs.front().input = 0;
            outputs.front().hardware = destination.hardware;
            if (const auto applied = studio.setTrackOutputs(track, outputs); applied.failed())
                error = applied.getErrorMessage();
        }
        ImGui::PopID();
    }
}

void formatCombo(Studio& studio, const TrackRef& track, const float width, juce::String& error)
{
    const auto channels = studio.trackChannels(track);
    ImGui::SetNextItemWidth(width);
    if (track.master || track.midi) {
        ImGui::BeginDisabled();
        if (ImGui::BeginCombo("##format", Studio::formatName(channels).toRawUTF8()))
            ImGui::EndCombo();
        ImGui::EndDisabled();
        ui::tooltip(L("Le master et les pistes instrument sont en stéréo.", "The master and instrument tracks are stereo."));
        return;
    }
    if (ImGui::BeginCombo("##format", Studio::formatName(channels).toRawUTF8())) {
        for (auto count = 1; count <= cydaw::engine::maximumTrackChannels; ++count) {
            juce::StringArray speakers;
            for (auto channel = 0; channel < count; ++channel)
                speakers.add(juce::String(std::string(cydaw::engine::speakerShortName(cydaw::engine::speakerRole(count, channel)))));
            const auto label = Studio::formatName(count) + "   " + speakers.joinIntoString(" ");
            ImGui::PushID(count);
            if (ImGui::Selectable(label.toRawUTF8(), count == channels) && count != channels)
                if (const auto applied = studio.setTrackChannels(track, count); applied.failed())
                    error = applied.getErrorMessage();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Son format : combien de canaux elle porte (mono, stéréo, quadriphonie, 5.1, 7.1…). Ses clips y sont "
                  "convertis, ses effets traitent chaque canal, et son son se convertit vers où il va (le master est en stéréo).",
                  "Its format: how many channels it carries (mono, stereo, quad, 5.1, 7.1…). Its clips convert to it, "
                  "its devices process every channel, and its sound converts into where it goes (the master is stereo)."));
}

void recordInputCombo(Studio& studio, const TrackRef& track, const float width)
{
    if (track.master || track.midi || !studio.isValid(track)) {
        ImGui::TextDisabled("%s", track.midi ? L("instrument (MIDI)", "instrument (MIDI)") : L("les pistes mixées", "the tracks mixed"));
        return;
    }
    auto& engine = studio.engine();
    const auto current = engine.trackMixState(track.audioIndex).inputBusId;
    const auto label = [](const app::VirtualAudioBus& bus) {
        return bus.name + "  (" + Studio::formatName(bus.channels) + ", " + deviceChannelsText(bus) + ")";
    };
    juce::String shown = ui::Ls("(aucune)", "(none)");
    for (const auto& bus : engine.virtualBuses())
        if (bus.id == current)
            shown = label(bus);
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##recordInput", shown.toRawUTF8())) {
        for (const auto& bus : engine.virtualBuses()) {
            if (bus.direction != app::VirtualBusDirection::input)
                continue;
            ImGui::PushID(static_cast<int>(bus.id & 0x7fffffff));
            if (ImGui::Selectable(label(bus).toRawUTF8(), bus.id == current) && bus.id != current
                && engine.setTrackInputBus(track.audioIndex, bus.id))
                studio.markDirty();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Ce qu'elle enregistre : une entrée de la carte son (un micro, une paire stéréo, une prise quad ou 5.1). "
                  "Les entrées se règlent dans le panneau Routage.",
                  "What it records: an input of the sound card (a microphone, a stereo pair, a quad or 5.1 rig). "
                  "The inputs are set in the Routing panel."));
}

void channelMeters(const Studio& studio, const TrackRef& track, const ImVec2 size)
{
    const auto& p = theme::palette();
    const auto meter = studio.trackMeter(track);
    const auto channels = track.master || track.midi ? 2 : std::max(1, studio.trackChannels(track));
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto gap = dp(2.0f);
    const auto barWidth = std::max(dp(3.0f), (size.x - gap * static_cast<float>(channels - 1)) / static_cast<float>(channels));
    const auto barHeight = std::max(dp(4.0f), size.y);
    for (auto channel = 0; channel < channels; ++channel) {
        const auto x = origin.x + static_cast<float>(channel) * (barWidth + gap);
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + barWidth, origin.y + barHeight);
        list->AddRectFilled(min, max, p.bg0, dp(1.0f));
        list->AddRect(min, max, p.line, dp(1.0f), 1.0f);
        const auto peak = meter.channelPeak[static_cast<std::size_t>(std::min(channel, 7))];
        const auto level = ui::meterPosition(juce::Decibels::gainToDecibels(peak, -100.0f));
        if (level > 0.0f) {
            const auto colour = level > 0.92f ? p.meterHigh : level > 0.72f ? p.meterMid : p.meterLow;
            list->AddRectFilled(ImVec2(min.x, max.y - (max.y - min.y) * level), max, colour, dp(1.0f));
        }
        // The speaker's name inside its bar, at the foot.
        const auto name = std::string(cydaw::engine::speakerShortName(cydaw::engine::speakerRole(channels, channel)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
        const auto textSize = ImGui::CalcTextSize(name.c_str());
        if (textSize.x < barWidth + dp(2.0f))
            list->AddText(ImVec2(x + (barWidth - textSize.x) * 0.5f, max.y - textSize.y - dp(1.0f)), theme::alpha(p.text, 0.85f),
                          name.c_str());
        ImGui::PopFont();
    }
    ImGui::Dummy(size);
}

} // namespace cydaw::v2::routing_ui
