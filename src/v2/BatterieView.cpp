#include "BatterieView.h"

#include "InstrumentEffects.h"
#include "SamplerControlsView.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
namespace drum = engine::drum;
using drum::Piece;

namespace {

constexpr ImU32 batterieGold = IM_COL32(0xe8, 0xc0, 0x6a, 0xff);
constexpr ImU32 shellColour = IM_COL32(0x8c, 0x3d, 0x30, 0xff);
constexpr ImU32 headColour = IM_COL32(0xe6, 0xe0, 0xd0, 0xff);
constexpr ImU32 cymbalColour = IM_COL32(0xd4, 0xae, 0x4a, 0xff);

// Where each piece sits on a kit 1000 wide and 600 high, seen from the
// front: the cymbals behind, the drums before them.
struct Place {
    Piece piece;
    float x;
    float y;
    float r;
    bool cymbal;
};
constexpr std::array<Place, 11> places { {
    { Piece::crash, 235.0f, 105.0f, 125.0f, true },
    { Piece::china, 610.0f, 62.0f, 82.0f, true },
    { Piece::ride, 845.0f, 145.0f, 135.0f, true },
    { Piece::hihat, 105.0f, 265.0f, 92.0f, true },
    { Piece::percussion, 500.0f, 118.0f, 30.0f, false },
    { Piece::tom1, 410.0f, 212.0f, 66.0f, false },
    { Piece::tom2, 592.0f, 212.0f, 72.0f, false },
    { Piece::tom3, 748.0f, 322.0f, 82.0f, false },
    { Piece::snare, 288.0f, 382.0f, 88.0f, false },
    { Piece::tom4, 812.0f, 478.0f, 96.0f, false },
    { Piece::kick, 520.0f, 440.0f, 150.0f, false },
} };

constexpr float cymbalFlat = 0.3f;

juce::String nameOf(const Piece piece)
{
    return juce::String::fromUTF8(drum::pieceName(piece, ui::french()).c_str());
}

// "C1 (36)": a key as a person reads it.
juce::String keyName(const int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1) + " (" + juce::String(note) + ")";
}

} // namespace

BatterieView::BatterieView(Studio& studio, UiState& state) : studio_(studio), state_(state), presets_(studio, state) {}

float BatterieView::paneWidth()
{
    return dp(360.0f);
}

bool BatterieView::bind(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId)
{
    const auto values = graph.editorDrumValues(nodeId);
    if (!values)
        return false;
    card_.track = track;
    card_.graph = &graph;
    card_.nodeId = nodeId;
    card_.values = *values;
    return true;
}

float BatterieView::value(const int index) const
{
    return card_.values[static_cast<std::size_t>(std::clamp(index, 0, drum::valueCount - 1))];
}

void BatterieView::set(const int index, const float newValue)
{
    if (card_.graph == nullptr || index < 0 || index >= drum::valueCount)
        return;
    const auto clamped = drum::clampValue(index, newValue);
    if (clamped == value(index))
        return;
    card_.values[static_cast<std::size_t>(index)] = clamped;
    const auto binding = "node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".batterie." + juce::String(drum::parameter(index).key);
    (void) card_.graph->editorSetParameter(binding, clamped);
    state_.focused = Panel::devices;
}

