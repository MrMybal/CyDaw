#include "GraphEditor.h"

#include "CardFiles.h"
#include "DeviceSignals.h"
#include "DeviceVisuals.h"

#include "GraphLayout.h"
#include "NodeTree.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

// The stored position of a card is a fraction of the v1 canvas; the editor
// works in world units of that canvas.
constexpr float kWorldX = graph_layout::worldX;
constexpr float kWorldY = graph_layout::worldY;
constexpr float kHeader = graph_layout::header;
constexpr float kPortRow = graph_layout::portRow;
constexpr float kKnobCellW = graph_layout::knobCellW;
constexpr float kKnobCellH = graph_layout::knobCellH;
constexpr float kSocket = 5.0f;
constexpr float kGridStep = 24.0f;

juce::String fold(const juce::String& text)
{
    auto out = text.toLowerCase();
    static const char* pairs[][2] = { { "é", "e" }, { "è", "e" }, { "ê", "e" }, { "ë", "e" }, { "à", "a" }, { "â", "a" },
                                      { "ä", "a" }, { "î", "i" }, { "ï", "i" }, { "ô", "o" }, { "ö", "o" }, { "ù", "u" },
                                      { "û", "u" }, { "ü", "u" }, { "ç", "c" } };
    for (const auto& pair : pairs)
        out = out.replace(juce::String::fromUTF8(pair[0]), pair[1]);
    return out;
}

ImU32 toImColour(const juce::Colour colour)
{
    return IM_COL32(colour.getRed(), colour.getGreen(), colour.getBlue(), colour.getAlpha());
}

std::string valueFormat(const double minimum, const double maximum)
{
    const auto range = std::abs(maximum - minimum);
    if (range >= 200.0)
        return "%.0f";
    if (range >= 20.0)
        return "%.1f";
    return "%.2f";
}

// A module's sockets are named after their ids ("input-37001"): the kind of
// signal says more. A value input takes the name of the knob it drives.
juce::String portLabel(const app::NodeGraphComponent::EditorPort& port, const app::NodeGraphComponent::EditorNode* node = nullptr)
{
    const auto name = port.name.trim();
    const auto lower = name.toLowerCase();
    if ((port.kind == 0 && (lower == "audio-in" || lower == "audio-out" || lower == "audio")) || lower == "input" || lower == "output"
        || lower == "mapped" || lower == "result" || lower == "out")
        return port.input ? ui::Ls("Entrée", "In") : ui::Ls("Sortie", "Out");
    if (port.kind == 2 && port.input && node != nullptr) {
        for (const auto& parameter : node->parameters)
            if (parameter.binding.endsWith("." + port.key))
                return parameter.label;
        if (port.key == "gain")
            for (const auto& parameter : node->parameters)
                if (parameter.binding.endsWith("gain.db"))
                    return parameter.label;
        if (port.key == "drive")
            for (const auto& parameter : node->parameters)
                if (parameter.binding.endsWith("drive.db"))
                    return parameter.label;
    }
    static const char* known[][3] = {
        { "gate", "Gate", "Gate" },           { "pitch", "Hauteur", "Pitch" },        { "velocity", "Vélocité", "Velocity" },
        { "envelope", "Enveloppe", "Envelope" }, { "reset", "Remise à zéro", "Reset" }, { "value", "Valeur", "Value" },
        { "sidechain", "Sidechain", "Sidechain" }, { "sidechain-in", "Sidechain", "Sidechain" },
        { "reduction", "Réduction", "Reduction" }, { "midi", "MIDI", "MIDI" },
        { "trigger", "Déclencheur", "Trigger" },  { "sum", "Somme", "Sum" },             { "level", "Niveau", "Level" },
    };
    for (const auto& entry : known)
        if (name == entry[0])
            return juce::String::fromUTF8(ui::french() ? entry[1] : entry[2]);
    const auto generated = (name.startsWith("input-") || name.startsWith("output-"))
                           && name.fromFirstOccurrenceOf("-", false, false).containsOnly("0123456789");
    if (!generated && name.isNotEmpty())
        return name;
    switch (port.kind) {
        case 0: return port.channels == 2 ? "Audio" : "Audio " + juce::String(port.channels);
        case 1: return "MIDI";
        case 2: return ui::Ls("Contrôle", "Control");
        default: return "Transport";
    }
}

bool isLogUnit(const juce::String& unit, const double minimum)
{
    return minimum > 0.0 && (unit == "Hz" || unit == "ms" || unit == "s");
}

} // namespace

GraphEditor::GraphEditor(Studio& studio, UiState& state, ImGuiHost& host, DeviceSignals& signals, const Mode mode)
    : studio_(studio), state_(state), host_(host), signals_(signals), mode_(mode) {}

ImU32 GraphEditor::kindColour(const int kind)
{
    const auto& p = theme::palette();
    switch (kind) {
        case 0: return p.audio;
        case 1: return p.midi;
        case 2: return p.control;
        case 3: return p.modulation;
        default: return p.textDim;
    }
}

bool GraphEditor::simulatedHere(const GraphSlot& slot)
{
    simulated_ = inWorkshop() && studio_.simulatorActive() && &slot == studio_.workshopRoot();
    return simulated_;
}

float GraphEditor::scale() const
{
    return (view_ != nullptr ? view_->zoom : 1.0f) * ImGui::GetStyle().FontScaleDpi;
}

ImVec2 GraphEditor::worldToScreen(const ImVec2 world) const
{
    const auto s = scale();
    return ImVec2(canvasMin_.x + (world.x - view_->scroll.x) * s, canvasMin_.y + (world.y - view_->scroll.y) * s);
}

ImVec2 GraphEditor::screenToWorld(const ImVec2 screen) const
{
    const auto s = scale();
    return ImVec2(view_->scroll.x + (screen.x - canvasMin_.x) / s, view_->scroll.y + (screen.y - canvasMin_.y) / s);
}

ImVec2 GraphEditor::fractionToWorld(const juce::Point<float> fraction)
{
    return ImVec2(fraction.x * kWorldX, fraction.y * kWorldY);
}

juce::Point<float> GraphEditor::worldToFraction(const ImVec2 world)
{
    return { world.x / kWorldX, world.y / kWorldY };
}

// ---- Frame -----------------------------------------------------------------------------

void GraphEditor::draw()
{
    nodeMenuId_ = ImGui::GetID("##nodeMenu");
    searchMenuId_ = ImGui::GetID("##nodeSearch");
    deviceMenuId_ = ImGui::GetID("##newDevice");
    ioMenuId_ = ImGui::GetID("##ioMenu");
    cableMenuId_ = ImGui::GetID("##cableMenu");
    std::optional<TrackRef> track;
    GraphSlot* slot = nullptr;
    if (inWorkshop()) {
        slot = studio_.workshopGraph();
        if (slot == nullptr) {
            ui::emptyState(L("Aucun périphérique ouvert", "No device open"),
                           L("Créez un effet ou un instrument, ou ouvrez un de vos périphériques.",
                             "Make an effect or an instrument, or open one of your devices."));
            return;
        }
    } else {
        if (!state_.selectedTrack) {
            ui::emptyState(L("Aucune piste sélectionnée", "No track selected"),
                           L("Sélectionnez une piste pour voir et éditer son graphe.", "Select a track to see and edit its graph."));
            return;
        }
        track = *state_.selectedTrack;
        slot = trackSlot(*track);
        if (slot == nullptr) {
            ui::emptyState(L("Pas de graphe", "No graph"), "");
            return;
        }
    }
    if (currentGraph_ != slot) {
        currentGraph_ = slot;
        selection_.clear();
        selectedWire_.reset();
        wire_.reset();
        renaming_.reset();
        // A device opened gets a view of its own, framed afresh (its slot
        // may reuse the address of one closed before).
        if (slot->owner() == GraphSlot::Owner::module || slot->owner() == GraphSlot::Owner::workshop)
            views_[slot] = {};
    }
    view_ = &views_[slot];
    drawToolbar(*slot, track);
    drawCanvas(*slot, track);
    if (const auto popup = std::exchange(pendingPopup_, ImGuiID {}); popup != 0)
        ImGui::OpenPopup(popup);
    searchPopup(*slot, track);
    nodeContextMenu(*slot);
    newDeviceMenu(*slot, track);
    ioMenu(*slot);
    cableMenu(*slot);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        handleKeys(*slot);
    // A device made of a graph opens after the frame that asked for it: in
    // place inside the workshop, in the workshop from a track.
    if (const auto node = std::exchange(pendingEnter_, std::nullopt)) {
        if (inWorkshop()) {
            if (const auto entered = studio_.enterWorkshopDevice(*node); entered.failed()) {
                lastError_ = entered.getErrorMessage();
                lastErrorTime_ = ImGui::GetTime();
            }
        } else if (track) {
            state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::trackDevice, *track, *node, {} };
            state_.bringToFront = Panel::workshop;
        }
    }
}

GraphSlot* GraphEditor::trackSlot(const TrackRef& track)
{
    showingModulation_ = false;
    if (state_.graphShowsModulation)
        if (auto* modulation = studio_.modulationGraphFor(track)) {
            showingModulation_ = true;
            return modulation;
        }
    return studio_.graphFor(track);
}

void GraphEditor::addNode(const juce::String& typeKey)
{
    auto* slot = inWorkshop() ? studio_.workshopGraph() : (state_.selectedTrack ? trackSlot(*state_.selectedTrack) : nullptr);
    if (slot == nullptr || view_ == nullptr || currentGraph_ != slot)
        return;
    // Near the middle of the view, where no card is: tried along a spiral
    // of steps a card wide, the middle itself when the view is full.
    const auto zoom = view_->zoom;
    const ImVec2 size(dp(220.0f) * zoom, dp(190.0f) * zoom);
    const ImVec2 middle((canvasMin_.x + canvasMax_.x) * 0.5f - size.x * 0.5f, (canvasMin_.y + canvasMax_.y) * 0.5f - size.y * 0.5f);
    const auto free = [this, &size](const ImVec2 at) {
        if (at.x < canvasMin_.x || at.y < canvasMin_.y || at.x + size.x > canvasMax_.x || at.y + size.y > canvasMax_.y)
            return false;
        for (const auto& [min, max] : cardRects_)
            if (at.x < max.x + 12.0f && at.x + size.x > min.x - 12.0f && at.y < max.y + 12.0f && at.y + size.y > min.y - 12.0f)
                return false;
        return true;
    };
    auto at = middle;
    auto found = free(at);
    for (auto ring = 1; ring < 8 && !found; ++ring)
        for (auto dx = -ring; dx <= ring && !found; ++dx)
            for (auto dy = -ring; dy <= ring && !found; ++dy) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring)
                    continue;
                const ImVec2 candidate(middle.x + static_cast<float>(dx) * size.x * 0.6f, middle.y + static_cast<float>(dy) * size.y * 0.6f);
                if (free(candidate)) {
                    at = candidate;
                    found = true;
                }
            }
    if (!found) {
        const auto step = dp(28.0f) * static_cast<float>(addedInPlace_++ % 6);
        at = ImVec2(middle.x + step, middle.y + step);
    }
    if (const auto id = slot->graph().editorAddNode(typeKey, worldToFraction(screenToWorld(at))); id != 0) {
        selection_ = { id };
        cardRects_.push_back({ at, ImVec2(at.x + size.x, at.y + size.y) });
    }
}

