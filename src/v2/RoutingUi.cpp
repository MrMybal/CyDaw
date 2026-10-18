#include "MidiDevices.h"
#include "RoutingUi.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/ChannelLayout.h>

#include <imgui.h>

#include <array>
#include <cmath>
#include <optional>

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

// The lists' groups, in the order destinationsFor() gives them, each
// under its heading (the master alone, none).
enum class Group { master, card, bus, track };

Group groupOf(const Studio& studio, const Destination& destination)
{
    if (destination.hardware != 0)
        return Group::card;
    if (destination.key == 0)
        return Group::master;
    const auto to = studio.trackForKey(destination.key);
    return to && studio.isBusTrack(*to) ? Group::bus : Group::track;
}

void groupHeading(const Group group)
{
    const auto* name = group == Group::card ? L("Carte son, en direct", "Sound card, direct")
                       : group == Group::bus ? L("Bus", "Buses")
                                             : L("Autres pistes", "Other tracks");
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(theme::palette().textFaint));
    ImGui::SeparatorText(name);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// A destination's name under its heading: an output of the card by its
// name and channels, another input of a track under it ("› Sidechain").
juce::String itemName(const Studio& studio, const Destination& destination)
{
    if (destination.hardware != 0) {
        const auto bus = studio.engine().virtualBus(destination.hardware);
        return bus ? bus->name + " (" + deviceChannelsText(*bus) + ")" : ui::Ls("(disparue)", "(gone)");
    }
    if (destination.input != 0)
        if (const auto to = studio.trackForKey(destination.key))
            return juce::String::fromUTF8("    \xe2\x80\xba ") + studio.trackInputName(*to, destination.input);
    return destinationName(studio, destination.key);
}