bool BatterieView::knob(const int index, const char* label, const float diameter)
{
    const auto& p = theme::palette();
    const auto& parameter = drum::parameter(index);
    ImGui::PushID(index);
    ImGui::BeginGroup();
    const auto range = std::max(1.0e-6f, parameter.maximum - parameter.minimum);
    auto normalised = (value(index) - parameter.minimum) / range;
    const auto defaultNormalised = (parameter.defaultValue - parameter.minimum) / range;
    ui::KnobStyle style;
    style.diameter = diameter;
    style.flags = ui::knobNoLabel;
    if (parameter.minimum < 0.0f && parameter.maximum > 0.0f && parameter.unit != "dB")
        style.flags |= ui::knobBipolar;
    style.colour = batterieGold;
    const auto origin = ImGui::GetCursorScreenPos();
    const auto changed = ui::knob("##knob", &normalised, 0.0f, 1.0f, defaultNormalised, style);
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    if (changed)
        set(index, parameter.minimum + normalised * range);
    const auto name = juce::String::fromUTF8((ui::french() ? parameter.french : parameter.english).c_str());
    const auto shown = drum::displayValue(index, value(index), ui::french());
    if (ImGui::BeginPopupContextItem("##knobMenu")) {
        ImGui::TextDisabled("%s", name.toRawUTF8());
        if (ImGui::MenuItem(L("Valeur par défaut", "Default value")))
            set(index, parameter.defaultValue);
        const auto binding = "node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".batterie." + juce::String(parameter.key);
        if (studio_.isValid(card_.track) && studio_.canAutomate(card_.track, binding)) {
            const auto automated = !studio_.lanePoints(card_.track, binding).empty();
            if (ImGui::MenuItem(automated ? L("Afficher l'automation", "Show the automation")
                                          : L("Automatiser dans l'arrangement", "Automate in the arrangement"))) {
                studio_.setShownLane(card_.track, binding);
                studio_.setAutomationVisible(card_.track, true);
                state_.selectTrack(card_.track);
                state_.bringToFront = Panel::arrangement;
            }
            if (automated && ImGui::MenuItem(L("Effacer l'automation", "Clear the automation")))
                studio_.setLanePoints(card_.track, binding, {});
        }
        ImGui::EndPopup();
    }
    if (hovered && !active)
        ImGui::SetTooltip("%s : %s\n%s", name.toRawUTF8(), shown.c_str(),
                          L("Double-clic : valeur de départ · clic droit : automatiser", "Double click: default · right click: automate"));
    const auto width = dp(diameter + 14.0f);
    const auto text = hovered || active ? juce::String::fromUTF8(shown.c_str()) : juce::String::fromUTF8(label != nullptr ? label : "");
    auto* list = ImGui::GetWindowDrawList();
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    const auto textHeight = ImGui::GetFontSize();
    const auto size = ImGui::CalcTextSize(text.toRawUTF8());
    const auto y = origin.y + dp(diameter) + dp(1.0f);
    list->PushClipRect(ImVec2(origin.x - dp(8.0f), y), ImVec2(origin.x + width + dp(8.0f), y + textHeight + dp(2.0f)), true);
    list->AddText(ImVec2(origin.x + (width - size.x) * 0.5f, y), hovered || active ? p.text : p.textDim, text.toRawUTF8());
    list->PopClipRect();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(width, textHeight + dp(2.0f)));
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

void BatterieView::toggle(const int index, const char* label, const ImVec2 size)
{
    const auto& parameter = drum::parameter(index);
    ImGui::PushID(index);
    auto on = value(index) >= 0.5f;
    const auto colour = parameter.key.ends_with(".solo") ? batterieGold : theme::palette().record;
    if (ui::toggle(label, &on, colour, size, (ui::french() ? parameter.french : parameter.english).c_str()))
        set(index, on ? 1.0f : 0.0f);
    ImGui::PopID();
}