void GraphEditor::addBoundary(GraphSlot& slot, const juce::String& typeKey, const bool input)
{
    // At the edge of what is there: inputs on the left, outputs on the
    // right, under those of their side.
    auto& graph = slot.graph();
    auto left = 1.0f;
    auto right = 0.0f;
    auto lowest = 0.0f;
    auto found = false;
    for (const auto& node : graph.editorNodes()) {
        left = std::min(left, node.position.x);
        right = std::max(right, node.position.x);
        const auto sideMatches = input ? (node.typeKey.contains("-input") || node.typeKey == "midi-input")
                                       : (node.typeKey.contains("-output") || node.typeKey == "master");
        if (node.boundary && sideMatches) {
            lowest = std::max(lowest, node.position.y);
            found = true;
        }
    }
    const juce::Point<float> at(input ? std::max(0.0f, left - (found ? 0.0f : 0.12f)) : std::min(0.95f, right + (found ? 0.0f : 0.12f)),
                                found ? std::min(0.92f, lowest + 0.16f) : 0.3f);
    if (const auto id = graph.editorAddNode(typeKey, at); id != 0)
        selection_ = { id };
    else {
        lastError_ = ui::Ls("Cette entrée ou sortie n'a pas pu être ajoutée", "This input or output could not be added");
        lastErrorTime_ = ImGui::GetTime();
    }
}

std::optional<GraphEditor::EditorConnection> GraphEditor::wireAt(const std::vector<EditorConnection>& connections,
                                                                 const std::vector<NodeLayout>& layouts,
                                                                 const ImVec2 mouse) const
{
    const auto s = scale();
    for (const auto& connection : connections) {
        ImVec2 a {};
        ImVec2 b {};
        auto found = 0;
        for (const auto& layout : layouts) {
            if (layout.node->id == connection.source)
                for (const auto& [port, centre] : layout.outputs)
                    if (port->id == connection.sourcePort) {
                        a = centre;
                        ++found;
                    }
            if (layout.node->id == connection.destination)
                for (const auto& [port, centre] : layout.inputs)
                    if (port->id == connection.destinationPort) {
                        b = centre;
                        ++found;
                    }
        }
        if (found < 2)
            continue;
        const auto dx = std::max(50.0f * s, std::abs(b.x - a.x) * 0.5f);
        const auto closest = ImBezierCubicClosestPointCasteljau(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, mouse,
                                                                 ImGui::GetStyle().CurveTessellationMaxError);
        if (ImLengthSqr(ImVec2(closest.x - mouse.x, closest.y - mouse.y)) < dp(7.0f) * dp(7.0f))
            return connection;
    }
    return std::nullopt;
}

void GraphEditor::cableMenu(GraphSlot& slot)
{
    if (!ImGui::BeginPopup("##cableMenu"))
        return;
    auto& graph = slot.graph();
    // The cable as it is now: the menu outlives a change made from it.
    std::optional<EditorConnection> wire;
    if (cableMenuWire_)
        for (const auto& connection : graph.editorConnections())
            if (connection.source == cableMenuWire_->source && connection.destination == cableMenuWire_->destination
                && connection.sourcePort == cableMenuWire_->sourcePort
                && connection.destinationPort == cableMenuWire_->destinationPort)
                wire = connection;
    if (!wire) {
        cableMenuWire_.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    const auto setMode = [&graph, &wire](const int mode, const float amount) {
        (void) graph.editorSetConnectionMode(wire->source, wire->sourcePort, wire->destination, wire->destinationPort, mode,
                                             amount);
    };
    if (wire->drivesParameter) {
        ImGui::TextDisabled("%s", L("Ce câble pilote un bouton", "This cable drives a knob"));
        ImGui::Separator();
        if (ImGui::MenuItem(L("Régler la valeur", "Set the value"), nullptr, wire->mode == 0))
            setMode(0, wire->amount);
        ui::tooltip(L("Le bouton prend la valeur du câble.", "The knob takes the cable's value."));
        if (ImGui::MenuItem(L("Moduler autour de la valeur", "Modulate around the value"), nullptr, wire->mode == 1))
            setMode(1, wire->amount);
        ui::tooltip(L("Le bouton garde sa valeur ; le câble le fait bouger autour, comme un LFO.",
                      "The knob keeps its value; the cable moves it around it, like an LFO."));
        if (wire->mode == 1) {
            ImGui::TextUnformatted(L("Profondeur", "Depth"));
            ImGui::SetNextItemWidth(dp(190.0f));
            auto percent = cableAmount_ * 100.0f;
            if (ImGui::SliderFloat("##depth", &percent, -100.0f, 100.0f, "%.0f %%"))
                cableAmount_ = percent / 100.0f;
            // One step of history per gesture, taken when it ends.
            if (ImGui::IsItemDeactivatedAfterEdit())
                setMode(1, cableAmount_);
            ui::tooltip(L("La part de la course du bouton parcourue de chaque côté de sa valeur. Négative : inversée.",
                          "How much of the knob's travel the cable covers on each side of its value. Negative: inverted."));
        }
        ImGui::Separator();
    }
    if (ImGui::MenuItem(L("Supprimer le câble", "Delete the cable"))) {
        (void) graph.editorDisconnect(wire->source, wire->sourcePort, wire->destination, wire->destinationPort);
        selectedWire_.reset();
        cableMenuWire_.reset();
    }
    ImGui::EndPopup();
}

void GraphEditor::ioMenu(GraphSlot& slot)
{
    if (!ImGui::BeginPopup("##ioMenu"))
        return;
    ImGui::TextDisabled("%s", L("Entrées et sorties du périphérique", "The device's inputs and outputs"));
    ImGui::Separator();
    const auto widths = [&](const bool input) {
        const auto key = [input](const int channels) {
            return juce::String(input ? "graph-audio-input-" : "graph-audio-output-") + juce::String(channels);
        };
        if (ImGui::MenuItem("Mono"))
            addBoundary(slot, key(1), input);
        if (ImGui::MenuItem(L("Stéréo", "Stereo")))
            addBoundary(slot, key(2), input);
        for (const auto channels : { 3, 4, 5, 6, 8 })
            if (ImGui::MenuItem((juce::String(channels) + ui::Ls(" canaux", " channels")).toRawUTF8()))
                addBoundary(slot, key(channels), input);
    };
    if (ImGui::BeginMenu(L("Entrée audio", "Audio input"))) {
        widths(true);
        ImGui::EndMenu();
    }
    ui::tooltip(L("Une de plus pour un sidechain, ou plusieurs sons à mélanger.", "One more for a sidechain, or several sounds to mix."));
    if (ImGui::BeginMenu(L("Sortie audio", "Audio output"))) {
        widths(false);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Entrée MIDI", "MIDI input")))
        addBoundary(slot, "graph-midi-input", true);
    if (ImGui::MenuItem(L("Sortie MIDI", "MIDI output")))
        addBoundary(slot, "graph-midi-output", false);
    ImGui::Separator();
    if (ImGui::MenuItem(L("Entrée de contrôle (valeur, LFO…)", "Control input (value, LFO…)")))
        addBoundary(slot, "graph-value-input", true);
    ui::tooltip(L("Une prise sur la carte du périphérique, où brancher un LFO ou une macro de l'extérieur.",
                  "A socket on the device's card, to plug an LFO or a macro in from outside."));
    if (ImGui::MenuItem(L("Sortie de contrôle", "Control output")))
        addBoundary(slot, "graph-value-output", false);
    ImGui::EndPopup();
}

void GraphEditor::groupSelection(GraphSlot& slot)
{
    if (selection_.empty())
        return;
    juce::String error;
    const auto card = studio_.createDeviceFromSelection(slot, { selection_.begin(), selection_.end() }, {}, error);
    if (card == 0) {
        lastError_ = error;
        lastErrorTime_ = ImGui::GetTime();
    } else {
        selection_ = { card };
    }
}

void GraphEditor::newDeviceMenu(GraphSlot& slot, const std::optional<TrackRef>& track)
{
    if (!ImGui::BeginPopup("##newDevice"))
        return;
    ImGui::TextDisabled("%s", L("Périphériques faits d'un graphe", "Devices made of a graph"));
    ImGui::Separator();
    ImGui::BeginDisabled(selection_.empty());
    if (ImGui::MenuItem(L("Réunir les cartes choisies en un périphérique", "Make one device of the cards picked"), "Ctrl+G"))
        groupSelection(slot);
    ImGui::EndDisabled();
    if (!inWorkshop() && track) {
        if (track->midi && ImGui::MenuItem(L("Éditer l'instrument dans l'atelier", "Edit the instrument in the workshop"))) {
            state_.workshopRequest = WorkshopRequest { WorkshopRequest::Kind::trackDevice, *track, 0, {} };
            state_.bringToFront = Panel::workshop;
        }
        if (ImGui::MenuItem(L("Ouvrir l'atelier de périphériques", "Open the device workshop")))
            state_.bringToFront = Panel::workshop;
    }
    // Effects of My devices go in here; an instrument takes a whole track.
    const auto devices = studio_.libraryDevices();
    if (!devices.empty() && track) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", L("Mes périphériques", "My devices"));
        for (const auto& device : devices) {
            ImGui::BeginDisabled(device.instrument && !track->midi);
            if (ImGui::MenuItem(device.name.toRawUTF8(), device.instrument ? L("remplace l'instrument", "replaces the instrument") : nullptr)) {
                if (const auto inserted = studio_.insertLibraryDevice(*track, device.file); inserted.failed()) {
                    lastError_ = inserted.getErrorMessage();
                    lastErrorTime_ = ImGui::GetTime();
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered() && device.description.isNotEmpty())
                ImGui::SetTooltip("%s", device.description.toRawUTF8());
        }
    }
    ImGui::EndPopup();
}

void GraphEditor::drawToolbar(GraphSlot& slot, const std::optional<TrackRef>& track)
{
    const auto& p = theme::palette();
    auto& graph = slot.graph();
    const auto height = ImGui::GetFrameHeight();
    if (track) {
        const auto chip = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(chip.x, chip.y + height * 0.2f), ImVec2(chip.x + dp(4.0f), chip.y + height * 0.8f),
                                                  theme::fromArgb(studio_.trackColour(*track)), dp(2.0f));
        ImGui::Dummy(ImVec2(dp(6.0f), height));
        ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::TextUnformatted(studio_.trackName(*track).toRawUTF8());
        ImGui::PopFont();
        ImGui::SameLine();
        if (track->midi || track->master) {
            ImGui::TextDisabled("%s", track->midi ? L("instrument", "instrument") : L("effets audio", "audio effects"));
        } else {
            // Its sound, or the value graph that moves its parameters.
            auto sound = !showingModulation_;
            if (ui::toggle(L("Son", "Sound"), &sound, theme::palette().accent))
                state_.graphShowsModulation = false;
            ui::tooltip(L("Le graphe du son de la piste : ses effets", "The track's sound graph: its effects"));
            ImGui::SameLine(0.0f, dp(2.0f));
            auto modulation = showingModulation_;
            if (ui::toggle(L("Modulation", "Modulation"), &modulation, theme::palette().modulation))
                state_.graphShowsModulation = true;
            ui::tooltip(L("Le graphe de modulation de la piste : LFO, macros, calculs et logique dont la sortie module les "
                          "paramètres choisis dans l'Inspecteur (Modulation)",
                          "The track's modulation graph: LFOs, macros, arithmetic and logic whose output modulates the "
                          "parameters picked in the Inspector (Modulation)"));
            if (showingModulation_) {
                ImGui::SameLine();
                const auto output = studio_.modulationOutput(*track);
                ImGui::TextDisabled("%s %+.2f", L("sortie", "output"), output);
            }
        }
    } else {
        // In the workshop: where in the device, and the way back out.
        const auto trail = studio_.workshop().trail;
        ImGui::BeginDisabled(trail.size() < 2);
        if (ui::iconButton(icon::back, L("Revenir au niveau du dessus (Échap)", "Back one level (Esc)")))
            studio_.leaveWorkshopDevice();
        ImGui::EndDisabled();
        for (std::size_t level = 0; level < trail.size(); ++level) {
            ImGui::SameLine(0.0f, dp(4.0f));
            if (level > 0) {
                ImGui::TextDisabled("%s", icon::chevronRight);
                ImGui::SameLine(0.0f, dp(4.0f));
            }
            ImGui::PushID(static_cast<int>(level));
            const auto name = trail[level].isNotEmpty() ? trail[level] : ui::Ls("Périphérique", "Device");
            if (level + 1 == trail.size()) {
                ImGui::AlignTextToFramePadding();
                ImGui::PushFont(theme::boldFont(), 0.0f);
                ImGui::TextUnformatted(name.toRawUTF8());
                ImGui::PopFont();
            } else if (ImGui::SmallButton(name.toRawUTF8())) {
                for (auto drop = trail.size() - level - 1; drop > 0; --drop)
                    studio_.leaveWorkshopDevice();
            }
            ImGui::PopID();
        }
    }

    ui::toolbarSeparator();
    if (ui::iconButton(icon::add, L("Ajouter un nœud (Tab, double-clic ou clic droit)", "Add a node (Tab, double click or right click)")))
        openSearch(ImVec2((canvasMin_.x + canvasMax_.x) * 0.5f, (canvasMin_.y + canvasMax_.y) * 0.4f), std::nullopt);
    if (!showingModulation_) {
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ui::iconButton(icon::library, L("Périphériques faits d'un graphe : réunir des cartes, Mes périphériques, l'atelier",
                                            "Devices made of a graph: group cards, My devices, the workshop")))
            ImGui::OpenPopup(deviceMenuId_);
    }
    if (inWorkshop()) {
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ui::iconButton(icon::connect, L("Entrées et sorties : audio (mono, stéréo, plusieurs), MIDI, contrôle",
                                            "Inputs and outputs: audio (mono, stereo, several), MIDI, control")))
            ImGui::OpenPopup(ioMenuId_);
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::grid, L("Ranger selon le signal (A)", "Arrange by signal flow (A)"))) {
        graph_layout::arrange(graph);
        view_->framed = false;
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::fullScreen, L("Tout voir (F)", "Frame all (F)"))) {
        view_->framed = false;
        view_->touched = false;
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    ImGui::BeginDisabled(!graph.editorCanUndo());
    if (ui::iconButton(icon::undo, L("Annuler (Ctrl+Z)", "Undo (Ctrl+Z)")))
        (void) graph.editorUndo();
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, dp(2.0f));
    ImGui::BeginDisabled(!graph.editorCanRedo());
    if (ui::iconButton(icon::redo, L("Rétablir (Ctrl+Y)", "Redo (Ctrl+Y)")))
        (void) graph.editorRedo();
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::view, L("Mini-carte", "Minimap"), showMinimap_))
        showMinimap_ = !showMinimap_;
    ImGui::SameLine();
    ImGui::TextDisabled("%d %%", static_cast<int>(std::round(view_->zoom * 100.0f)));

    // Compilation state, on the right.
    const auto error = slot.lastCompileError();
    const auto message = error.isNotEmpty() ? error
                         : lastError_.isNotEmpty() && ImGui::GetTime() - lastErrorTime_ < 5.0 ? lastError_
                                                                                               : juce::String();
    const auto text = message.isNotEmpty() ? message
                      : showingModulation_ ? ui::Ls("Modulation : sa sortie bouge les paramètres choisis",
                                                     "Modulation: its output moves the parameters picked")
                                           : ui::Ls("Graphe compilé", "Graph compiled");
    const auto width = std::min(dp(360.0f), ImGui::CalcTextSize(text.toRawUTF8()).x + dp(24.0f));
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + dp(8.0f), ImGui::GetWindowWidth() - width - ImGui::GetStyle().WindowPadding.x));
    const auto origin = ImGui::GetCursorScreenPos();
    const auto colour = message.isNotEmpty() ? p.record : p.play;
    auto* list = ImGui::GetWindowDrawList();
    list->AddCircleFilled(ImVec2(origin.x + dp(6.0f), origin.y + height * 0.5f), dp(3.5f), colour);
    ui::clippedText(list, ImVec2(origin.x + dp(14.0f), origin.y + (height - ImGui::GetFontSize()) * 0.5f), width - dp(16.0f),
                    message.isNotEmpty() ? p.record : p.textDim, text.toRawUTF8());
    ImGui::Dummy(ImVec2(width, height));
    if (ImGui::IsItemHovered() && message.isNotEmpty())
        ImGui::SetTooltip("%s", message.toRawUTF8());
}