bool pickDestination(const char* id, const Studio& studio, const TrackRef& track, TrackOutput& output, const float width)
{
    auto changed = false;
    ImGui::SetNextItemWidth(width);
    // Its list as wide as its names, even from a narrow combo.
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(width, dp(190.0f)), 0.0f), ImVec2(FLT_MAX, dp(420.0f)));
    if (ImGui::BeginCombo(id, outputName(studio, output).toRawUTF8())) {
        std::optional<Group> last;
        for (const auto& destination : destinationsFor(studio, track)) {
            const auto group = groupOf(studio, destination);
            if (group != Group::master && group != last)
                groupHeading(group);
            last = group;
            const auto label = itemName(studio, destination);
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
        if (outputs[index].hardware != 0) {
            // An output of the card: through its effects, or straight.
            ImGui::SameLine(0.0f, dp(2.0f));
            auto straight = outputs[index].hardwareAfterEffects;
            if (ui::toggle(L("Direct", "Direct"), &straight, p.accent, ImVec2(0.0f, ImGui::GetFrameHeight()),
                           L("Direct : droit sur la carte, sans passer par les effets de cette sortie ; sinon à travers eux.",
                             "Direct: straight to the card, not through this output's effects; else through them."))) {
                outputs[index].hardwareAfterEffects = straight;
                changed = true;
            }
        }
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
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Sidechain", "Sidechain"));
    ui::tooltip(L("Ses entrées audio de plus, où d'autres pistes se routent : un sidechain (un compresseur qui suit la grosse "
                  "caisse), une seconde source. Elles apparaissent dans son graphe.",
                  "Its extra audio inputs other tracks are routed into: a sidechain (a compressor following the kick), a "
                  "second source. They appear in its graph."));
    ImGui::SameLine(labelWidth);
    const auto inputs = studio.trackInputs(track);
    std::optional<std::uint64_t> removed;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        const auto& input = inputs[index];
        ImGui::PushID(static_cast<int>(input.node & 0x7fffffff));
        if (index > 0) {
            ImGui::Dummy(ImVec2(labelWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f));
            ImGui::SameLine(labelWidth);
        }
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
    if (!inputs.empty()) {
        ImGui::Dummy(ImVec2(labelWidth - ImGui::GetStyle().ItemSpacing.x, 0.0f));
        ImGui::SameLine(labelWidth);
    }
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
    std::optional<Group> last;
    for (const auto& destination : destinationsFor(studio, track)) {
        if (destination.input != 0)
            continue;
        const auto group = groupOf(studio, destination);
        if (group != Group::master && group != last)
            groupHeading(group);
        last = group;
        const auto selected = outputs.front().hardware == destination.hardware && outputs.front().destination == destination.key
                              && outputs.front().input == 0;
        const auto label = itemName(studio, destination);
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

std::uint32_t midiInputActivity(const Studio& studio, const TrackRef& track, const juce::String& input)
{
    const auto& engine = studio.engine();
    const auto clips = engine.midiTrackClipActivity(track.midiId);
    if (input == app::AudioTrackController::allMidiInputs)
        return clips + engine.midiInActivity();
    if (input == app::AudioTrackController::computerKeyboardInput)
        return clips + engine.computerKeyboardActivity();
    return clips + engine.midiInputActivity(input);
}

juce::String midiInputName(const juce::String& input)
{
    if (input == app::AudioTrackController::allMidiInputs)
        return ui::Ls("Toutes les entrées", "Every input");
    if (input == app::AudioTrackController::computerKeyboardInput)
        return ui::Ls("Clavier de l'ordinateur", "Computer keyboard");
    return midi_devices::inputName(input);
}

void midiInputCombo(Studio& studio, const TrackRef& track, const float width)
{
    if (!track.midi)
        return;
    const auto& p = theme::palette();
    auto& engine = studio.engine();
    const auto inputs = engine.midiTrackInputs(track.midiId);
    const auto current = inputs.empty() ? juce::String() : inputs.front();
    // A narrow combo (a launcher's cell) says it short.
    const auto brief = width > 0.0f && width < dp(150.0f);
    auto shown = inputs.empty()                                                  ? ui::Ls(brief ? "Aucune" : "Aucune entrée", brief ? "None" : "No input")
                 : brief && current == app::AudioTrackController::allMidiInputs ? ui::Ls("Toutes", "All ins")
                 : brief && current == app::AudioTrackController::computerKeyboardInput ? ui::Ls("Clavier PC", "PC keys")
                                                                                         : midiInputName(current);
    if (inputs.size() > 1)
        shown << " +" << juce::String(static_cast<int>(inputs.size()) - 1);
    auto* list = ImGui::GetWindowDrawList();
    const auto at = ImGui::GetCursorScreenPos();
    const auto height = ImGui::GetFrameHeight();
    const auto light = std::round(height * 0.42f);
    // Room for its light before the name.
    const auto spaces = static_cast<int>(std::ceil((light + dp(4.0f)) / std::max(1.0f, ImGui::CalcTextSize(" ").x)));
    const auto preview = juce::String::repeatedString(" ", spaces) + shown;
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(width, dp(200.0f)), 0.0f), ImVec2(FLT_MAX, dp(420.0f)));
    if (ImGui::BeginCombo("##midiInput", preview.toRawUTF8())) {
        const auto pick = [&](const juce::String& input, const char* label) {
            const auto selected = input.isEmpty() ? inputs.empty() : inputs.size() == 1 && current == input;
            if (ImGui::Selectable(label, selected) && !selected) {
                engine.setMidiTrackInputs(track.midiId, input.isEmpty() ? std::vector<juce::String> {} : std::vector<juce::String> { input });
                studio.markDirty();
            }
        };
        pick(app::AudioTrackController::allMidiInputs, L("Toutes les entrées", "Every input"));
        ui::tooltip(L("Tous les claviers MIDI branchés, et le clavier de l'ordinateur", "Every MIDI keyboard plugged in, and the computer keyboard"));
        pick(app::AudioTrackController::computerKeyboardInput, L("Clavier de l'ordinateur", "Computer keyboard"));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.textFaint));
        ImGui::SeparatorText(L("Claviers MIDI", "MIDI keyboards"));
        ImGui::PopStyleColor();
        ImGui::PopFont();
        if (midi_devices::inputs().isEmpty())
            ImGui::TextDisabled("%s", L("Aucun branché", "None plugged in"));
        for (const auto& device : midi_devices::inputs()) {
            ImGui::PushID(device.identifier.toRawUTF8());
            pick(device.identifier, device.name.toRawUTF8());
            ImGui::PopID();
        }
        ImGui::Separator();
        pick({}, L("Aucune", "None"));
        ui::tooltip(L("Elle n'entend aucun clavier : ses clips seulement", "It hears no keyboard: its clips only"));
        ImGui::EndCombo();
    }
    ui::tooltip(L("L'entrée MIDI qu'elle entend (armée, ou choisie) ; son carré s'allume quand une note arrive.",
                  "The MIDI input it hears (armed, or picked); its square lights up when a note comes in."));
    // Its light, over the combo's frame.
    const auto counter = current.isEmpty() ? engine.midiTrackClipActivity(track.midiId) : midiInputActivity(studio, track, current);
    const auto lit = midi_devices::activity("in" + std::to_string(track.midiId) + ":" + current.toStdString(), counter);
    const auto on = engine.midiTrackHearsInputs(track.midiId) ? p.play : p.textFaint;
    const ImVec2 lightMin(at.x + ImGui::GetStyle().FramePadding.x, at.y + std::round((height - light) * 0.5f));
    const ImVec2 lightMax(lightMin.x + light, lightMin.y + light);
    list->AddRectFilled(lightMin, lightMax, theme::mix(p.bg0 | 0xff000000u, on, 0.15f + 0.85f * lit), dp(2.0f));
    list->AddRect(lightMin, lightMax, theme::alpha(on, 0.7f), dp(2.0f));
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
    // A narrow combo (a launcher's cell): the input's name alone.
    const auto brief = width > 0.0f && width < dp(150.0f);
    for (const auto& bus : engine.virtualBuses())
        if (bus.id == current)
            shown = brief ? bus.name : label(bus);
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(width, dp(200.0f)), 0.0f), ImVec2(FLT_MAX, dp(420.0f)));
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