void BatterieView::kitChoice(const float width)
{
    const auto current = std::clamp(static_cast<int>(std::lround(value(drum::globalBase + drum::globalKit))), 0,
                                    static_cast<int>(drum::kits.size()) - 1);
    ImGui::SetNextItemWidth(width);
    const auto shown = L(drum::kits[static_cast<std::size_t>(current)].french, drum::kits[static_cast<std::size_t>(current)].english);
    if (ImGui::BeginCombo("##kit", shown)) {
        for (int at = 0; at < static_cast<int>(drum::kits.size()); ++at) {
            const auto& kit = drum::kits[static_cast<std::size_t>(at)];
            if (ImGui::Selectable(L(kit.french, kit.english), at == current) && at != current) {
                juce::String error;
                if (!studio_.setBatterieKit(card_.track, card_.nodeId, at, error))
                    grooveReport_ = error;
                else
                    card_.values[static_cast<std::size_t>(drum::globalBase + drum::globalKit)] = static_cast<float>(at);
            }
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Le kit joué : ses échantillons (une banque libre, téléchargée dans CyDAW s'il le faut)",
                  "The kit played: its samples (a free bank, downloaded in CyDAW when needed)"));
}

// ---- The kit drawn -----------------------------------------------------------------------------

void BatterieView::drawKit(const ImVec2 origin, const ImVec2 size, const bool interactive)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    list->AddRectFilled(origin, end, p.bg0, dp(4.0f));
    list->AddRect(origin, end, p.line, dp(4.0f));
    const auto scale = std::min(size.x / 1'000.0f, size.y / 600.0f);
    const ImVec2 offset(origin.x + (size.x - 1'000.0f * scale) * 0.5f, origin.y + (size.y - 600.0f * scale) * 0.5f);
    const auto centreOf = [&](const Place& place) { return ImVec2(offset.x + place.x * scale, offset.y + place.y * scale); };
    auto soloed = false;
    for (int piece = 0; piece < drum::pieceCount; ++piece)
        soloed = soloed || value(drum::index(static_cast<Piece>(piece), drum::fieldSolo)) >= 0.5f;
    // Which piece is under the pointer: the one drawn last first.
    const auto mouse = ImGui::GetIO().MousePos;
    auto hovered = -1;
    if (interactive && mouse.x >= origin.x && mouse.y >= origin.y && mouse.x < end.x && mouse.y < end.y)
        for (int at = static_cast<int>(places.size()) - 1; at >= 0 && hovered < 0; --at) {
            const auto& place = places[static_cast<std::size_t>(at)];
            const auto centre = centreOf(place);
            const auto rx = place.r * scale;
            const auto ry = place.cymbal ? rx * cymbalFlat : rx;
            const auto dx = (mouse.x - centre.x) / rx;
            const auto dy = (mouse.y - centre.y) / ry;
            if (dx * dx + dy * dy <= 1.0f)
                hovered = at;
        }
    list->PushClipRect(origin, end, true);
    for (int at = 0; at < static_cast<int>(places.size()); ++at) {
        const auto& place = places[static_cast<std::size_t>(at)];
        const auto centre = centreOf(place);
        const auto r = place.r * scale;
        const auto muted = value(drum::index(place.piece, drum::fieldMute)) >= 0.5f
                           || (soloed && value(drum::index(place.piece, drum::fieldSolo)) < 0.5f);
        const auto alpha = [muted](const ImU32 colour) { return muted ? (colour & 0x00ffffffu) | 0x50000000u : colour; };
        const auto selected = place.piece == selected_;
        if (place.cymbal) {
            // Its stand, then the cymbal seen slightly from above.
            list->AddLine(ImVec2(centre.x, centre.y), ImVec2(centre.x, end.y - dp(4.0f)), alpha(p.textFaint), std::max(1.0f, 3.0f * scale));
            list->AddEllipseFilled(centre, ImVec2(r, r * cymbalFlat), alpha(cymbalColour));
            list->AddEllipse(centre, ImVec2(r * 0.25f, r * cymbalFlat * 0.25f), alpha(theme::brighter(cymbalColour, 0.3f)), 0.0f, 0,
                             std::max(1.0f, 2.0f * scale));
            if (place.piece == Piece::hihat)
                list->AddEllipseFilled(ImVec2(centre.x, centre.y + 10.0f * scale), ImVec2(r, r * cymbalFlat * 0.8f),
                                       alpha(theme::brighter(cymbalColour, -0.25f)));
        } else if (place.piece == Piece::percussion) {
            list->AddRectFilled(ImVec2(centre.x - r, centre.y - r * 0.7f), ImVec2(centre.x + r, centre.y + r * 0.7f), alpha(p.textDim),
                                3.0f * scale);
        } else {
            list->AddCircleFilled(centre, r, alpha(shellColour), 48);
            list->AddCircleFilled(centre, r * 0.86f, alpha(headColour), 48);
            list->AddCircle(centre, r * 0.86f, alpha(p.textFaint), 48, std::max(1.0f, 3.0f * scale));
            if (place.piece == Piece::kick)
                list->AddCircle(centre, r * 0.25f, alpha(shellColour), 32, std::max(1.0f, 4.0f * scale));
        }
        const auto rx = r;
        const auto ry = place.cymbal ? r * cymbalFlat : r;
        if (selected || at == hovered)
            list->AddEllipse(centre, ImVec2(rx + dp(3.0f), ry + dp(3.0f)), selected ? batterieGold : p.text, 0.0f, 0,
                             dp(selected ? 2.5f : 1.2f));
        if (value(drum::index(place.piece, drum::fieldSolo)) >= 0.5f)
            list->AddText(ImVec2(centre.x - dp(4.0f), centre.y - dp(7.0f)), batterieGold, "S");
    }
    list->PopClipRect();
    if (!interactive)
        return;
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##kitPicture", size);
    if (hovered >= 0) {
        const auto piece = places[static_cast<std::size_t>(hovered)].piece;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            selected_ = piece;
        if (ImGui::IsItemHovered()) {
            const auto level = drum::index(piece, drum::fieldLevel);
            ImGui::SetTooltip("%s \xc2\xb7 %s\n%s", nameOf(piece).toRawUTF8(), drum::displayValue(level, value(level), ui::french()).c_str(),
                              L("Clic : ses réglages", "Click: its settings"));
        }
    }
}

// ---- The window ------------------------------------------------------------------------------

void BatterieView::drawPiecePanel()
{
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextColored(theme::vec(batterieGold), "%s", nameOf(selected_).toUpperCase().toRawUTF8());
    ImGui::PopFont();
    knob(drum::index(selected_, drum::fieldLevel), L("NIVEAU", "LEVEL"), 40.0f);
    ImGui::SameLine();
    knob(drum::index(selected_, drum::fieldPan), L("PANO.", "PAN"), 40.0f);
    ImGui::SameLine();
    knob(drum::index(selected_, drum::fieldTune), L("ACCORD", "TUNE"), 40.0f);
    ImGui::SameLine();
    knob(drum::index(selected_, drum::fieldDecay), L("DURÉE", "DECAY"), 40.0f);
    ImGui::SameLine();
    ImGui::BeginGroup();
    toggle(drum::index(selected_, drum::fieldMute), L("Muet", "Mute"), ImVec2(dp(64.0f), 0.0f));
    toggle(drum::index(selected_, drum::fieldSolo), "Solo", ImVec2(dp(64.0f), 0.0f));
    ImGui::EndGroup();
    // The keys that play it.
    juce::StringArray keys;
    for (int note = 21; note < 96; ++note)
        if (drum::pieceForNote(note) == selected_ && selected_ != Piece::percussion && selected_ != Piece::other)
            keys.add(keyName(note));
    if (!keys.isEmpty())
        ImGui::TextDisabled("%s %s", L("Touches :", "Keys:"), keys.joinIntoString(", ").toRawUTF8());
}

void BatterieView::drawKitTab()
{
    const auto available = ImGui::GetContentRegionAvail();
    const auto controlsHeight = dp(40.0f + 16.0f) + ImGui::GetTextLineHeightWithSpacing() * 2.0f + dp(16.0f);
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 kitSize(available.x, std::max(dp(220.0f), available.y - controlsHeight - dp(8.0f)));
    drawKit(origin, kitSize, true);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + kitSize.y + dp(8.0f)));
    ImGui::BeginChild("##piece", ImVec2(available.x * 0.62f, controlsHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    drawPiecePanel();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##kitGlobal", ImVec2(0.0f, controlsHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextColored(theme::vec(batterieGold), "%s", L("KIT", "KIT"));
    ImGui::PopFont();
    knob(drum::globalBase + drum::globalTune, L("ACCORD", "TUNE"), 40.0f);
    ImGui::SameLine();
    knob(drum::globalBase + drum::globalHumanise, L("JEU HUMAIN", "HUMANISE"), 40.0f);
    ImGui::SameLine();
    knob(drum::globalBase + drum::globalLevel, L("NIVEAU", "LEVEL"), 40.0f);
    ImGui::EndChild();
}

void BatterieView::drawConsoleTab()
{
    const auto& p = theme::palette();
    const auto available = ImGui::GetContentRegionAvail();
    const auto stripWidth = std::max(dp(70.0f), (available.x - dp(12.0f)) / static_cast<float>(drum::pieceCount + 1) - dp(4.0f));
    const auto stripHeight = available.y;
    const auto faderHeight = std::max(dp(80.0f), stripHeight - dp(250.0f));
    for (int at = 0; at <= drum::pieceCount; ++at) {
        const auto kit = at == drum::pieceCount;
        const auto piece = static_cast<Piece>(std::min(at, drum::pieceCount - 1));
        ImGui::PushID(at);
        ImGui::BeginChild("##strip", ImVec2(stripWidth, stripHeight), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
        const auto name = kit ? juce::String(L("KIT", "KIT")) : nameOf(piece);
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.85f);
        ui::clippedText(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), stripWidth - dp(10.0f),
                        !kit && piece == selected_ ? batterieGold : p.text, name.toRawUTF8());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, ImGui::GetTextLineHeight()));
        const auto levelIndex = kit ? drum::globalBase + drum::globalLevel : drum::index(piece, drum::fieldLevel);
        auto db = value(levelIndex);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (stripWidth - dp(38.0f)) * 0.5f - dp(8.0f)));
        if (ui::fader("##level", &db, -40.0f, 6.0f, ImVec2(dp(38.0f), faderHeight), batterieGold))
            set(levelIndex, db);
        ImGui::TextDisabled("%s", drum::displayValue(levelIndex, value(levelIndex), ui::french()).c_str());
        if (kit) {
            knob(drum::globalBase + drum::globalTune, L("ACCORD", "TUNE"), 28.0f);
            knob(drum::globalBase + drum::globalHumanise, L("HUMAIN", "HUMAN"), 28.0f);
        } else {
            knob(drum::index(piece, drum::fieldPan), L("PANO.", "PAN"), 28.0f);
            knob(drum::index(piece, drum::fieldTune), L("ACCORD", "TUNE"), 28.0f);
            knob(drum::index(piece, drum::fieldDecay), L("DURÉE", "DECAY"), 28.0f);
            const auto half = (stripWidth - dp(16.0f)) * 0.5f;
            toggle(drum::index(piece, drum::fieldMute), "M", ImVec2(half, 0.0f));
            ImGui::SameLine(0.0f, dp(2.0f));
            toggle(drum::index(piece, drum::fieldSolo), "S", ImVec2(half, 0.0f));
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                selected_ = piece;
        }
        ImGui::EndChild();
        ImGui::PopID();
        if (at < drum::pieceCount)
            ImGui::SameLine(0.0f, dp(4.0f));
    }
}