void GraphEditor::layoutNodes(const std::vector<EditorNode>& nodes, const std::vector<EditorConnection>& connections,
                              std::vector<NodeLayout>& out) const
{
    const auto s = scale();
    out.clear();
    out.reserve(nodes.size());
    // Value inputs nothing feeds stay out of the way, except on the card in
    // hand and while a control cable looks for somewhere to land.
    const auto drawingControl = (wire_ && wire_->kind == 2 && wire_->fromOutput) || (searchWire_ && searchWire_->kind == 2);
    for (const auto& node : nodes) {
        NodeLayout layout;
        layout.node = &node;
        const auto world = fractionToWorld(node.position);
        layout.min = worldToScreen(world);
        layout.headerHeight = kHeader * s;
        const auto showAll = drawingControl || selection_.contains(node.id);
        std::vector<const EditorPort*> shown;
        for (const auto& port : node.ports) {
            auto visible = !(port.input && port.kind == 2) || showAll;
            if (!visible)
                for (const auto& connection : connections)
                    if (connection.destination == node.id && connection.destinationPort == port.id)
                        visible = true;
            if (visible)
                shown.push_back(&port);
        }
        auto inputIndex = 0;
        auto outputIndex = 0;
        auto rows = 0;
        for (const auto* port : shown)
            rows = std::max(rows, port->input ? ++inputIndex : ++outputIndex);
        layout.max = ImVec2(layout.min.x + graph_layout::width(node) * s, layout.min.y + graph_layout::height(node, rows) * s);
        layout.bodyTop = layout.min.y + (kHeader + (rows > 0 ? rows * kPortRow + 8.0f : 0.0f)) * s;
        inputIndex = 0;
        outputIndex = 0;
        for (const auto* shownPort : shown) {
            const auto& port = *shownPort;
            const auto row = static_cast<float>(port.input ? inputIndex++ : outputIndex++);
            auto y = layout.min.y + (kHeader + 4.0f + (row + 0.5f) * kPortRow) * s;
            if (node.collapsed)
                y = layout.min.y + layout.headerHeight * 0.5f + row * 3.0f * s;
            if (port.input)
                layout.inputs.push_back({ &port, ImVec2(layout.min.x, y) });
            else
                layout.outputs.push_back({ &port, ImVec2(layout.max.x, y) });
        }
        out.push_back(std::move(layout));
    }
}