void hybridAudioInputCombo(Studio& studio, const TrackRef& owner, const TrackRef& audioPart, const float width)
{
    if (audioPart.midi || !studio.isValid(audioPart))
        return;
    auto& engine = studio.engine();
    const auto records = studio.recordsHybridAudio(owner);
    const auto current = engine.trackMixState(audioPart.audioIndex).inputBusId;
    const auto label = [](const app::VirtualAudioBus& bus) {
        return bus.name + "  (" + Studio::formatName(bus.channels) + ", " + deviceChannelsText(bus) + ")";
    };
    juce::String shown = ui::Ls("(aucune)", "(none)");
    const auto brief = width > 0.0f && width < dp(150.0f);
    if (records)
        for (const auto& bus : engine.virtualBuses())
            if (bus.id == current)
                shown = brief ? bus.name : label(bus);
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(std::max(width, dp(200.0f)), 0.0f), ImVec2(FLT_MAX, dp(420.0f)));
    if (ImGui::BeginCombo("##hybridAudioInput", shown.toRawUTF8())) {
        if (ImGui::Selectable(L("(aucune) : ses notes seules", "(none): its notes alone"), !records))
            studio.setRecordsHybridAudio(owner, false);
        for (const auto& bus : engine.virtualBuses()) {
            if (bus.direction != app::VirtualBusDirection::input)
                continue;
            ImGui::PushID(static_cast<int>(bus.id & 0x7fffffff));
            if (ImGui::Selectable(label(bus).toRawUTF8(), records && bus.id == current)) {
                if (bus.id != current)
                    (void) engine.setTrackInputBus(audioPart.audioIndex, bus.id);
                studio.setRecordsHybridAudio(owner, true);
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Ce que la piste enregistre en audio avec ses notes (piste hybride) : aucune entrée au départ ; choisissez-en une "
                  "pour enregistrer aussi le son (un micro, une guitare).",
                  "What the track records as audio with its notes (a hybrid track): no input at first; pick one to record the sound "
                  "too (a microphone, a guitar)."));
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