void BatterieView::drawGroovesTab()
{
    const auto& p = theme::palette();
    const auto& list = drum::grooves();
    ImGui::TextDisabled("%s", L("Un motif devient un clip MIDI sur la piste, à la mesure de la tête de lecture : on le modifie "
                                "ensuite dans l'éditeur comme n'importe quelles notes.",
                                "A groove becomes a MIDI clip on the track, at the playhead's bar: edit it afterwards in the "
                                "editor like any notes."));
    // The grooves, then the fills.
    for (int pass = 0; pass < 2; ++pass) {
        ImGui::TextColored(theme::vec(batterieGold), "%s", pass == 0 ? L("MOTIFS", "GROOVES") : L("ROULEMENTS", "FILLS"));
        auto first = true;
        for (int at = 0; at < static_cast<int>(list.size()); ++at) {
            if (list[static_cast<std::size_t>(at)].fill != (pass == 1))
                continue;
            if (!first)
                ImGui::SameLine(0.0f, dp(4.0f));
            first = false;
            auto chosen = groove_ == at;
            const auto label = juce::String::fromUTF8((ui::french() ? list[static_cast<std::size_t>(at)].french : list[static_cast<std::size_t>(at)].english).c_str());
            ImGui::PushID(at);
            if (ui::toggle(label.toRawUTF8(), &chosen, batterieGold, ImVec2(0.0f, 0.0f)))
                groove_ = at;
            ImGui::PopID();
        }
    }
    // The chosen one, drawn: a line a key, its hits on sixteen steps.
    const auto& groove = list[static_cast<std::size_t>(std::clamp(groove_, 0, static_cast<int>(list.size()) - 1))];
    std::vector<int> lanes;
    for (const auto& hit : groove.hits)
        if (std::find(lanes.begin(), lanes.end(), hit.note) == lanes.end())
            lanes.push_back(hit.note);
    std::sort(lanes.begin(), lanes.end(), std::greater<>());
    const auto origin = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto laneHeight = dp(20.0f);
    const auto nameWidth = dp(150.0f);
    auto* draw = ImGui::GetWindowDrawList();
    const auto gridWidth = std::max(dp(100.0f), width - nameWidth);
    for (std::size_t lane = 0; lane < lanes.size(); ++lane) {
        const auto y = origin.y + static_cast<float>(lane) * laneHeight;
        const auto piece = drum::pieceForNote(lanes[lane]);
        draw->AddText(ImVec2(origin.x, y + dp(2.0f)), p.textDim, (nameOf(piece) + " \xc2\xb7 " + juce::String(lanes[lane])).toRawUTF8());
        draw->AddRectFilled(ImVec2(origin.x + nameWidth, y + dp(1.0f)), ImVec2(origin.x + nameWidth + gridWidth, y + laneHeight - dp(1.0f)),
                            p.bg0, dp(2.0f));
        for (int beat = 1; beat < 4; ++beat) {
            const auto x = origin.x + nameWidth + gridWidth * static_cast<float>(beat) / 4.0f;
            draw->AddLine(ImVec2(x, y), ImVec2(x, y + laneHeight), p.grid);
        }
        for (const auto& hit : groove.hits)
            if (hit.note == lanes[lane]) {
                const auto x = origin.x + nameWidth + gridWidth * static_cast<float>(hit.beat / 4.0) + dp(6.0f);
                const auto alpha = static_cast<unsigned>(80 + 175 * hit.velocity);
                draw->AddCircleFilled(ImVec2(x, y + laneHeight * 0.5f), laneHeight * 0.32f, (batterieGold & 0x00ffffffu) | (alpha << 24));
            }
    }
    ImGui::Dummy(ImVec2(width, laneHeight * static_cast<float>(lanes.size()) + dp(8.0f)));
    // How many bars, which fill to end on, then in.
    ImGui::TextUnformatted(L("Mesures", "Bars"));
    ImGui::SameLine();
    for (const auto count : { 1, 2, 4, 8 }) {
        auto chosen = bars_ == count;
        ImGui::PushID(count + 1'000);
        if (ui::toggle(juce::String(count).toRawUTF8(), &chosen, batterieGold, ImVec2(dp(30.0f), 0.0f)))
            bars_ = count;
        ImGui::PopID();
        ImGui::SameLine(0.0f, dp(2.0f));
    }
    ImGui::SameLine(0.0f, dp(16.0f));
    ImGui::TextUnformatted(L("Fin", "Ending"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(230.0f));
    const auto fillName = fill_ < 0 ? juce::String(L("Sans roulement", "No fill"))
                                     : juce::String::fromUTF8((ui::french() ? list[static_cast<std::size_t>(fill_)].french
                                                                            : list[static_cast<std::size_t>(fill_)].english).c_str());
    if (ImGui::BeginCombo("##fill", fillName.toRawUTF8())) {
        if (ImGui::Selectable(L("Sans roulement", "No fill"), fill_ < 0))
            fill_ = -1;
        for (int at = 0; at < static_cast<int>(list.size()); ++at)
            if (list[static_cast<std::size_t>(at)].fill
                && ImGui::Selectable((ui::french() ? list[static_cast<std::size_t>(at)].french : list[static_cast<std::size_t>(at)].english).c_str(),
                                     fill_ == at))
                fill_ = at;
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, dp(16.0f));
    if (ui::primaryButton(L("Mettre sur la piste", "Put on the track"))) {
        juce::String error;
        const auto clip = studio_.addGrooveClip(card_.track, groove_, bars_, groove.fill ? -1 : fill_, -1.0, error);
        grooveReport_ = clip != 0 ? (ui::Ls("Clip ajouté : ", "Clip added: ") + juce::String::fromUTF8((ui::french() ? groove.french : groove.english).c_str())
                                     + " \xc3\x97 " + juce::String(bars_))
                                  : error;
    }
    if (grooveReport_.isNotEmpty())
        ImGui::TextDisabled("%s", grooveReport_.toRawUTF8());
}

void BatterieView::drawPane(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId, const ImVec2 origin,
                            const ImVec2 size, const ImU32 colour)
{
    if (!bind(track, graph, nodeId))
        return;
    (void) colour;
    ImGui::PushID(static_cast<int>(nodeId) + 43'000);
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginGroup();
    if (ui::primaryButton(L("Ouvrir Batterie", "Open Batterie"), ImVec2(dp(120.0f), 0.0f)))
        openEditor(track, nodeId);
    ImGui::SameLine();
    const auto kit = std::clamp(static_cast<int>(std::lround(value(drum::globalBase + drum::globalKit))), 0, static_cast<int>(drum::kits.size()) - 1);
    ImGui::TextDisabled("%s", L(drum::kits[static_cast<std::size_t>(kit)].french, drum::kits[static_cast<std::size_t>(kit)].english));
    const auto knobsHeight = dp(26.0f) + ui::currentFontBase() * 0.78f + dp(3.0f);
    const auto pictureOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 pictureSize(size.x, std::max(dp(36.0f), size.y - (pictureOrigin.y - origin.y) - knobsHeight - dp(6.0f)));
    drawKit(pictureOrigin, pictureSize, false);
    ImGui::SetCursorScreenPos(ImVec2(pictureOrigin.x, pictureOrigin.y + pictureSize.y + dp(4.0f)));
    const std::array<std::pair<Piece, const char*>, 6> levels { { { Piece::kick, "KICK" },
                                                                  { Piece::snare, L("CAISSE", "SNARE") },
                                                                  { Piece::hihat, "HH" },
                                                                  { Piece::tom1, "TOMS" },
                                                                  { Piece::crash, "CRASH" },
                                                                  { Piece::ride, "RIDE" } } };
    for (std::size_t at = 0; at < levels.size(); ++at) {
        if (at > 0)
            ImGui::SameLine(0.0f, dp(2.0f));
        knob(drum::index(levels[at].first, drum::fieldLevel), levels[at].second, 26.0f);
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(drum::globalBase + drum::globalTune, L("ACCORD", "TUNE"), 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(drum::globalBase + drum::globalLevel, L("NIVEAU", "LEVEL"), 26.0f);
    ImGui::EndGroup();
    ImGui::PopID();
}

void BatterieView::openEditor(const TrackRef& track, const std::uint64_t nodeId, const bool detached, const bool mics)
{
    editor_ = Target { track, nodeId };
    focusEditor_ = true;
    showMics_ = mics;
    if (detached)
        ui::setDetached(editorWindowId, true);
}

void BatterieView::drawEditor()
{
    if (!editor_)
        return;
    const auto target = *editor_;
    auto* slot = studio_.graphFor(target.track);
    if (slot == nullptr || !studio_.isValid(target.track) || !bind(target.track, slot->graph(), target.nodeId)) {
        editor_.reset();
        return;
    }
    const auto& p = theme::palette();
    ui::beforeDetachable(editorWindowId);
    ImGui::SetNextWindowSize(ImVec2(dp(1'180.0f), dp(760.0f)), ImGuiCond_FirstUseEver);
    if (focusEditor_) {
        ImGui::SetNextWindowFocus();
        focusEditor_ = false;
    }
    auto open = true;
    const auto title = juce::String::fromUTF8("Batterie \xe2\x80\x94 ") + studio_.trackName(target.track) + editorWindowId;
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, theme::mix(batterieGold, p.bg2 | 0xff000000u, 0.65f));
    const auto visible = ImGui::Begin(title.toRawUTF8(), &open, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();
    if (visible) {
        {
            const auto headerY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - dp(24.0f));
            (void) ui::detachButton(editorWindowId, ImVec2(dp(24.0f), dp(22.0f)));
            ImGui::SetCursorPosY(headerY);
        }
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextColored(theme::vec(batterieGold), "BATTERIE");
        ImGui::PopFont();
        ImGui::SameLine(0.0f, dp(16.0f));
        if (const auto holder = studio_.devicePresetHolder(target.track, target.nodeId)) {
            const auto track = target.track;
            const auto nodeId = target.nodeId;
            presets_.bar("batteriePresets", *holder, [this, track, nodeId] { return studio_.devicePresetState(track, nodeId); },
                         [this, track, nodeId](const juce::var& preset, juce::String& error) {
                             return studio_.applyDevicePreset(track, nodeId, preset, error);
                         },
                         dp(300.0f));
        }
        ImGui::SameLine(0.0f, dp(16.0f));
        kitChoice(dp(220.0f));
        ImGui::SameLine(0.0f, dp(16.0f));
        if (ImGui::Button(L("Initialiser le mixage", "Reset the mix"))) {
            const auto neutral = drum::neutralValues();
            for (int index = 0; index < drum::globalBase + drum::globalCount; ++index)
                if (index != drum::globalBase + drum::globalKit)
                    set(index, neutral[static_cast<std::size_t>(index)]);
        }
        ui::tooltip(L("Chaque élément à son niveau, au centre, accordé, sonnant comme enregistré (Ctrl+Z l'annule)",
                      "Every piece at its level, centred, in tune, ringing as recorded (Ctrl+Z undoes it)"));
        if (ImGui::BeginTabBar("##batterieTabs")) {
            if (ImGui::BeginTabItem("KIT")) {
                drawKitTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("CONSOLE", "MIXER"))) {
                drawConsoleTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("MOTIFS", "GROOVES"))) {
                drawGroovesTab();
                ImGui::EndTabItem();
            }
            // The kit's own controls (its microphones, its pieces' tuning
            // and damping), as its SFZ has them.
            const auto micsTab = ui::labelId(L("MICROS", "MICS"), "batterieMics");
            const auto showMics = std::exchange(showMics_, false);
            if (const auto has = sampler_controls::count(slot->graph(), target.nodeId); has.controls > 0
                && ImGui::BeginTabItem(micsTab.c_str(), nullptr, showMics ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (ImGui::BeginChild("##batterieMics", ImVec2(0.0f, 0.0f))) {
                    ImGui::TextColored(theme::vec(p.textDim), "%s",
                                       L("Les réglages du kit enregistré : le niveau de chaque micro (proche, ambiance, dessous), "
                                         "l'accord, l'étouffement… Ils s'ajoutent à la console.",
                                         "The recorded kit's own settings: each microphone's level (close, overheads, "
                                         "bottom), tuning, damping… They add to the mixer."));
                    sampler_controls::draw(studio_, state_, target.track, slot->graph(), target.nodeId, batterieGold);
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("EFFETS", "FX"))) {
                static constexpr instrument_effects::Choice effects[] = {
                    { "comp-pro", "Compresseur", "Compressor" }, { "eq-pro", "Égaliseur", "EQ" },
                    { "pression", "Pression", "Pression" },      { "fournaise", "Fournaise", "Fournaise" },
                    { "tranche", "Tranche", "Tranche" },         { "transient-shaper", "Transitoires", "Transients" },
                    { "patine", "Patine", "Patine" },            { "halo", "Halo", "Halo" },
                    { "limiter", "Limiteur", "Limiter" },
                };
                ImGui::BeginChild("##batterieEffects", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                instrument_effects::draw(studio_, state_, target.track, "batterie", "Batterie", effects, batterieGold);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    if (!open)
        editor_.reset();
}

} // namespace cydaw::v2