void GraphEditor::drawCanvas(GraphSlot& slot, const std::optional<TrackRef>& track)
{
    const auto& p = theme::palette();
    auto& graph = slot.graph();
    auto& io = ImGui::GetIO();
    juce::ignoreUnused(track);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg0);
    const auto open = ImGui::BeginChild("##graphCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                                            | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return;
    }
    canvasMin_ = ImGui::GetCursorScreenPos();
    const auto size = ImGui::GetContentRegionAvail();
    canvasMax_ = ImVec2(canvasMin_.x + size.x, canvasMin_.y + size.y);
    auto* list = ImGui::GetWindowDrawList();

    auto nodes = graph.editorNodes();
    if (!view_->tidied && size.x > 50.0f && size.y > 50.0f) {
        view_->tidied = true;
        const auto fit = frameAll(nodes, false);
        // Cards drawn over one another (a layout made for smaller cards).
        auto overlapping = false;
        for (std::size_t i = 0; i < nodes.size() && !overlapping; ++i)
            for (std::size_t j = i + 1; j < nodes.size() && !overlapping; ++j) {
                const auto a = fractionToWorld(nodes[i].position);
                const auto b = fractionToWorld(nodes[j].position);
                const auto overlapX = std::min(a.x + graph_layout::width(nodes[i]), b.x + graph_layout::width(nodes[j])) - std::max(a.x, b.x);
                const auto overlapY = std::min(a.y + graph_layout::height(nodes[i]), b.y + graph_layout::height(nodes[j])) - std::max(a.y, b.y);
                overlapping = overlapX > 12.0f && overlapY > 12.0f;
            }
        // CyDAW 1 stored positions against a canvas of any width: spread
        // that far, the cards would be unreadable. Ctrl+Z restores them.
        if ((fit < 0.55f || overlapping) && nodes.size() > 1) {
            graph_layout::arrange(graph);
            nodes = graph.editorNodes();
            view_->framed = false;
        }
    }
    const auto connections = graph.editorConnections();
    const auto sizeChanged = std::abs(size.x - view_->framedSize.x) > 2.0f || std::abs(size.y - view_->framedSize.y) > 2.0f;
    if ((!view_->framed || (!view_->touched && sizeChanged)) && size.x > 50.0f && size.y > 50.0f) {
        frameAll(nodes, false);
        view_->framed = true;
        view_->framedSize = size;
    }

    // Background: grid, then the canvas's own input under everything.
    const auto s = scale();
    {
        // A field of dots, the stronger ones every fourth step.
        auto step = kGridStep * s;
        while (step < dp(9.0f))
            step *= 2.0f;
        const auto worldStep = step / s;
        const auto startX = canvasMin_.x - std::fmod(view_->scroll.x * s, step) - (view_->scroll.x < 0.0f ? -step : 0.0f);
        const auto startY = canvasMin_.y - std::fmod(view_->scroll.y * s, step) - (view_->scroll.y < 0.0f ? -step : 0.0f);
        const auto dot = theme::alpha(p.text, 0.07f);
        const auto strongDot = theme::alpha(p.text, 0.16f);
        for (auto x = startX; x < canvasMax_.x; x += step) {
            const auto column = static_cast<long long>(std::llround(screenToWorld(ImVec2(x, 0.0f)).x / worldStep));
            for (auto y = startY; y < canvasMax_.y; y += step) {
                const auto row = static_cast<long long>(std::llround(screenToWorld(ImVec2(0.0f, y)).y / worldStep));
                const auto strong = column % 4 == 0 && row % 4 == 0;
                list->AddRectFilled(ImVec2(x - 0.75f, y - 0.75f), ImVec2(x + 0.75f + (strong ? 0.6f : 0.0f), y + 0.75f + (strong ? 0.6f : 0.0f)),
                                    strong ? strongDot : dot);
            }
        }
    }
    ImGui::SetCursorScreenPos(canvasMin_);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvas", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const auto canvasHovered = ImGui::IsItemHovered();
    const auto canvasActive = ImGui::IsItemActive();
    const auto mouse = io.MousePos;
    // A node type dragged from the workshop's palette lands where it drops.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(nodeTypePayload); payload != nullptr && payload->DataSize > 0) {
            const auto key = juce::String::fromUTF8(static_cast<const char*>(payload->Data), payload->DataSize - 1);
            if (const auto id = slot.graph().editorAddNode(key, worldToFraction(screenToWorld(mouse))); id != 0)
                selection_ = { id };
        }
        ImGui::EndDragDropTarget();
    }

    // Zoom around the pointer; pan with the middle button or Space.
    // The wheel zooms anywhere on the canvas, except over a knob, where it
    // turns the knob.
    const auto wheelOverKnob = knobHovered_;
    knobHovered_ = false;
    if (ImGui::IsWindowHovered() && !wheelOverKnob && io.MouseWheel != 0.0f) {
        const auto before = screenToWorld(mouse);
        view_->zoom = std::clamp(view_->zoom * (io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.25f, 2.5f);
        view_->touched = true;
        const auto after = screenToWorld(mouse);
        view_->scroll.x += before.x - after.x;
        view_->scroll.y += before.y - after.y;
    }
    // The hand: a left drag begun on the empty canvas, a right drag (a
    // right click that does not move still opens the search), the middle
    // button, or Space held.
    if (grabbing_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        grabbing_ = false;
    const auto spacePan = ImGui::IsKeyDown(ImGuiKey_Space) && canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const auto rightPan = canvasActive && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 4.0f);
    const auto grabPan = grabbing_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f);
    if ((canvasActive && ImGui::IsMouseDown(ImGuiMouseButton_Middle)) || spacePan || rightPan || grabPan) {
        view_->scroll.x -= io.MouseDelta.x / s;
        view_->scroll.y -= io.MouseDelta.y / s;
        panning_ = true;
        view_->touched = true;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else {
        panning_ = false;
    }

    // What plays through the cards: a track's, not the workshop's (its
    // cards play nowhere until the device is on a track).
    if (track && !showingModulation_)
        signals_.update(*track, slot);
    else if (simulatedHere(slot))
        signals_.updateSimulation(slot);
    std::vector<NodeLayout> layouts;
    layoutNodes(nodes, connections, layouts);
    cardRects_.clear();
    for (const auto& layout : layouts)
        cardRects_.push_back({ layout.min, layout.max });

    // Wire hover and selection, before the nodes take the pointer.
    drawComments(graph, list);
    drawWires(list, connections, layouts);

    // Cards, the selected ones on top.
    std::vector<const NodeLayout*> order;
    for (const auto& layout : layouts)
        if (!selection_.contains(layout.node->id))
            order.push_back(&layout);
    for (const auto& layout : layouts)
        if (selection_.contains(layout.node->id))
            order.push_back(&layout);
    std::optional<std::pair<std::uint64_t, std::uint64_t>> hoveredPort; // node, port
    for (const auto* layout : order) {
        if (layout->max.x < canvasMin_.x - 40.0f || layout->min.x > canvasMax_.x + 40.0f
            || layout->max.y < canvasMin_.y - 40.0f || layout->min.y > canvasMax_.y + 40.0f) {
            continue;
        }
        drawNode(graph, *layout, list);
    }

    // A lone card dragged over a cable joins it there.
    if (movingNodes_ && selection_.size() == 1)
        findSpliceTarget(connections, layouts);
    else if (!droppedNode_)
        spliceTarget_.reset();
    if (droppedNode_) {
        findSpliceTarget(connections, layouts);
        splice(graph, layouts);
        droppedNode_.reset();
        spliceTarget_.reset();
    }

    // Sockets: hover target for a cable being drawn, start of a new one.
    for (const auto& layout : layouts) {
        ImGui::PushID(static_cast<int>(layout.node->id));
        const auto drawSockets = [&](const auto& sockets, const bool inputs) {
            for (const auto& [port, centre] : sockets) {
                const auto radius = std::max(3.0f, kSocket * s);
                const auto hit = std::max(radius * 2.2f, dp(9.0f));
                ImGui::SetCursorScreenPos(ImVec2(centre.x - hit, centre.y - hit));
                ImGui::PushID(static_cast<int>(port->id) * 2 + (inputs ? 1 : 0));
                ImGui::InvisibleButton("##socket", ImVec2(hit * 2.0f, hit * 2.0f));
                const auto hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                const auto colour = kindColour(port->kind);
                auto connected = false;
                for (const auto& connection : connections)
                    if ((inputs && connection.destination == layout.node->id && connection.destinationPort == port->id)
                        || (!inputs && connection.source == layout.node->id && connection.sourcePort == port->id))
                        connected = true;
                const auto compatible = wire_.has_value() && wire_->fromOutput == inputs && wire_->kind == port->kind
                                        && wire_->node != layout.node->id;
                if (hovered && wire_ && compatible)
                    hoveredPort = std::make_pair(layout.node->id, port->id);
                const auto big = hovered || compatible;
                list->AddCircleFilled(centre, radius + (big ? 1.5f : 0.0f), connected || big ? colour : p.bg0, 16);
                list->AddCircle(centre, radius + (big ? 1.5f : 0.0f), colour, 16, std::max(1.0f, 1.4f * s));
                if (port->channels > 1 && port->kind == 0)
                    list->AddCircleFilled(centre, radius * 0.38f, p.bg0, 10);
                if (ImGui::IsItemActivated() && !wire_) {
                    selectedWire_.reset();
                    if (!inputs) {
                        wire_ = PendingWire { layout.node->id, port->id, true, port->kind, centre };
                    } else {
                        // Grabbing a fed input picks its cable up from the source.
                        std::optional<EditorConnection> existing;
                        for (const auto& connection : connections)
                            if (connection.destination == layout.node->id && connection.destinationPort == port->id)
                                existing = connection;
                        if (existing) {
                            graph.editorDisconnect(existing->source, existing->sourcePort, existing->destination,
                                                   existing->destinationPort);
                            ImVec2 anchor = centre;
                            for (const auto& other : layouts)
                                if (other.node->id == existing->source)
                                    for (const auto& [otherPort, otherCentre] : other.outputs)
                                        if (otherPort->id == existing->sourcePort)
                                            anchor = otherCentre;
                            wire_ = PendingWire { existing->source, existing->sourcePort, true, port->kind, anchor };
                        } else {
                            wire_ = PendingWire { layout.node->id, port->id, false, port->kind, centre };
                        }
                    }
                }
                if (hovered && !wire_) {
                    ImGui::BeginTooltip();
                    ImGui::TextColored(theme::vec(colour), "%s", portLabel(*port, layout.node).toRawUTF8());
                    const char* kinds[] = { "audio", "MIDI", L("contrôle", "control"), "transport" };
                    ImGui::TextDisabled("%s%s", kinds[std::clamp(port->kind, 0, 3)],
                                        port->kind == 0 && port->channels > 1 ? (port->channels == 2 ? L(" stéréo", " stereo") : "")
                                                                               : "");
                    ImGui::EndTooltip();
                }
                // Labels inside the card.
                if (s > 0.55f && !layout.node->collapsed) {
                    ImGui::PushFont(nullptr, ui::currentFontBase() * std::clamp(view_->zoom, 0.6f, 1.4f) * 0.8f);
                    const auto label = portLabel(*port, layout.node);
                    const auto textSize = ImGui::CalcTextSize(label.toRawUTF8());
                    const auto x = inputs ? centre.x + radius + dp(4.0f) * view_->zoom
                                          : centre.x - radius - dp(4.0f) * view_->zoom - textSize.x;
                    list->AddText(ImVec2(x, centre.y - textSize.y * 0.5f), p.textDim, label.toRawUTF8());
                    ImGui::PopFont();
                }
                ImGui::PopID();
            }
        };
        drawSockets(layout.inputs, true);
        drawSockets(layout.outputs, false);
        ImGui::PopID();
    }

    // A cable in hand is caught by the nearest socket that takes it, within
    // a reach, or by the card it is over: a free socket of it, else the first.
    if (wire_ && !hoveredPort) {
        auto best = dp(24.0f) * std::max(0.7f, view_->zoom);
        best *= best;
        std::optional<std::pair<std::uint64_t, std::uint64_t>> onCard;
        for (const auto& layout : layouts) {
            if (layout.node->id == wire_->node)
                continue;
            const auto inside = mouse.x >= layout.min.x && mouse.x <= layout.max.x && mouse.y >= layout.min.y && mouse.y <= layout.max.y;
            std::optional<std::pair<std::uint64_t, std::uint64_t>> first;
            std::optional<std::pair<std::uint64_t, std::uint64_t>> free;
            for (const auto& [port, centre] : wire_->fromOutput ? layout.inputs : layout.outputs) {
                if (port->kind != wire_->kind)
                    continue;
                const auto distance = ImLengthSqr(ImVec2(centre.x - mouse.x, centre.y - mouse.y));
                if (distance < best) {
                    best = distance;
                    hoveredPort = std::make_pair(layout.node->id, port->id);
                }
                if (!first)
                    first = std::make_pair(layout.node->id, port->id);
                const auto used = std::any_of(connections.begin(), connections.end(), [&](const auto& connection) {
                    return wire_->fromOutput ? connection.destination == layout.node->id && connection.destinationPort == port->id
                                             : connection.source == layout.node->id && connection.sourcePort == port->id;
                });
                if (!free && !used)
                    free = std::make_pair(layout.node->id, port->id);
            }
            if (inside)
                onCard = free ? free : first;
        }
        if (!hoveredPort)
            hoveredPort = onCard;
    }

    // A cable being drawn.
    if (wire_) {
        const auto colour = kindColour(wire_->kind);
        ImVec2 end = mouse;
        if (hoveredPort)
            for (const auto& layout : layouts)
                if (layout.node->id == hoveredPort->first)
                    for (const auto& [port, centre] : wire_->fromOutput ? layout.inputs : layout.outputs)
                        if (port->id == hoveredPort->second)
                            end = centre;
        const auto a = wire_->fromOutput ? wire_->anchor : end;
        const auto b = wire_->fromOutput ? end : wire_->anchor;
        const auto dx = std::max(40.0f * s, std::abs(b.x - a.x) * 0.5f);
        list->AddBezierCubic(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, colour, std::max(1.5f, 2.2f * s));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto pending = *wire_;
            wire_.reset();
            if (hoveredPort) {
                const auto error = pending.fromOutput
                                       ? graph.editorConnect(pending.node, pending.port, hoveredPort->first, hoveredPort->second)
                                       : graph.editorConnect(hoveredPort->first, hoveredPort->second, pending.node, pending.port);
                if (error.isNotEmpty()) {
                    lastError_ = error;
                    lastErrorTime_ = ImGui::GetTime();
                }
            } else if (mouse.x > canvasMin_.x && mouse.x < canvasMax_.x && mouse.y > canvasMin_.y && mouse.y < canvasMax_.y) {
                // Dropped on the canvas: search what accepts it, connect it.
                openSearch(mouse, pending);
            }
        }
    }

    // Canvas clicks: box selection, search.
    if (canvasHovered && !wire_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsKeyDown(ImGuiKey_Space)) {
        if (!io.KeyCtrl && !io.KeyShift)
            selection_.clear();
        selectedWire_.reset();
        // A click on a cable selects it.
        for (const auto& connection : connections) {
            ImVec2 a {};
            ImVec2 b {};
            auto found = 0;
            for (const auto& layout : layouts) {
                if (layout.node->id == connection.source)
                    for (const auto& [port, centre] : layout.outputs)
                        if (port->id == connection.sourcePort) {
                            a = centre;
                            ++found;
                        }
                if (layout.node->id == connection.destination)
                    for (const auto& [port, centre] : layout.inputs)
                        if (port->id == connection.destinationPort) {
                            b = centre;
                            ++found;
                        }
            }
            if (found < 2)
                continue;
            const auto dx = std::max(40.0f * s, std::abs(b.x - a.x) * 0.5f);
            const auto closest = ImBezierCubicClosestPointCasteljau(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, mouse,
                                                                     ImGui::GetStyle().CurveTessellationMaxError);
            if (ImLengthSqr(ImVec2(closest.x - mouse.x, closest.y - mouse.y)) < dp(6.0f) * dp(6.0f)) {
                selectedWire_ = connection;
                if (io.KeyAlt) {
                    graph.editorDisconnect(connection.source, connection.sourcePort, connection.destination,
                                           connection.destinationPort);
                    selectedWire_.reset();
                }
                break;
            }
        }
        // On the empty canvas the hand takes the view; with Ctrl or Shift a
        // box selects.
        if (!selectedWire_) {
            if (io.KeyCtrl || io.KeyShift) {
                boxSelecting_ = true;
                boxStart_ = mouse;
            } else {
                grabbing_ = true;
            }
        }
    }
    if (canvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        openSearch(mouse, std::nullopt);
    if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const auto moved = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f);
        if (moved.x * moved.x + moved.y * moved.y < 16.0f) {
            // On a cable, the cable's menu; elsewhere, the node search.
            if (const auto wire = wireAt(connections, layouts, mouse)) {
                cableMenuWire_ = *wire;
                cableAmount_ = wire->amount;
                selectedWire_ = *wire;
                pendingPopup_ = cableMenuId_;
            } else {
                openSearch(mouse, std::nullopt);
            }
        }
    }
    if (boxSelecting_) {
        const ImVec2 a(std::min(boxStart_.x, mouse.x), std::min(boxStart_.y, mouse.y));
        const ImVec2 b(std::max(boxStart_.x, mouse.x), std::max(boxStart_.y, mouse.y));
        list->AddRectFilled(a, b, theme::alpha(p.accent, 0.08f));
        list->AddRect(a, b, theme::alpha(p.accent, 0.7f));
        if (b.x - a.x > 4.0f || b.y - a.y > 4.0f)
            for (const auto& layout : layouts) {
                const auto inside = layout.max.x >= a.x && layout.min.x <= b.x && layout.max.y >= a.y && layout.min.y <= b.y;
                if (inside)
                    selection_.insert(layout.node->id);
                else if (!io.KeyCtrl && !io.KeyShift)
                    selection_.erase(layout.node->id);
            }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            boxSelecting_ = false;
    }

    if (showMinimap_)
        drawMinimap(list, layouts);

    // Empty graph: how to start.
    if (nodes.empty())
        ui::emptyState(L("Graphe vide", "Empty graph"), L("Tab ou double-clic pour ajouter un nœud.", "Tab or double click to add a node."));

    ImGui::EndChild();
}

void GraphEditor::drawWires(ImDrawList* list, const std::vector<EditorConnection>& connections,
                            const std::vector<NodeLayout>& layouts)
{
    const auto& p = theme::palette();
    const auto s = scale();
    const auto mouse = ImGui::GetIO().MousePos;
    const auto playing = studio_.engine().isPlaying();
    const auto now = static_cast<float>(ImGui::GetTime());
    auto flowIndex = 0;
    // Under an open menu the cables neither light up nor say what they do.
    const auto menuOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    for (const auto& connection : connections) {
        ImVec2 a {};
        ImVec2 b {};
        auto found = 0;
        for (const auto& layout : layouts) {
            if (layout.node->id == connection.source)
                for (const auto& [port, centre] : layout.outputs)
                    if (port->id == connection.sourcePort) {
                        a = centre;
                        ++found;
                    }
            if (layout.node->id == connection.destination)
                for (const auto& [port, centre] : layout.inputs)
                    if (port->id == connection.destinationPort) {
                        b = centre;
                        ++found;
                    }
        }
        if (found < 2)
            continue;
        const auto dx = std::max(50.0f * s, std::abs(b.x - a.x) * 0.5f);
        const ImVec2 c1(a.x + dx, a.y);
        const ImVec2 c2(b.x - dx, b.y);
        const auto selected = selectedWire_ && selectedWire_->source == connection.source
                              && selectedWire_->destination == connection.destination
                              && selectedWire_->sourcePort == connection.sourcePort
                              && selectedWire_->destinationPort == connection.destinationPort;
        auto hovered = false;
        if (!wire_ && !movingNodes_ && !menuOpen) {
            const auto closest = ImBezierCubicClosestPointCasteljau(a, c1, c2, b, mouse, ImGui::GetStyle().CurveTessellationMaxError);
            hovered = ImLengthSqr(ImVec2(closest.x - mouse.x, closest.y - mouse.y)) < dp(6.0f) * dp(6.0f);
        }
        const auto selectedFlow = selection_.contains(connection.source) || selection_.contains(connection.destination);
        auto colour = kindColour(connection.kind);
        const auto emphasised = selected || hovered || selectedFlow;
        if (!emphasised)
            colour = theme::alpha(colour, 0.8f);
        const auto thickness = std::max(1.2f, (connection.kind == 0 ? 2.8f : 2.0f) * s) + (selected || hovered ? 1.2f : 0.0f);
        // A dark edge under each cable, so it reads over the cards' shadows.
        list->AddBezierCubic(a, c1, c2, b, IM_COL32(0, 0, 0, 120), thickness + std::max(2.0f, 2.5f * s));
        if (selected)
            list->AddBezierCubic(a, c1, c2, b, theme::alpha(p.text, 0.30f), thickness + 4.0f);
        const auto spliceHere = spliceTarget_ && spliceTarget_->source == connection.source
                                && spliceTarget_->destination == connection.destination
                                && spliceTarget_->sourcePort == connection.sourcePort
                                && spliceTarget_->destinationPort == connection.destinationPort;
        if (spliceHere)
            list->AddBezierCubic(a, c1, c2, b, theme::alpha(p.accent, 0.55f), thickness + 7.0f);
        if (connection.kind == 2) {
            // Control cables dashed: they carry values, not sound.
            constexpr auto segments = 40;
            ImVec2 previous = a;
            for (auto segment = 1; segment <= segments; ++segment) {
                const auto point = ImBezierCubicCalc(a, c1, c2, b, static_cast<float>(segment) / segments);
                if (segment % 2 == 1)
                    list->AddLine(previous, point, colour, thickness);
                previous = point;
            }
        } else {
            list->AddBezierCubic(a, c1, c2, b, colour, thickness);
        }
        if (connection.kind == 2 && connection.drivesParameter && s > 0.45f) {
            const auto mid = ImBezierCubicCalc(a, c1, c2, b, 0.5f);
            const auto r = std::max(5.0f, 7.0f * s);
            list->AddCircleFilled(mid, r, p.bg0, 16);
            list->AddCircle(mid, r, colour, 16, 1.3f);
            if (connection.mode == 1) {
                ImVec2 wave[11];
                for (auto index = 0; index < 11; ++index) {
                    const auto t = static_cast<float>(index) / 10.0f;
                    wave[index] = ImVec2(mid.x + (t - 0.5f) * r * 1.3f,
                                         mid.y - std::sin(t * 6.2831853f) * r * 0.42f);
                }
                list->AddPolyline(wave, 11, colour, 1.5f);
            } else {
                list->AddLine(ImVec2(mid.x - r * 0.45f, mid.y - r * 0.22f), ImVec2(mid.x + r * 0.45f, mid.y - r * 0.22f), colour,
                              1.5f);
                list->AddLine(ImVec2(mid.x - r * 0.45f, mid.y + r * 0.22f), ImVec2(mid.x + r * 0.45f, mid.y + r * 0.22f), colour,
                              1.5f);
            }
        }
        // The sound moving along the audio cables while playing.
        if (playing && connection.kind == 0) {
            for (auto pulse = 0; pulse < 3; ++pulse) {
                const auto t = std::fmod(now * 0.55f + static_cast<float>(pulse) / 3.0f + static_cast<float>(flowIndex) * 0.17f, 1.0f);
                list->AddCircleFilled(ImBezierCubicCalc(a, c1, c2, b, t), std::max(1.6f, 2.4f * s), theme::brighter(colour, 0.45f), 10);
            }
            ++flowIndex;
        }
        if (hovered && connection.kind == 2 && connection.drivesParameter) {
            const auto what = connection.mode == 1
                                  ? ui::Ls("Module le bouton autour de sa valeur, profondeur ", "Modulates the knob around its value, depth ")
                                        + juce::String(juce::roundToInt(connection.amount * 100.0f)) + " %"
                                  : ui::Ls("Règle la valeur du bouton", "Sets the knob's value");
            ImGui::SetTooltip("%s\n%s", what.toRawUTF8(),
                              L("Clic droit : régler ou moduler, profondeur. Alt+clic : couper",
                                "Right click: set or modulate, depth. Alt+click: cut"));
        } else if (hovered) {
            ImGui::SetTooltip("%s", L("Clic : sélectionner, Alt+clic : couper, Suppr : supprimer",
                                      "Click: select, Alt+click: cut, Del: delete"));
        }
    }
    if (playing)
        host_.wake(2);
}

void GraphEditor::drawNode(app::NodeGraphComponent& graph, const NodeLayout& layout, ImDrawList* list)
{
    const auto& p = theme::palette();
    const auto& node = *layout.node;
    const auto s = scale();
    auto& io = ImGui::GetIO();
    const auto selected = selection_.contains(node.id);
    const auto colour = node.boundary ? p.bg5 : toImColour(node.colour);
    const auto rounding = 7.0f * s;

    // A soft shadow, the body, the header tinted with the card's colour.
    list->AddRectFilled(ImVec2(layout.min.x + 1.0f * s, layout.min.y + 4.0f * s), ImVec2(layout.max.x + 3.0f * s, layout.max.y + 7.0f * s),
                        IM_COL32(0, 0, 0, 45), rounding + 3.0f * s);
    list->AddRectFilled(ImVec2(layout.min.x + 1.0f * s, layout.min.y + 2.0f * s), ImVec2(layout.max.x + 1.5f * s, layout.max.y + 3.5f * s),
                        IM_COL32(0, 0, 0, 70), rounding);
    if (selected)
        list->AddRect(ImVec2(layout.min.x - 3.0f * s, layout.min.y - 3.0f * s), ImVec2(layout.max.x + 3.0f * s, layout.max.y + 3.0f * s),
                      theme::alpha(p.accent, 0.35f), rounding + 3.0f * s, std::max(2.0f, 3.0f * s));
    list->AddRectFilled(layout.min, layout.max, node.boundary ? p.bg3 : p.bg2, rounding);
    list->AddRectFilled(layout.min, ImVec2(layout.max.x, layout.min.y + layout.headerHeight),
                        theme::mix(colour, (node.boundary ? p.bg3 : p.bg2) | 0xff000000u, node.boundary ? 0.3f : 0.55f), rounding,
                        ImDrawFlags_RoundCornersTop);
    // The card's colour, as a band along the header's foot.
    list->AddRectFilled(ImVec2(layout.min.x, layout.min.y + layout.headerHeight - 2.5f * s),
                        ImVec2(layout.max.x, layout.min.y + layout.headerHeight), colour);
    const auto border = node.invalid ? p.record : selected ? p.accent : theme::alpha(p.lineStrong, 0.9f);
    list->AddRect(layout.min, layout.max, border, rounding, selected || node.invalid ? 1.8f : 1.0f);

    ImGui::PushID(static_cast<int>(node.id));
    // The card itself: select, drag, double-click to rename. The controls
    // placed afterwards take the pointer first.
    ImGui::SetCursorScreenPos(layout.min);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##card", ImVec2(std::max(1.0f, layout.max.x - layout.min.x), std::max(1.0f, layout.max.y - layout.min.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (io.KeyCtrl || io.KeyShift) {
            if (selection_.contains(node.id))
                selection_.erase(node.id);
            else
                selection_.insert(node.id);
        } else if (!selection_.contains(node.id)) {
            selection_ = { node.id };
        }
        selectedWire_.reset();
        movingNodes_ = true;
        moveStartMouse_ = io.MousePos;
        moveOrigins_.clear();
        for (const auto& other : graph.editorNodes())
            if (selection_.contains(other.id))
                moveOrigins_[other.id] = fractionToWorld(other.position);
    }
    if (ImGui::IsItemActive() && movingNodes_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        const ImVec2 delta((io.MousePos.x - moveStartMouse_.x) / s, (io.MousePos.y - moveStartMouse_.y) / s);
        std::vector<std::pair<std::uint64_t, juce::Point<float>>> positions;
        for (const auto& [id, origin] : moveOrigins_) {
            auto world = ImVec2(origin.x + delta.x, origin.y + delta.y);
            if (!io.KeyAlt) {
                world.x = std::round(world.x / (kGridStep * 0.5f)) * kGridStep * 0.5f;
                world.y = std::round(world.y / (kGridStep * 0.5f)) * kGridStep * 0.5f;
            }
            positions.push_back({ id, worldToFraction(world) });
        }
        graph.editorMoveNodes(positions, false);
    }
    if (ImGui::IsItemDeactivated() && movingNodes_) {
        movingNodes_ = false;
        if (ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2.0f).x != 0.0f
            || ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2.0f).y != 0.0f) {
            graph.editorMoveNodes({}, true);
            if (selection_.size() == 1)
                droppedNode_ = node.id;
        }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
        && io.MousePos.y < layout.min.y + layout.headerHeight && !node.boundary) {
        if (node.composite) {
            pendingEnter_ = node.id;
        } else {
            renaming_ = node.id;
            focusRename_ = true;
            std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", node.title.toRawUTF8());
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (!selection_.contains(node.id))
            selection_ = { node.id };
        contextNode_ = node.id;
        pendingPopup_ = nodeMenuId_;
    }
    if (ImGui::IsItemHovered() && !node.description.isEmpty()
        && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)
        && io.MousePos.y < layout.min.y + layout.headerHeight)
        ImGui::SetTooltip("%s", node.description.toRawUTF8());

    // Title, and under it what the card is (a module, its class).
    const auto fontScale = std::clamp(view_->zoom, 0.55f, 1.5f);
    if (renaming_ && *renaming_ == node.id) {
        ImGui::SetCursorScreenPos(ImVec2(layout.min.x + 4.0f * s, layout.min.y + 3.0f * s));
        ImGui::SetNextItemWidth(layout.max.x - layout.min.x - 8.0f * s);
        if (focusRename_) {
            ImGui::SetKeyboardFocusHere();
            focusRename_ = false;
        }
        if (ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            graph.editorSetAlias(node.id, juce::String::fromUTF8(renameBuffer_));
            renaming_.reset();
        } else if (ImGui::IsItemDeactivated()) {
            renaming_.reset();
        }
    } else if (s > 0.3f) {
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.92f * fontScale);
        // A graph's own ends, said shortly: the card is narrow.
        auto title = node.boundary ? node.title.toUpperCase() : node.title;
        if (node.boundary) {
            if (node.typeKey.startsWith("graph-audio-input"))
                title = ui::Ls("ENTRÉE AUDIO", "AUDIO IN");
            else if (node.typeKey.startsWith("graph-audio-output"))
                title = ui::Ls("SORTIE AUDIO", "AUDIO OUT");
            else if (node.typeKey == "midi-input" || node.typeKey == "graph-midi-input")
                title = ui::Ls("ENTRÉE MIDI", "MIDI IN");
            else if (node.typeKey == "master")
                title = ui::Ls("SORTIE", "OUTPUT");
        }
        const auto textTop = layout.min.y + (layout.headerHeight - ImGui::GetFontSize()) * 0.5f - 1.0f * s;
        auto textLeft = layout.min.x + 9.0f * s;
        if (node.composite && !node.boundary) {
            // A module: a small stack before its name.
            const auto icon = ImVec2(textLeft, textTop + ImGui::GetFontSize() * 0.15f);
            const auto box = ImGui::GetFontSize() * 0.55f;
            list->AddRect(ImVec2(icon.x + 2.0f * s, icon.y), ImVec2(icon.x + 2.0f * s + box, icon.y + box), theme::alpha(p.text, 0.6f),
                          1.5f * s);
            list->AddRectFilled(ImVec2(icon.x, icon.y + 2.0f * s), ImVec2(icon.x + box, icon.y + 2.0f * s + box), theme::alpha(p.text, 0.75f),
                                1.5f * s);
            textLeft += box + 7.0f * s;
        }
        ui::clippedText(list, ImVec2(textLeft, textTop), layout.max.x - textLeft - 28.0f * s, p.text, title.toRawUTF8());
        ImGui::PopFont();
    }
    // Collapse toggle.
    if (!node.boundary && s > 0.45f) {
        const auto buttonSize = layout.headerHeight * 0.72f;
        ImGui::SetCursorScreenPos(ImVec2(layout.max.x - buttonSize - 4.0f * s, layout.min.y + (layout.headerHeight - buttonSize) * 0.5f));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f * fontScale);
        if (ui::iconButton(node.collapsed ? icon::chevronDown : icon::chevronUp,
                           node.collapsed ? L("Déplier", "Expand") : L("Replier", "Collapse"), false, 0,
                           ImVec2(buttonSize, buttonSize)))
            graph.editorSetCollapsed(node.id, !node.collapsed);
        ImGui::PopFont();
    }

    // A live value: what an LFO or envelope puts out now.
    const auto liveValue = graph.editorControlValue(node.id);
    if (liveValue) {
        const auto t = std::clamp((*liveValue + 1.0f) * 0.5f, 0.0f, 1.0f);
        const auto y = layout.min.y + layout.headerHeight - 2.5f * s;
        list->AddRectFilled(ImVec2(layout.min.x, y), ImVec2(layout.min.x + (layout.max.x - layout.min.x) * t, y + 2.5f * s),
                            p.modulation);
        host_.wake(2);
    }
    if (node.collapsed || node.boundary || s <= 0.35f) {
        ImGui::PopID();
        return;
    }

    // The card's display, then its knobs.
    auto card = graph_layout::content(node);
    const auto setParameter = [&graph, this](const visual::Param& parameter, const double value) {
        (void) graph.editorSetParameter(parameter.binding, value);
        state_.focused = panel();
    };
    auto top = layout.bodyTop;
    if (card.pane) {
        DeviceSignals::Histories histories;
        auto live = (inWorkshop() && !simulated_) || showingModulation_ ? visual::Live {} : signals_.live(node.id, histories);
        const auto cables = graph.editorModulatedParameters();
        live.modulated = &cables;
        if (liveValue) {
            live.hasControl = true;
            live.control = *liveValue;
            live.running = live.running || studio_.engine().isPlaying();
        }
        ImGui::PushFont(nullptr, ui::currentFontBase() * fontScale);
        const ImVec2 at(layout.min.x + 7.0f * s, top);
        const ImVec2 size(layout.max.x - layout.min.x - 14.0f * s, graph_layout::paneHeight * s);
        auto expand = false;
        std::optional<CardFileScope> cardFile;
        if (state_.selectedTrack && !inWorkshop() && !showingModulation_)
            cardFile.emplace(studio_, state_, *state_.selectedTrack, graph, node.id, node.typeKey);
        if (visual::drawPane("##cardPane", *card.pane, card.parameters, at, size, colour, live, setParameter, false, &expand))
            state_.focused = panel();
        // The card's display is its device's first.
        if (expand && state_.selectedTrack && !inWorkshop())
            state_.openExpandedPane(ExpandedPane { *state_.selectedTrack, node.id, 0 });
        if (ImGui::IsMouseHoveringRect(at, ImVec2(at.x + size.x, at.y + size.y)))
            knobHovered_ = true;
        ImGui::PopFont();
        top += (graph_layout::paneHeight + 8.0f) * s;
    }
    const auto modulated = graph.editorModulatedParameters();
    if (!card.knobs.empty() && s > 0.4f) {
        const auto cardWidth = layout.max.x - layout.min.x;
        const auto gridWidth = static_cast<float>(card.columns) * kKnobCellW * s;
        const auto left = layout.min.x + (cardWidth - gridWidth) * 0.5f;
        ImGui::PushFont(nullptr, ui::currentFontBase() * fontScale);
        for (std::size_t slot = 0; slot < card.knobs.size(); ++slot) {
            const auto& parameter = node.parameters[static_cast<std::size_t>(card.knobs[slot])];
            const auto column = static_cast<int>(slot) % card.columns;
            const auto row = static_cast<int>(slot) / card.columns;
            ImGui::SetCursorScreenPos(ImVec2(left + column * kKnobCellW * s, top + row * kKnobCellH * s));
            auto value = static_cast<float>(parameter.value);
            ui::KnobStyle style;
            style.diameter = 28.0f * view_->zoom;
            const auto label = parameter.label.length() > 9 ? parameter.label.substring(0, 8) + "." : parameter.label;
            const auto labelText = label.toStdString();
            style.label = labelText.c_str();
            const auto format = valueFormat(parameter.minimum, parameter.maximum);
            style.format = format.c_str();
            const auto unit = parameter.unit.toStdString();
            style.unit = unit.c_str();
            style.colour = colour;
            if (isLogUnit(parameter.unit, parameter.minimum))
                style.flags |= ui::knobLogarithmic;
            if (parameter.minimum < 0.0 && parameter.maximum > 0.0 && std::abs(parameter.minimum + parameter.maximum) < 1.0e-6)
                style.flags |= ui::knobBipolar;
            if (s < 0.62f)
                style.flags |= ui::knobNoLabel;
            // Where a cable puts the knob now.
            if (const auto found = modulated.find(parameter.binding); found != modulated.end() && parameter.maximum > parameter.minimum) {
                const auto low = static_cast<float>(parameter.minimum);
                const auto high = static_cast<float>(parameter.maximum);
                style.modulation = (style.flags & ui::knobLogarithmic) != 0 && low > 0.0f
                                       ? std::log(std::max(low, found->second) / low) / std::log(high / low)
                                       : (found->second - low) / (high - low);
            }
            ImGui::PushID(parameter.binding.toRawUTF8());
            if (ui::knob("##p", &value, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                         static_cast<float>(parameter.defaultValue), style)) {
                (void) graph.editorSetParameter(parameter.binding, value);
                state_.focused = panel();
            }
            if (ImGui::IsItemHovered()) {
                knobHovered_ = true;
                ImGui::SetTooltip("%s", parameter.label.toRawUTF8());
            }
            ImGui::PopID();
        }
        ImGui::PopFont();
        if (card.hiddenKnobs > 0) {
            char more[32];
            std::snprintf(more, sizeof(more), "+%d", card.hiddenKnobs);
            list->AddText(ImVec2(layout.max.x - 24.0f * s, layout.max.y - 17.0f * s), p.textFaint, more);
        }
    }
    ImGui::PopID();
}

void GraphEditor::drawComments(app::NodeGraphComponent& graph, ImDrawList* list)
{
    const auto& p = theme::palette();
    const auto s = scale();
    for (const auto& comment : graph.editorComments()) {
        const auto min = worldToScreen(fractionToWorld(comment.position));
        const auto max = worldToScreen(fractionToWorld({ comment.position.x + comment.size.x, comment.position.y + comment.size.y }));
        const auto colour = toImColour(comment.colour);
        list->AddRectFilled(min, max, theme::alpha(colour, 0.10f), 8.0f * s);
        list->AddRect(min, max, theme::alpha(colour, 0.45f), 8.0f * s);
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * std::clamp(view_->zoom, 0.6f, 1.6f));
        list->AddText(ImVec2(min.x + 8.0f * s, min.y + 6.0f * s), theme::alpha(colour, 0.95f), comment.title.toRawUTF8());
        ImGui::PopFont();
        juce::ignoreUnused(p);
    }
}

void GraphEditor::drawMinimap(ImDrawList* list, const std::vector<NodeLayout>& layouts)
{
    if (layouts.empty())
        return;
    const auto& p = theme::palette();
    // Only when something is out of view.
    auto outside = false;
    for (const auto& layout : layouts)
        if (layout.min.x < canvasMin_.x || layout.max.x > canvasMax_.x || layout.min.y < canvasMin_.y
            || layout.max.y > canvasMax_.y)
            outside = true;
    if (!outside)
        return;
    const ImVec2 size(dp(140.0f), dp(84.0f));
    const ImVec2 max(canvasMax_.x - dp(10.0f), canvasMax_.y - dp(10.0f));
    const ImVec2 min(max.x - size.x, max.y - size.y);
    // World bounds of the cards and of the view.
    auto worldMin = ImVec2(1.0e9f, 1.0e9f);
    auto worldMax = ImVec2(-1.0e9f, -1.0e9f);
    for (const auto& layout : layouts) {
        const auto a = screenToWorld(layout.min);
        const auto b = screenToWorld(layout.max);
        worldMin = ImVec2(std::min(worldMin.x, a.x), std::min(worldMin.y, a.y));
        worldMax = ImVec2(std::max(worldMax.x, b.x), std::max(worldMax.y, b.y));
    }
    const auto viewMin = screenToWorld(canvasMin_);
    const auto viewMax = screenToWorld(canvasMax_);
    worldMin = ImVec2(std::min(worldMin.x, viewMin.x), std::min(worldMin.y, viewMin.y));
    worldMax = ImVec2(std::max(worldMax.x, viewMax.x), std::max(worldMax.y, viewMax.y));
    const auto extent = ImVec2(std::max(1.0f, worldMax.x - worldMin.x), std::max(1.0f, worldMax.y - worldMin.y));
    const auto mapScale = std::min((size.x - dp(8.0f)) / extent.x, (size.y - dp(8.0f)) / extent.y);
    const auto map = [&](const ImVec2 world) {
        return ImVec2(min.x + dp(4.0f) + (world.x - worldMin.x) * mapScale, min.y + dp(4.0f) + (world.y - worldMin.y) * mapScale);
    };
    list->AddRectFilled(min, max, theme::alpha(p.bg1, 0.80f), dp(5.0f));
    list->AddRect(min, max, p.lineStrong, dp(5.0f));
    for (const auto& layout : layouts)
        list->AddRectFilled(map(screenToWorld(layout.min)), map(screenToWorld(layout.max)),
                            selection_.contains(layout.node->id) ? p.accent : toImColour(layout.node->colour), dp(1.5f));
    list->AddRect(map(viewMin), map(viewMax), p.text, 0.0f, 1.0f);
    // Click or drag to move the view.
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##minimap", size);
    if (ImGui::IsItemActive()) {
        const auto mouse = ImGui::GetIO().MousePos;
        const ImVec2 world(worldMin.x + (mouse.x - min.x - dp(4.0f)) / mapScale, worldMin.y + (mouse.y - min.y - dp(4.0f)) / mapScale);
        const auto s = scale();
        view_->scroll = ImVec2(world.x - (canvasMax_.x - canvasMin_.x) * 0.5f / s, world.y - (canvasMax_.y - canvasMin_.y) * 0.5f / s);
        view_->touched = true;
    }
}

float GraphEditor::frameAll(const std::vector<EditorNode>& nodes, const bool selectionOnly)
{
    auto worldMin = ImVec2(1.0e9f, 1.0e9f);
    auto worldMax = ImVec2(-1.0e9f, -1.0e9f);
    auto count = 0;
    for (const auto& node : nodes) {
        if (selectionOnly && !selection_.contains(node.id))
            continue;
        const auto world = fractionToWorld(node.position);
        worldMin = ImVec2(std::min(worldMin.x, world.x), std::min(worldMin.y, world.y));
        worldMax = ImVec2(std::max(worldMax.x, world.x + graph_layout::width(node)),
                          std::max(worldMax.y, world.y + graph_layout::height(node)));
        ++count;
    }
    if (count == 0) {
        view_->zoom = 1.0f;
        view_->scroll = ImVec2(-40.0f, -40.0f);
        return 1.0f;
    }
    const auto dpi = ImGui::GetStyle().FontScaleDpi;
    const auto width = (canvasMax_.x - canvasMin_.x) / dpi;
    const auto height = (canvasMax_.y - canvasMin_.y) / dpi;
    const auto margin = 60.0f;
    const auto fit = std::min((width - margin * 2.0f) / std::max(1.0f, worldMax.x - worldMin.x),
                              (height - margin * 2.0f) / std::max(1.0f, worldMax.y - worldMin.y));
    // Never so small the cards cannot be read: a wide graph starts at its
    // inputs and scrolls (the minimap shows the rest).
    const auto readable = 0.72f;
    const auto zoom = std::clamp(fit, readable, 1.2f);
    view_->zoom = zoom;
    const auto centre = ImVec2((worldMin.x + worldMax.x) * 0.5f, (worldMin.y + worldMax.y) * 0.5f);
    view_->scroll = ImVec2(centre.x - width * 0.5f / zoom, centre.y - height * 0.5f / zoom);
    if (fit < readable) {
        view_->scroll.x = worldMin.x - margin / zoom;
        if ((worldMax.y - worldMin.y) * zoom > height - margin)
            view_->scroll.y = worldMin.y - margin / zoom;
    }
    return fit;
}

// ---- Splicing ------------------------------------------------------------------------------------

namespace {

// An audio input and an audio output, both free: a card that can go in a cable.
bool freeAudioEnds(const app::NodeGraphComponent::EditorNode& node, const std::vector<app::NodeGraphComponent::EditorConnection>& connections,
                   std::uint64_t& input, std::uint64_t& output)
{
    input = output = 0;
    for (const auto& port : node.ports) {
        if (port.kind != 0)
            continue;
        if (port.input && input == 0)
            input = port.id;
        else if (!port.input && output == 0)
            output = port.id;
    }
    if (input == 0 || output == 0)
        return false;
    for (const auto& connection : connections)
        if ((connection.destination == node.id && connection.destinationPort == input)
            || (connection.source == node.id && connection.sourcePort == output))
            return false;
    return true;
}

} // namespace

void GraphEditor::findSpliceTarget(const std::vector<EditorConnection>& connections, const std::vector<NodeLayout>& layouts)
{
    spliceTarget_.reset();
    const auto id = droppedNode_ ? *droppedNode_ : *selection_.begin();
    const NodeLayout* card = nullptr;
    for (const auto& layout : layouts)
        if (layout.node->id == id)
            card = &layout;
    std::uint64_t input = 0;
    std::uint64_t output = 0;
    if (card == nullptr || card->node->boundary || !freeAudioEnds(*card->node, connections, input, output))
        return;
    const ImVec2 centre((card->min.x + card->max.x) * 0.5f, card->min.y + card->headerHeight * 0.5f);
    const auto s = scale();
    auto best = std::max(dp(26.0f), 40.0f * s);
    for (const auto& connection : connections) {
        if (connection.kind != 0 || connection.source == id || connection.destination == id)
            continue;
        ImVec2 a {};
        ImVec2 b {};
        auto found = 0;
        for (const auto& layout : layouts) {
            if (layout.node->id == connection.source)
                for (const auto& [port, position] : layout.outputs)
                    if (port->id == connection.sourcePort) {
                        a = position;
                        ++found;
                    }
            if (layout.node->id == connection.destination)
                for (const auto& [port, position] : layout.inputs)
                    if (port->id == connection.destinationPort) {
                        b = position;
                        ++found;
                    }
        }
        if (found < 2)
            continue;
        const auto dx = std::max(50.0f * s, std::abs(b.x - a.x) * 0.5f);
        const auto closest = ImBezierCubicClosestPointCasteljau(a, ImVec2(a.x + dx, a.y), ImVec2(b.x - dx, b.y), b, centre,
                                                                 ImGui::GetStyle().CurveTessellationMaxError);
        const auto distance = std::sqrt(ImLengthSqr(ImVec2(closest.x - centre.x, closest.y - centre.y)));
        if (distance < best) {
            best = distance;
            spliceTarget_ = connection;
        }
    }
}

void GraphEditor::splice(app::NodeGraphComponent& graph, const std::vector<NodeLayout>& layouts)
{
    if (!spliceTarget_ || !droppedNode_)
        return;
    const auto connections = graph.editorConnections();
    for (const auto& layout : layouts) {
        if (layout.node->id != *droppedNode_)
            continue;
        std::uint64_t input = 0;
        std::uint64_t output = 0;
        if (!freeAudioEnds(*layout.node, connections, input, output))
            return;
        const auto target = *spliceTarget_;
        graph.editorDisconnect(target.source, target.sourcePort, target.destination, target.destinationPort);
        auto steps = 1;
        auto error = graph.editorConnect(target.source, target.sourcePort, layout.node->id, input);
        if (error.isEmpty()) {
            ++steps;
            error = graph.editorConnect(layout.node->id, output, target.destination, target.destinationPort);
        }
        if (error.isNotEmpty()) {
            // Put the cable back as it was: exactly the steps taken.
            for (auto step = 0; step < steps; ++step)
                (void) graph.editorUndo();
            lastError_ = error;
            lastErrorTime_ = ImGui::GetTime();
        }
        return;
    }
}

// ---- Search --------------------------------------------------------------------------------

void GraphEditor::openSearch(const ImVec2 screenPosition, std::optional<PendingWire> wire)
{
    searchScreen_ = screenPosition;
    searchWorld_ = screenToWorld(screenPosition);
    searchWire_ = wire;
    searchText_[0] = 0;
    searchHighlight_ = 0;
    focusSearch_ = true;
    pendingPopup_ = searchMenuId_;
}

void GraphEditor::searchPopup(GraphSlot& slot, const std::optional<TrackRef>& track)
{
    const auto& p = theme::palette();
    juce::ignoreUnused(track);
    ImGui::SetNextWindowPos(searchScreen_, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(dp(360.0f), dp(440.0f)), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("##nodeSearch"))
        return;
    auto& graph = slot.graph();
    if (searchWire_) {
        const char* kinds[] = { "audio", "MIDI", L("contrôle", "control"), "transport" };
        ImGui::TextColored(theme::vec(kindColour(searchWire_->kind)), "%s %s", L("Nœuds qui acceptent :", "Nodes that accept:"),
                           kinds[std::clamp(searchWire_->kind, 0, 3)]);
    }
    if (focusSearch_) {
        ImGui::SetKeyboardFocusHere();
        focusSearch_ = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    const auto entered = ImGui::InputTextWithHint("##search", L("Rechercher un nœud…", "Search a node…"), searchText_,
                                                  sizeof(searchText_), ImGuiInputTextFlags_EnterReturnsTrue);
    const auto query = fold(juce::String::fromUTF8(searchText_).trim());

    // Candidates, ranked.
    struct Candidate {
        const EditorNodeType* type;
        int score;
    };
    static std::vector<EditorNodeType> types;
    types = graph.editorNodeTypes();
    std::vector<Candidate> candidates;
    for (const auto& type : types) {
        if (type.hidden)
            continue;
        // A modulation graph takes values only: no audio, no MIDI.
        if (showingModulation_) {
            const auto valuesOnly = [](const std::vector<int>& kinds) {
                return std::all_of(kinds.begin(), kinds.end(), [](const int kind) { return kind == 2 || kind == 3; });
            };
            if ((type.inputKinds.empty() && type.outputKinds.empty()) || !valuesOnly(type.inputKinds) || !valuesOnly(type.outputKinds))
                continue;
        }
        if (searchWire_) {
            const auto& kinds = searchWire_->fromOutput ? type.inputKinds : type.outputKinds;
            if (std::find(kinds.begin(), kinds.end(), searchWire_->kind) == kinds.end())
                continue;
        }
        auto score = 1;
        if (query.isNotEmpty()) {
            const auto title = fold(type.title);
            const auto category = fold(type.category);
            if (title.startsWith(query))
                score = 100;
            else if (title.contains(query))
                score = 60;
            else if (fold(type.key).contains(query))
                score = 40;
            else if (category.contains(query))
                score = 25;
            else if (fold(type.description).contains(query))
                score = 10;
            else
                continue;
        }
        candidates.push_back({ &type, score });
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.type->category != b.type->category)
            return a.type->category < b.type->category;
        return a.type->title < b.type->title;
    });
    const auto count = static_cast<int>(candidates.size());
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        searchHighlight_ = std::min(count - 1, searchHighlight_ + 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        searchHighlight_ = std::max(0, searchHighlight_ - 1);
    searchHighlight_ = std::clamp(searchHighlight_, 0, std::max(0, count - 1));

    const EditorNodeType* chosen = nullptr;
    if (entered && count > 0)
        chosen = candidates[static_cast<std::size_t>(searchHighlight_)].type;

    auto describe = count > 0 ? candidates[static_cast<std::size_t>(searchHighlight_)].type : nullptr;
    const auto footer = ImGui::GetFontSize() * 3.2f;
    juce::ignoreUnused(footer);
    ImGui::BeginChild("##results", ImVec2(dp(344.0f), dp(330.0f)), ImGuiChildFlags_None);
    // Nothing typed: the tree of nodes, to walk; typed: the ranked list.
    if (query.isEmpty() && count > 0) {
        std::vector<const node_tree::Type*> kept;
        for (const auto& candidate : candidates)
            kept.push_back(candidate.type);
        const auto tree = node_tree::build(kept);
        node_tree::draw(tree, false, [&](const node_tree::Type& type) {
            const auto origin = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##type", false, ImGuiSelectableFlags_SpanAvailWidth, ImVec2(0.0f, ImGui::GetFontSize() * 1.35f)))
                chosen = &type;
            if (ImGui::IsItemHovered())
                describe = &type;
            auto* list = ImGui::GetWindowDrawList();
            list->AddRectFilled(ImVec2(origin.x + dp(2.0f), origin.y + dp(4.0f)),
                                ImVec2(origin.x + dp(6.0f), origin.y + ImGui::GetFontSize() * 1.1f), toImColour(type.colour), dp(1.5f));
            list->AddText(ImVec2(origin.x + dp(12.0f), origin.y + dp(2.0f)), type.module ? p.accent : p.text, type.title.toRawUTF8());
        });
    }
    for (auto index = 0; index < (query.isEmpty() ? 0 : count); ++index) {
        const auto& type = *candidates[static_cast<std::size_t>(index)].type;
        ImGui::PushID(index);
        const auto highlighted = index == searchHighlight_;
        const auto origin = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##type", highlighted, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFontSize() * 1.35f)))
            chosen = &type;
        if (highlighted && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            ImGui::SetScrollHereY(0.5f);
        if (ImGui::IsItemHovered() && type.description.isNotEmpty())
            ImGui::SetTooltip("%s", type.description.toRawUTF8());
        auto* list = ImGui::GetWindowDrawList();
        list->AddRectFilled(ImVec2(origin.x + dp(2.0f), origin.y + dp(4.0f)), ImVec2(origin.x + dp(6.0f), origin.y + ImGui::GetFontSize() * 1.1f),
                            toImColour(type.colour), dp(1.5f));
        list->AddText(ImVec2(origin.x + dp(12.0f), origin.y + dp(2.0f)), p.text, type.title.toRawUTF8());
        if (query.isNotEmpty()) {
            // Where it sits in the tree, its last level.
            const auto path = node_tree::pathOf(type);
            const auto where = path.empty() ? juce::String() : path.back();
            const auto width = ImGui::CalcTextSize(where.toRawUTF8()).x;
            list->AddText(ImVec2(origin.x + ImGui::GetContentRegionAvail().x - width - dp(4.0f), origin.y + dp(2.0f)), p.textFaint,
                          where.toRawUTF8());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", node_tree::pathText(type).toRawUTF8());
        }
        if (type.module) {
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.75f);
            const auto x = origin.x + dp(16.0f) + ImGui::CalcTextSize(type.title.toRawUTF8()).x * 1.2f;
            list->AddText(ImVec2(x, origin.y + dp(4.0f)), p.accentDim, "MODULE");
            ImGui::PopFont();
        }
        ImGui::PopID();
    }
    if (count == 0)
        ImGui::TextDisabled("%s", L("Aucun nœud ne correspond.", "No node matches."));
    ImGui::EndChild();
    // What the highlighted node does.
    ImGui::Separator();
    if (describe != nullptr) {
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.9f);
        ImGui::TextColored(theme::vec(toImColour(describe->colour)), "%s", describe->title.toRawUTF8());
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.85f);
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           describe->description.isNotEmpty() ? describe->description.toRawUTF8()
                                                              : node_tree::pathText(*describe).toRawUTF8());
        ImGui::PopFont();
        ImGui::PopTextWrapPos();
    }

    if (chosen != nullptr) {
        const auto key = chosen->key;
        const auto id = graph.editorAddNode(key, worldToFraction(searchWorld_));
        if (id != 0) {
            selection_ = { id };
            if (searchWire_) {
                // Connect the new card to the cable that asked for it.
                for (const auto& node : graph.editorNodes()) {
                    if (node.id != id)
                        continue;
                    for (const auto& port : node.ports) {
                        if (port.kind != searchWire_->kind || port.input != searchWire_->fromOutput)
                            continue;
                        const auto error = searchWire_->fromOutput
                                               ? graph.editorConnect(searchWire_->node, searchWire_->port, id, port.id)
                                               : graph.editorConnect(id, port.id, searchWire_->node, searchWire_->port);
                        if (error.isEmpty())
                            break;
                        lastError_ = error;
                        lastErrorTime_ = ImGui::GetTime();
                    }
                }
            }
        }
        searchWire_.reset();
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void GraphEditor::nodeContextMenu(GraphSlot& slot)
{
    if (!ImGui::BeginPopup("##nodeMenu"))
        return;
    auto& graph = slot.graph();
    if (contextNode_) {
        const auto nodes = graph.editorNodes();
        const auto found = std::find_if(nodes.begin(), nodes.end(), [this](const auto& n) { return n.id == *contextNode_; });
        if (found != nodes.end()) {
            ImGui::TextDisabled("%s", found->title.toRawUTF8());
            ImGui::Separator();
            if (found->composite
                && ImGui::MenuItem(inWorkshop() ? L("Ouvrir (éditer son graphe)", "Open (edit its graph)")
                                                : L("Éditer dans l'atelier", "Edit in the workshop"),
                                   L("Double-clic", "Double click")))
                pendingEnter_ = found->id;
            if (!found->boundary && ImGui::MenuItem(L("Réunir en un périphérique", "Make one device of them"), "Ctrl+G"))
                groupSelection(slot);
            if (!found->boundary && ImGui::MenuItem(L("Renommer", "Rename"), found->composite ? nullptr : L("Double-clic", "Double click"))) {
                renaming_ = found->id;
                focusRename_ = true;
                std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", found->title.toRawUTF8());
            }
            if (!found->boundary && ImGui::MenuItem(L("Dupliquer", "Duplicate"), "Ctrl+D")) {
                const auto copies = graph.editorDuplicate({ selection_.begin(), selection_.end() });
                selection_ = { copies.begin(), copies.end() };
            }
            if (!found->boundary && ImGui::MenuItem(found->collapsed ? L("Déplier", "Expand") : L("Replier", "Collapse")))
                graph.editorSetCollapsed(found->id, !found->collapsed);
            if (found->alias.isNotEmpty() && ImGui::MenuItem(L("Nom d'origine", "Original name")))
                graph.editorSetAlias(found->id, {});
            ImGui::Separator();
            if (!found->boundary && ImGui::MenuItem(L("Supprimer", "Delete"), L("Suppr", "Del"))) {
                graph.editorRemoveNodes({ selection_.begin(), selection_.end() });
                selection_.clear();
            }
            if (found->boundary)
                ImGui::TextDisabled("%s", L("Entrée ou sortie de la piste : toujours présente.", "Track input or output: always present."));
        }
    }
    ImGui::EndPopup();
}

void GraphEditor::handleKeys(GraphSlot& slot)
{
    auto& io = ImGui::GetIO();
    if (io.WantTextInput || renaming_)
        return;
    auto& graph = slot.graph();
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false))
        openSearch(ImGui::IsMousePosValid() ? io.MousePos : ImVec2((canvasMin_.x + canvasMax_.x) * 0.5f, canvasMin_.y + dp(60.0f)),
                   std::nullopt);
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        if (selectedWire_) {
            graph.editorDisconnect(selectedWire_->source, selectedWire_->sourcePort, selectedWire_->destination,
                                   selectedWire_->destinationPort);
            selectedWire_.reset();
        } else if (!selection_.empty()) {
            graph.editorRemoveNodes({ selection_.begin(), selection_.end() });
            selection_.clear();
        }
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !selection_.empty()) {
        const auto copies = graph.editorDuplicate({ selection_.begin(), selection_.end() });
        selection_ = { copies.begin(), copies.end() };
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        for (const auto& node : graph.editorNodes())
            selection_.insert(node.id);
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        frameAll(graph.editorNodes(), !selection_.empty());
        view_->touched = !selection_.empty();
    }
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        graph_layout::arrange(graph);
        view_->framed = false;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_G, false))
        groupSelection(slot);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        // Nothing picked: out of the device open, one level up.
        if (selection_.empty() && !selectedWire_ && inWorkshop())
            studio_.leaveWorkshopDevice();
        selection_.clear();
        selectedWire_.reset();
    }
}

} // namespace cydaw::v2
