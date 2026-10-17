#include "PrismView.h"
#include "InstrumentPanels.h"

#include "Icons.h"
#include "InstrumentEffects.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <unordered_map>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
namespace prism = engine::prism;

namespace {

constexpr const char* sourcePayload = "PRISM_SOURCE";
constexpr ImU32 prismOrange = IM_COL32(0xff, 0x8a, 0x3d, 0xff);

// The colour of each modulation source, on chips, rings and the matrix.
ImU32 sourceColour(const int source)
{
    using S = prism::Source;
    const auto s = static_cast<S>(source);
    if (s >= S::env1 && s <= S::env4)
        return IM_COL32(0x4f, 0xc3, 0xf7, 0xff);
    if (s >= S::lfo1 && s <= S::lfo4)
        return IM_COL32(0xb3, 0x88, 0xff, 0xff);
    if (s >= S::macro1 && s <= S::macro4)
        return IM_COL32(0xff, 0xd5, 0x4f, 0xff);
    return IM_COL32(0x81, 0xe6, 0x8f, 0xff);
}

juce::String sourceName(const int source)
{
    const auto& choices = prism::parameter(prism::slotBase(0) + prism::slotSource).choices;
    if (source < 0 || source >= static_cast<int>(choices.size()))
        return {};
    return juce::String::fromUTF8((ui::french() ? choices[static_cast<std::size_t>(source)].first
                                                : choices[static_cast<std::size_t>(source)].second)
                                      .c_str());
}

juce::String parameterName(const int index)
{
    const auto& p = prism::parameter(index);
    return juce::String::fromUTF8((ui::french() ? p.french : p.english).c_str());
}

// Where a parameter sits in the slots' destination list (0: none).
int destinationOf(const int index)
{
    static const auto table = [] {
        std::unordered_map<int, int> map;
        const auto& list = prism::destinations();
        for (std::size_t at = 1; at < list.size(); ++at)
            map[list[at]] = static_cast<int>(at);
        return map;
    }();
    const auto found = table.find(index);
    return found != table.end() ? found->second : 0;
}

double bend(const double t, const double curve)
{
    const auto x = std::clamp(t, 0.0, 1.0);
    if (curve > 0.0)
        return 1.0 - std::pow(1.0 - x, 1.0 + curve * 4.0);
    if (curve < 0.0)
        return std::pow(x, 1.0 - curve * 4.0);
    return x;
}

const float kArcStart = std::numbers::pi_v<float> * 0.75f;
const float kArcSweep = std::numbers::pi_v<float> * 1.5f;

void arc(ImDrawList* list, const ImVec2 centre, const float radius, const float from, const float to, const ImU32 colour,
         const float thickness)
{
    if (std::abs(to - from) < 1.0e-4f)
        return;
    list->PathArcTo(centre, radius, std::min(from, to), std::max(from, to), 24);
    list->PathStroke(colour, thickness);
}

} // namespace

PrismView::PrismView(DeviceHost& studio, UiState& state) : studio_(studio), state_(state), presets_(studio, state) {}

float PrismView::paneWidth()
{
    return dp(330.0f);
}

bool PrismView::bind(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId)
{
    const auto values = graph.editorPrismValues(nodeId);
    if (!values)
        return false;
    card_.track = track;
    card_.graph = &graph;
    card_.nodeId = nodeId;
    card_.values = *values;
    card_.own = graph.nodePrismTables(nodeId);
    card_.live = studio_.voiceLive(track, card_.heard);
    // As it plays: the lanes at the playhead, then the voice's modulations.
    card_.played = card_.values;
    const auto prefix = "node." + juce::String(static_cast<juce::int64>(nodeId)) + ".prism.";
    for (const auto& binding : studio_.automatedParameters(track))
        if (binding.startsWith(prefix))
            if (const auto index = prism::parameterIndex(binding.substring(prefix.length()).toStdString()); index >= 0)
                if (const auto now = studio_.laneValueNow(track, binding))
                    card_.played[static_cast<std::size_t>(index)] = prism::clampValue(index, *now);
    if (card_.live)
        for (int index = 0; index < prism::parameterCount; ++index)
            if (const auto now = card_.heard.values[static_cast<std::size_t>(index)]; std::isfinite(now))
                card_.played[static_cast<std::size_t>(index)] = now;
    return true;
}

std::optional<float> PrismView::heard(const int index) const
{
    if (index < 0 || index >= prism::parameterCount)
        return std::nullopt;
    const auto played = card_.played[static_cast<std::size_t>(index)];
    return played != card_.values[static_cast<std::size_t>(index)] ? std::optional<float>(played) : std::nullopt;
}

prism::WavetableRef PrismView::tableOf(const int osc) const
{
    if (card_.own != nullptr)
        if (const auto& own = card_.own->oscillator[static_cast<std::size_t>(std::clamp(osc, 0, prism::oscillatorCount - 1))])
            return own;
    return prism::factoryTable(static_cast<int>(std::lround(value(prism::oscBase(osc) + prism::oscTable))));
}

void PrismView::chooseTableFile(const int osc)
{
    const auto track = card_.track;
    const auto nodeId = card_.nodeId;
    chooser_ = std::make_unique<juce::FileChooser>(
        ui::Ls("Une table d'ondes (ou un son) pour l'oscillateur ", "A wavetable (or a sound) for oscillator ") + (osc == 0 ? "A" : "B"),
        juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, track, nodeId, osc](const juce::FileChooser& chooser) {
                              const auto file = chooser.getResult();
                              if (file == juce::File())
                                  return;
                              juce::String error;
                              const auto done = studio_.importPrismTable(track, nodeId, osc, file, error);
                              state_.status = done ? ui::Ls("Table importée : ", "Table imported: ") + file.getFileName() : error;
                              state_.statusIsError = !done;
                              state_.statusTime = ImGui::GetTime();
                          });
}

float PrismView::value(const int index) const
{
    return card_.values[static_cast<std::size_t>(std::clamp(index, 0, prism::parameterCount - 1))];
}

void PrismView::set(const int index, const float newValue)
{
    if (card_.graph == nullptr)
        return;
    const auto clamped = prism::clampValue(index, newValue);
    card_.values[static_cast<std::size_t>(index)] = clamped;
    const auto binding = "node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".prism."
                         + juce::String(prism::parameter(index).key);
    (void) card_.graph->editorSetParameter(binding, clamped);
    state_.focused = Panel::devices;
}

float PrismView::modulationRange(const int index, float& low, float& high) const
{
    // Every slot that moves it: how far up and down, from the knob.
    low = 0.0f;
    high = 0.0f;
    const auto destination = destinationOf(index);
    if (destination == 0)
        return 0.0f;
    auto total = 0.0f;
    for (int slot = 0; slot < prism::slotCount; ++slot) {
        if (static_cast<int>(std::lround(value(prism::slotBase(slot) + prism::slotDestination))) != destination
            || static_cast<int>(std::lround(value(prism::slotBase(slot) + prism::slotSource))) <= 0)
            continue;
        const auto amount = value(prism::slotBase(slot) + prism::slotAmount);
        // A bipolar source swings both ways; the envelopes, macros and the
        // like go one way.
        const auto source = static_cast<prism::Source>(std::lround(value(prism::slotBase(slot) + prism::slotSource)));
        const auto lfo = source >= prism::Source::lfo1 && source <= prism::Source::lfo4;
        const auto bipolar = (lfo && value(prism::lfoBase(static_cast<int>(source) - static_cast<int>(prism::Source::lfo1))
                                           + prism::lfoUnipolar) < 0.5f)
                             || source == prism::Source::pitchBend || source == prism::Source::note;
        high += std::max(0.0f, amount);
        low += std::min(0.0f, amount);
        if (bipolar) {
            high += std::max(0.0f, -amount);
            low += std::min(0.0f, -amount);
        }
        total += std::abs(amount);
    }
    return total;
}

void PrismView::addModulation(const int source, const int index, const float amount)
{
    const auto destination = destinationOf(index);
    if (destination == 0 || source <= 0)
        return;
    // The same source on the same parameter: that slot; else the first free.
    auto free = -1;
    for (int slot = 0; slot < prism::slotCount; ++slot) {
        const auto slotSource = static_cast<int>(std::lround(value(prism::slotBase(slot) + prism::slotSource)));
        const auto slotDestination = static_cast<int>(std::lround(value(prism::slotBase(slot) + prism::slotDestination)));
        if (slotSource == source && slotDestination == destination) {
            set(prism::slotBase(slot) + prism::slotAmount, amount);
            return;
        }
        if (free < 0 && (slotSource == 0 || slotDestination == 0))
            free = slot;
    }
    if (free < 0) {
        state_.status = ui::Ls("Les 16 modulations sont prises (onglet Matrice)", "The 16 modulations are taken (Matrix tab)");
        state_.statusIsError = true;
        state_.statusTime = ImGui::GetTime();
        return;
    }
    set(prism::slotBase(free) + prism::slotSource, static_cast<float>(source));
    set(prism::slotBase(free) + prism::slotDestination, static_cast<float>(destination));
    set(prism::slotBase(free) + prism::slotAmount, amount);
}

void PrismView::modulationMenu(const int index)
{
    // A source dropped on the knob.
    if (destinationOf(index) != 0 && ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(sourcePayload))
            addModulation(*static_cast<const int*>(payload->Data), index, 0.35f);
        ImGui::EndDragDropTarget();
    }
    if (!ImGui::BeginPopupContextItem("##modulation"))
        return;
    ImGui::TextDisabled("%s", parameterName(index).toRawUTF8());
    if (ImGui::MenuItem(L("Valeur par défaut", "Default value")))
        set(index, prism::parameter(index).defaultValue);
    // A lane in the arrangement, as any card's knob.
    const auto binding = "node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".prism."
                         + juce::String(prism::parameter(index).key);
    if (studio_.isValid(card_.track) && studio_.canAutomate(card_.track, binding)) {
        const auto automated = studio_.hasLane(card_.track, binding);
        if (ImGui::MenuItem(automated ? L("Afficher l'automation", "Show the automation")
                                      : L("Automatiser dans l'arrangement", "Automate in the arrangement"))) {
            studio_.setShownLane(card_.track, binding);
            studio_.setAutomationVisible(card_.track, true);
            state_.selectTrack(card_.track);
            state_.bringToFront = Panel::arrangement;
        }
        if (automated && ImGui::MenuItem(L("Effacer l'automation", "Clear the automation")))
            studio_.clearLane(card_.track, binding);
    }
    const auto destination = destinationOf(index);
    if (destination != 0) {
        ImGui::Separator();
        if (ImGui::BeginMenu(L("Moduler par", "Modulate by"))) {
            for (int source = 1; source < static_cast<int>(prism::Source::count); ++source)
                if (ImGui::MenuItem(sourceName(source).toRawUTF8()))
                    addModulation(source, index, 0.35f);
            ImGui::EndMenu();
        }
        // What already moves it: its amount, or out.
        for (int slot = 0; slot < prism::slotCount; ++slot) {
            const auto base = prism::slotBase(slot);
            const auto source = static_cast<int>(std::lround(value(base + prism::slotSource)));
            if (source <= 0 || static_cast<int>(std::lround(value(base + prism::slotDestination))) != destination)
                continue;
            ImGui::PushID(slot);
            ImGui::PushStyleColor(ImGuiCol_Text, sourceColour(source));
            ImGui::TextUnformatted(sourceName(source).toRawUTF8());
            ImGui::PopStyleColor();
            ImGui::SameLine(dp(90.0f));
            auto amount = value(base + prism::slotAmount) * 100.0f;
            ImGui::SetNextItemWidth(dp(120.0f));
            if (ImGui::SliderFloat("##amount", &amount, -100.0f, 100.0f, "%.0f %%"))
                set(base + prism::slotAmount, amount / 100.0f);
            ImGui::SameLine();
            if (ImGui::SmallButton(icon::close)) {
                set(base + prism::slotSource, 0.0f);
                set(base + prism::slotDestination, 0.0f);
                set(base + prism::slotAmount, 0.0f);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndPopup();
}

bool PrismView::knob(const int index, const char* label, const float diameter)
{
    const auto& p = theme::palette();
    const auto& parameter = prism::parameter(index);
    ImGui::PushID(index);
    // One group: the knob, its name under it; a row of them stays a row.
    ImGui::BeginGroup();
    auto normalised = prism::normalise(index, value(index));
    const auto defaultNormalised = prism::normalise(index, parameter.defaultValue);
    ui::KnobStyle style;
    style.diameter = diameter;
    style.flags = ui::knobNoLabel;
    style.colour = prismOrange;
    if (parameter.minimum < 0.0f && std::abs(parameter.minimum + parameter.maximum) < 1.0e-6f)
        style.flags |= ui::knobBipolar;
    // A whole number (voices, octaves): the knob lands on each.
    if (parameter.kind != prism::Kind::continuous)
        style.steps = static_cast<int>(std::lround(parameter.maximum - parameter.minimum));
    // Where the automation and the newest note's modulations put it now.
    if (const auto moved = heard(index))
        style.modulation = prism::normalise(index, *moved);
    const auto origin = ImGui::GetCursorScreenPos();
    bool changed {};
    {
        const ui::LearnParameterScope learn("node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".prism."
                                                + juce::String(parameter.key),
                                            juce::String::fromUTF8(label), studio_.learnPrefix(card_.track), studio_.trackName(card_.track));
        changed = ui::knob("##knob", &normalised, 0.0f, 1.0f, defaultNormalised, style);
    }
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    if (changed)
        set(index, prism::denormalise(index, normalised));
    modulationMenu(index);
    if (hovered && !active)
        ImGui::SetTooltip("%s : %s\n%s", parameterName(index).toRawUTF8(),
                          prism::displayValue(index, value(index), ui::french()).c_str(),
                          L("Double-clic : défaut · clic droit : moduler · glisser une source dessus",
                            "Double click: default · right click: modulate · drop a source on it"));
    const auto width = std::max(dp(diameter), dp(diameter + 12.0f));
    // The modulation's ring, outside the knob.
    auto low = 0.0f;
    auto high = 0.0f;
    if (modulationRange(index, low, high) > 0.0f) {
        const ImVec2 centre(origin.x + width * 0.5f, origin.y + dp(diameter) * 0.5f);
        const auto radius = dp(diameter) * 0.5f + dp(2.5f);
        const auto from = kArcStart + kArcSweep * std::clamp(normalised + low, 0.0f, 1.0f);
        const auto to = kArcStart + kArcSweep * std::clamp(normalised + high, 0.0f, 1.0f);
        arc(ImGui::GetWindowDrawList(), centre, radius, from, to, p.modulation, dp(2.2f));
    }
    // Its name, its value while touched.
    const auto text = hovered || active ? juce::String::fromUTF8(prism::displayValue(index, value(index), ui::french()).c_str())
                                        : juce::String::fromUTF8(label != nullptr ? label : "");
    auto* list = ImGui::GetWindowDrawList();
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    const auto textHeight = ImGui::GetFontSize();
    const auto size = ImGui::CalcTextSize(text.toRawUTF8());
    const auto y = origin.y + dp(diameter) + dp(1.0f);
    list->PushClipRect(ImVec2(origin.x - dp(8.0f), y), ImVec2(origin.x + width + dp(8.0f), y + textHeight + dp(2.0f)), true);
    list->AddText(ImVec2(origin.x + (width - size.x) * 0.5f, y), hovered || active ? p.text : p.textDim, text.toRawUTF8());
    list->PopClipRect();
    ImGui::PopFont();
    // Its name, or its number: a click on it types the value in.
    auto typed = static_cast<double>(value(index));
    if (ui::valueEntryButton("##number", ImVec2(width, textHeight + dp(2.0f)), typed, parameter.minimum, parameter.maximum,
                             parameter.unit == "%" ? 100.0 : 1.0, parameter.unit.c_str()))
        set(index, static_cast<float>(typed));
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

void PrismView::toggle(const int index, const char* label, const ImVec2 size)
{
    ImGui::PushID(index);
    auto on = value(index) >= 0.5f;
    if (ui::toggle(label, &on, prismOrange, size, parameterName(index).toRawUTF8()))
        set(index, on ? 1.0f : 0.0f);
    ImGui::PopID();
}

void PrismView::choice(const int index, const float width)
{
    const auto& parameter = prism::parameter(index);
    ImGui::PushID(index);
    const auto current = std::clamp(static_cast<int>(std::lround(value(index))), 0, static_cast<int>(parameter.choices.size()) - 1);
    ImGui::SetNextItemWidth(width);
    const auto shown = prism::displayValue(index, static_cast<float>(current), ui::french());
    if (ImGui::BeginCombo("##choice", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int at = 0; at < static_cast<int>(parameter.choices.size()); ++at) {
            const auto name = prism::displayValue(index, static_cast<float>(at), ui::french());
            if (ImGui::Selectable(name.c_str(), at == current))
                set(index, static_cast<float>(at));
        }
        ImGui::EndCombo();
    }
    ui::tooltip(parameterName(index).toRawUTF8());
    ImGui::PopID();
}

void PrismView::integer(const int index, const char* label, const float width)
{
    const auto& parameter = prism::parameter(index);
    ImGui::PushID(index);
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    ImGui::TextDisabled("%s", label);
    ImGui::PopFont();
    auto current = static_cast<int>(std::lround(value(index)));
    ImGui::SetNextItemWidth(width);
    if (ImGui::DragInt("##integer", &current, 0.08f, static_cast<int>(parameter.minimum), static_cast<int>(parameter.maximum),
                       "%d", ImGuiSliderFlags_AlwaysClamp))
        set(index, static_cast<float>(current));
    ImGui::EndGroup();
    ui::tooltip(parameterName(index).toRawUTF8());
    ImGui::PopID();
}

void PrismView::sourceChip(const int source, const char* label, const ImVec2 size, const bool selected, bool* clicked)
{
    const auto& p = theme::palette();
    ImGui::PushID(source + 7'000);
    const auto colour = sourceColour(source);
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? theme::mix(colour, p.bg2 | 0xff000000u, 0.45f) : p.bg3);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? p.text : colour);
    if (ImGui::Button(label, size) && clicked != nullptr)
        *clicked = true;
    ImGui::PopStyleColor(2);
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload(sourcePayload, &source, sizeof(source));
        ImGui::Text("%s → %s", label, L("un potard", "a knob"));
        ImGui::EndDragDropSource();
    }
    ui::tooltip(L("Glisser sur un potard pour qu'il le module", "Drag onto a knob to modulate it"));
    ImGui::PopID();
}

// ---- Displays ------------------------------------------------------------------------------

void PrismView::drawWavetable(const int osc, const ImVec2 origin, const ImVec2 size, const bool compact)
{
    // As it plays now (its position automated, swept): the settings played
    // stand in for the knobs while it is drawn.
    struct Playing {
        Card& card;
        explicit Playing(Card& c) : card(c) { std::swap(card.values, card.played); }
        ~Playing() { std::swap(card.values, card.played); }
    } playing { card_ };
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(4.0f));
    const auto base = prism::oscBase(osc);
    const auto table = tableOf(osc);
    if (table == nullptr || table->frameCount <= 0)
        return;
    const auto on = value(base + prism::oscOn) >= 0.5f;
    const auto frames = table->frameCount;
    const auto position = value(base + prism::oscPosition) * static_cast<float>(frames - 1);
    // The frames stacked into the depth, the one playing in front.
    const auto depthX = compact ? 0.0f : size.x * 0.22f;
    const auto depthY = compact ? 0.0f : size.y * 0.32f;
    const auto plotWidth = size.x - depthX - dp(12.0f);
    const auto plotHeight = (size.y - depthY) * 0.42f;
    constexpr int points = 96;
    const auto draw = [&](const float frame, const ImU32 colour, const float thickness) {
        const auto t = frames > 1 ? frame / static_cast<float>(frames - 1) : 0.0f;
        const ImVec2 start(origin.x + dp(6.0f) + depthX * (1.0f - t), origin.y + depthY * t + (size.y - depthY) * 0.5f);
        const auto f0 = std::clamp(static_cast<int>(frame), 0, frames - 1);
        const auto f1 = std::min(f0 + 1, frames - 1);
        const auto mix = frame - static_cast<float>(f0);
        const auto* a = table->frame(f0, 2);
        const auto* b = table->frame(f1, 2);
        ImVec2 line[points];
        for (int i = 0; i < points; ++i) {
            const auto index = i * prism::Wavetable::frameLength / points;
            const auto sample = a[index] + (b[index] - a[index]) * mix;
            line[i] = ImVec2(start.x + plotWidth * static_cast<float>(i) / (points - 1), start.y - sample * plotHeight);
        }
        list->AddPolyline(line, points, colour, thickness);
    };
    if (!compact) {
        const auto step = std::max(1, frames / 24);
        for (int frame = frames - 1; frame >= 0; frame -= step)
            draw(static_cast<float>(frame), theme::alpha(p.textFaint, 0.22f), dp(1.0f));
    }
    draw(position, on ? prismOrange : p.textFaint, dp(compact ? 1.6f : 2.2f));
    // Its name and where it reads.
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.76f);
    char text[96];
    std::snprintf(text, sizeof(text), "%s  ·  %d/%d", table->name.c_str(), static_cast<int>(std::lround(position)) + 1, frames);
    list->AddText(ImVec2(origin.x + dp(5.0f), origin.y + dp(3.0f)), p.textDim, text);
    ImGui::PopFont();
}

void PrismView::drawFilterResponse(const ImVec2 origin, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    if (!drawingHeard_)
        list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(4.0f));
    const auto on = value(prism::filterBase + prism::filterOn) >= 0.5f;
    // Grid: octaves.
    if (!drawingHeard_) {
        for (double f = 100.0; f < 20'000.0; f *= 10.0) {
            const auto x = origin.x + size.x * static_cast<float>(std::log(f / 20.0) / std::log(1'000.0));
            list->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), p.grid);
        }
        const auto zeroY = origin.y + size.y * (18.0f / 54.0f);
        list->AddLine(ImVec2(origin.x, zeroY), ImVec2(origin.x + size.x, zeroY), p.grid);
    }
    constexpr int points = 120;
    ImVec2 line[points];
    for (int i = 0; i < points; ++i) {
        const auto f = 20.0 * std::pow(1'000.0, static_cast<double>(i) / (points - 1));
        const auto db=instrument_ui::filterResponse(card_.values,f);
        line[i] = ImVec2(origin.x + size.x * static_cast<float>(i) / (points - 1),
                         origin.y + size.y * static_cast<float>((18.0 - db) / 54.0));
    }
    list->AddPolyline(line, points, drawingHeard_ ? theme::alpha(p.modulation, 0.9f) : on ? prismOrange : p.textFaint,
                      drawingHeard_ ? dp(1.4f) : dp(2.0f));
    // Where the newest note's modulations put it now.
    if (!drawingHeard_ && on && card_.live) {
        auto moved = card_.values;
        auto any = false;
        for (const auto index : { prism::filterBase + prism::filterCutoff, prism::filterBase + prism::filterResonance,
                                  prism::filterBase + prism::filterVariation, prism::filterBase + prism::filterMix })
            if (const auto now = heard(index)) {
                moved[static_cast<std::size_t>(index)] = *now;
                any = true;
            }
        if (any) {
            std::swap(card_.values, moved);
            drawingHeard_ = true;
            drawFilterResponse(origin, size);
            drawingHeard_ = false;
            std::swap(card_.values, moved);
        }
    }
}

void PrismView::drawEnvelope(const int env, const ImVec2 origin, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(4.0f));
    const auto base = prism::envBase(env);
    const auto attack = std::max(0.0005, static_cast<double>(value(base + prism::envAttack)));
    const auto hold = static_cast<double>(value(base + prism::envHold));
    const auto decay = std::max(0.0005, static_cast<double>(value(base + prism::envDecay)));
    const auto sustain = static_cast<double>(value(base + prism::envSustain));
    const auto release = std::max(0.0005, static_cast<double>(value(base + prism::envRelease)));
    // A stretch of sustain between the decay and the release.
    const auto held = std::max(0.25, (attack + hold + decay) * 0.35);
    const auto total = attack + hold + decay + held + release;
    const auto top = origin.y + dp(6.0f);
    const auto bottom = origin.y + size.y - dp(4.0f);
    const auto x = [&](const double seconds) {
        return origin.x + dp(4.0f) + (size.x - dp(8.0f)) * static_cast<float>(std::pow(seconds / total, 0.6));
    };
    const auto y = [&](const double level) { return bottom - (bottom - top) * static_cast<float>(level); };
    std::vector<ImVec2> line;
    constexpr int steps = 24;
    for (int i = 0; i <= steps; ++i) {
        const auto t = static_cast<double>(i) / steps;
        line.emplace_back(x(attack * t), y(bend(t, value(base + prism::envAttackCurve))));
    }
    line.emplace_back(x(attack + hold), y(1.0));
    for (int i = 1; i <= steps; ++i) {
        const auto t = static_cast<double>(i) / steps;
        line.emplace_back(x(attack + hold + decay * t),
                          y(sustain + (1.0 - sustain) * (1.0 - bend(t, value(base + prism::envDecayCurve)))));
    }
    line.emplace_back(x(attack + hold + decay + held), y(sustain));
    for (int i = 1; i <= steps; ++i) {
        const auto t = static_cast<double>(i) / steps;
        line.emplace_back(x(attack + hold + decay + held + release * t),
                          y(sustain * (1.0 - bend(t, value(base + prism::envReleaseCurve)))));
    }
    std::vector<ImVec2> fill = line;
    fill.emplace_back(line.back().x, bottom);
    fill.emplace_back(line.front().x, bottom);
    list->AddConcavePolyFilled(fill.data(), static_cast<int>(fill.size()), theme::alpha(sourceColour(1 + env), 0.18f));
    list->AddPolyline(line.data(), static_cast<int>(line.size()), sourceColour(1 + env), dp(2.0f));
    // Where the newest note's envelope is now: a line at its level.
    if (card_.live && env >= 0 && static_cast<std::size_t>(env) < card_.heard.envelopes.size())
        if (const auto now = card_.heard.envelopes[static_cast<std::size_t>(env)]; std::isfinite(now)) {
            const auto at = y(std::clamp(static_cast<double>(now), 0.0, 1.0));
            list->AddLine(ImVec2(origin.x + dp(4.0f), at), ImVec2(origin.x + size.x - dp(4.0f), at), theme::alpha(p.modulation, 0.55f),
                          dp(1.0f));
            list->AddCircleFilled(ImVec2(origin.x + size.x - dp(5.0f), at), dp(3.5f), p.modulation);
        }
}

void PrismView::drawLfo(const int lfo, const ImVec2 origin, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(4.0f));
    const auto base = prism::lfoBase(lfo);
    const auto shape = static_cast<prism::LfoShape>(std::lround(value(base + prism::lfoShape)));
    const auto phaseOffset = static_cast<double>(value(base + prism::lfoPhase));
    const auto unipolar = value(base + prism::lfoUnipolar) >= 0.5f;
    const auto top = origin.y + dp(6.0f);
    const auto bottom = origin.y + size.y - dp(6.0f);
    const auto middle = unipolar ? bottom : (top + bottom) * 0.5f;
    list->AddLine(ImVec2(origin.x, middle), ImVec2(origin.x + size.x, middle), p.grid);
    // Two cycles (one for a drawn shape, the one being drawn); the random
    // shapes from a fixed draw.
    constexpr int points = 160;
    ImVec2 line[points];
    const double randoms[6] = { 0.4, -0.8, 0.9, -0.2, 0.6, -0.5 };
    const auto drawn = shape == prism::LfoShape::drawn;
    std::array<float, prism::drawPoints> drawnPoints {};
    for (int point = 0; point < prism::drawPoints; ++point)
        drawnPoints[static_cast<std::size_t>(point)] = value(prism::drawBase(lfo) + point);
    for (int i = 0; i < points; ++i) {
        const auto cycles = (drawn ? 1.0 : 2.0) * i / (points - 1) + (drawn ? 0.0 : phaseOffset);
        const auto cycle = static_cast<int>(std::floor(cycles));
        const auto phase = cycles - cycle;
        auto v = 0.0;
        switch (shape) {
        case prism::LfoShape::sine: v = std::sin(2.0 * std::numbers::pi * phase); break;
        case prism::LfoShape::triangle: v = 1.0 - 4.0 * std::abs(phase - 0.5); break;
        case prism::LfoShape::sawUp: v = 2.0 * phase - 1.0; break;
        case prism::LfoShape::sawDown: v = 1.0 - 2.0 * phase; break;
        case prism::LfoShape::square: v = phase < 0.5 ? 1.0 : -1.0; break;
        case prism::LfoShape::sampleHold: v = randoms[static_cast<std::size_t>(cycle % 6)]; break;
        case prism::LfoShape::smoothRandom: {
            const auto t = 0.5 - 0.5 * std::cos(std::numbers::pi * phase);
            const auto a = randoms[static_cast<std::size_t>(cycle % 6)];
            const auto b = randoms[static_cast<std::size_t>((cycle + 1) % 6)];
            v = a + (b - a) * t;
            break;
        }
        case prism::LfoShape::exponential: v = 2.0 * std::exp(-5.0 * phase) - 1.0; break;
        case prism::LfoShape::drawn: v = prism::drawnLfo(drawnPoints.data(), i == points - 1 ? 0.0 : phase); break;
        case prism::LfoShape::count: break;
        }
        if (unipolar)
            v = 0.5 * (v + 1.0);
        const auto level = unipolar ? v : 0.5 * (v + 1.0);
        line[i] = ImVec2(origin.x + dp(3.0f) + (size.x - dp(6.0f)) * static_cast<float>(i) / (points - 1),
                         bottom - (bottom - top) * static_cast<float>(level));
    }
    list->AddPolyline(line, points, sourceColour(5 + lfo), dp(2.0f));
    // Where the newest note's LFO is now.
    if (card_.live && lfo >= 0 && static_cast<std::size_t>(lfo) < card_.heard.lfos.size()) {
        const auto now = card_.heard.lfos[static_cast<std::size_t>(lfo)];
        const auto phase = card_.heard.lfoPhases[static_cast<std::size_t>(lfo)];
        if (std::isfinite(now) && std::isfinite(phase)) {
            auto along = static_cast<double>(phase);
            if (!drawn) {
                const auto cycles = along >= phaseOffset ? along : along + 1.0;
                along = (cycles - phaseOffset) * 0.5;
            }
            const auto level = unipolar ? static_cast<double>(now) : 0.5 * (static_cast<double>(now) + 1.0);
            const ImVec2 at(origin.x + dp(3.0f) + (size.x - dp(6.0f)) * static_cast<float>(std::clamp(along, 0.0, 1.0)),
                            bottom - (bottom - top) * static_cast<float>(std::clamp(level, 0.0, 1.0)));
            list->AddCircleFilled(at, dp(4.0f), p.modulation);
            list->AddCircle(at, dp(4.0f), p.bg0, 0, dp(1.0f));
        }
    }
    // The points one draws, on the line.
    if (drawn)
        for (int point = 0; point < prism::drawPoints; ++point) {
            const auto v = static_cast<double>(drawnPoints[static_cast<std::size_t>(point)]);
            const auto level = unipolar ? 0.5 * (v + 1.0) : 0.5 * (v + 1.0);
            const ImVec2 at(origin.x + dp(3.0f) + (size.x - dp(6.0f)) * static_cast<float>(point) / prism::drawPoints,
                            bottom - (bottom - top) * static_cast<float>(level));
            list->AddCircleFilled(at, dp(2.4f), theme::brighter(sourceColour(5 + lfo), 0.3f));
        }
}

void PrismView::editDrawnLfo(const int lfo, const ImVec2 origin, const ImVec2 size)
{
    const auto base = prism::drawBase(lfo);
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushID(lfo + 700);
    ImGui::InvisibleButton("##drawLfo", size);
    const auto top = origin.y + dp(6.0f);
    const auto bottom = origin.y + size.y - dp(6.0f);
    const auto left = origin.x + dp(3.0f);
    const auto width = size.x - dp(6.0f);
    // Where the mouse is: a point and its value (-1..1, the line's height;
    // unipolar, the same: it reads the point as 0..1).
    const auto mouse = ImGui::GetIO().MousePos;
    const auto point = std::clamp(static_cast<int>(std::floor((mouse.x - left) / std::max(1.0f, width) * prism::drawPoints + 0.5f)), 0,
                                  prism::drawPoints - 1);
    const auto level = std::clamp((bottom - mouse.y) / std::max(1.0f, bottom - top), 0.0f, 1.0f);
    const auto wanted = std::clamp(2.0f * level - 1.0f, -1.0f, 1.0f);
    if (ImGui::IsItemActive()) {
        // Every point between the last and this one, on a straight line:
        // a fast stroke leaves no gap.
        const auto from = drawnLast_ < 0 ? point : drawnLast_;
        const auto fromValue = drawnLast_ < 0 ? wanted : drawnLastValue_;
        const auto steps = std::abs(point - from);
        for (int step = 0; step <= steps; ++step) {
            const auto at = from + (point > from ? step : -step);
            const auto t = steps == 0 ? 1.0f : static_cast<float>(step) / static_cast<float>(steps);
            const auto target = fromValue + (wanted - fromValue) * t;
            if (std::abs(value(base + at) - target) > 1.0e-4f)
                set(base + at, target);
        }
        drawnLast_ = point;
        drawnLastValue_ = wanted;
    } else {
        drawnLast_ = -1;
    }
    if (ImGui::IsItemHovered())
        ui::tooltip(L("Dessinez la forme du LFO : glissez sur la courbe. Clic droit : des formes toutes faites.",
                      "Draw the LFO's shape: drag over the curve. Right click: shapes ready made."));
    // Shapes ready made, and what to do to the one drawn.
    if (ImGui::BeginPopupContextItem("##drawnShapes")) {
        std::array<float, prism::drawPoints> shape {};
        for (int i = 0; i < prism::drawPoints; ++i)
            shape[static_cast<std::size_t>(i)] = value(base + i);
        auto changed = false;
        const auto fill = [&](const auto& function) {
            for (int i = 0; i < prism::drawPoints; ++i)
                shape[static_cast<std::size_t>(i)] = std::clamp(static_cast<float>(function(static_cast<double>(i) / prism::drawPoints, i)),
                                                                -1.0f, 1.0f);
            changed = true;
        };
        if (ImGui::MenuItem(L("Sinus", "Sine")))
            fill([](const double x, int) { return std::sin(2.0 * std::numbers::pi * x); });
        if (ImGui::MenuItem(L("Triangle", "Triangle")))
            fill([](const double x, int) { return 1.0 - 4.0 * std::abs(x - 0.5); });
        if (ImGui::MenuItem(L("Rampe montante", "Saw up")))
            fill([](const double x, int) { return 2.0 * x - 1.0; });
        if (ImGui::MenuItem(L("Rampe descendante", "Saw down")))
            fill([](const double x, int) { return 1.0 - 2.0 * x; });
        if (ImGui::MenuItem(L("Carré", "Square")))
            fill([](const double x, int) { return x < 0.5 ? 1.0 : -1.0; });
        if (ImGui::MenuItem(L("Escalier (8 marches)", "Steps (8)")))
            fill([](const double x, int) { return std::floor(x * 8.0) / 3.5 - 1.0; });
        if (ImGui::MenuItem(L("Pompe (sidechain)", "Pump (sidechain)")))
            fill([](const double x, int) { return 2.0 * std::min(1.0, x * 4.0) * std::min(1.0, x * 4.0) - 1.0; });
        if (ImGui::MenuItem(L("Aléatoire", "Random"))) {
            auto random = juce::Random::getSystemRandom();
            fill([&random](double, int) { return random.nextDouble() * 2.0 - 1.0; });
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Lisser", "Smooth"))) {
            const auto before = shape;
            for (int i = 0; i < prism::drawPoints; ++i)
                shape[static_cast<std::size_t>(i)] = 0.25f * before[static_cast<std::size_t>((i + prism::drawPoints - 1) % prism::drawPoints)]
                                                     + 0.5f * before[static_cast<std::size_t>(i)]
                                                     + 0.25f * before[static_cast<std::size_t>((i + 1) % prism::drawPoints)];
            changed = true;
        }
        if (ImGui::MenuItem(L("Inverser haut et bas", "Flip up and down"))) {
            for (auto& v : shape)
                v = -v;
            changed = true;
        }
        if (ImGui::MenuItem(L("À l'envers", "Backwards"))) {
            std::reverse(shape.begin() + 1, shape.end());
            changed = true;
        }
        if (ImGui::MenuItem(L("Remettre à zéro", "Flat at zero"))) {
            shape.fill(0.0f);
            changed = true;
        }
        if (changed)
            for (int i = 0; i < prism::drawPoints; ++i)
                if (std::abs(value(base + i) - shape[static_cast<std::size_t>(i)]) > 1.0e-5f)
                    set(base + i, shape[static_cast<std::size_t>(i)]);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

// ---- Sections --------------------------------------------------------------------------------

void PrismView::drawOscillator(const int osc, const float width, const float height)
{
    const auto& p = theme::palette();
    const auto base = prism::oscBase(osc);
    ImGui::PushID(osc + 100);
    ImGui::BeginChild("##osc", ImVec2(width, height), ImGuiChildFlags_Borders);
    toggle(base + prism::oscOn, osc == 0 ? "OSC A" : osc == 1 ? "OSC B" : "OSC C", ImVec2(dp(58.0f), 0.0f));
    ImGui::SameLine();
    // The table: the list, or the one before or after; or a wave of one's
    // own (imported), which the factory's arrows leave.
    const auto table = static_cast<int>(std::lround(value(base + prism::oscTable)));
    const auto own = card_.own != nullptr ? card_.own->oscillator[static_cast<std::size_t>(osc)] : prism::WavetableRef {};
    const auto pick = [&](const int index) {
        if (own != nullptr)
            (void) studio_.clearPrismTable(card_.track, card_.nodeId, osc);
        set(base + prism::oscTable, static_cast<float>(index));
    };
    if (ImGui::ArrowButton("##previous", ImGuiDir_Left))
        pick(own != nullptr ? table : (table + prism::factoryTableCount() - 1) % prism::factoryTableCount());
    ImGui::SameLine(0.0f, dp(2.0f));
    const auto listWidth = width - dp(204.0f);
    if (own != nullptr) {
        // Its name, and a cross back to the factory's.
        ImGui::PushStyleColor(ImGuiCol_Button, theme::alpha(prismOrange, 0.30f));
        (void) ImGui::Button((juce::String::fromUTF8("\xe2\x99\xaa ") + juce::String(own->name) + "##own").toRawUTF8(),
                             ImVec2(listWidth - dp(24.0f), 0.0f));
        ImGui::PopStyleColor();
        ui::tooltip((ui::Ls("Table importée, ", "Imported table, ") + juce::String(own->frameCount)
                     + ui::Ls(" images", " frames"))
                        .toRawUTF8());
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ImGui::Button(icon::close, ImVec2(dp(22.0f), 0.0f)))
            (void) studio_.clearPrismTable(card_.track, card_.nodeId, osc);
        ui::tooltip(L("Revenir aux tables de Prisme", "Back to Prisme's tables"));
    } else {
        choice(base + prism::oscTable, listWidth);
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::ArrowButton("##next", ImGuiDir_Right))
        pick(own != nullptr ? table : (table + 1) % prism::factoryTableCount());
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::Button(icon::folder, ImVec2(dp(26.0f), 0.0f)))
        chooseTableFile(osc);
    ui::tooltip(L("Importer une table d'ondes (.wav de Serum, Vital…) ou n'importe quel son, découpé en cycles à sa hauteur",
                  "Import a wavetable (a Serum or Vital .wav…) or any sound, cut into cycles at its pitch"));
    ImGui::SameLine();
    toggle(base + prism::oscToFilter, "F", ImVec2(dp(24.0f), 0.0f));
    // The table.
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 displaySize(ImGui::GetContentRegionAvail().x, std::max(dp(80.0f), height * 0.36f));
    drawWavetable(osc, origin, displaySize, false);
    ImGui::Dummy(displaySize);
    // The table's knobs, then the unison's, then the pitch.
    knob(base + prism::oscPosition, L("WT POS", "WT POS"));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    ImGui::TextDisabled("%s", L("DÉFORMATION", "WARP"));
    ImGui::PopFont();
    choice(base + prism::oscWarp, dp(96.0f));
    ImGui::EndGroup();
    ImGui::SameLine();
    knob(base + prism::oscWarpAmount, L("QUANTITÉ", "AMOUNT"));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    ImGui::TextDisabled("%s", L("SPECTRE", "SPECTRAL"));
    ImGui::PopFont();
    choice(prism::spectralBase(osc) + prism::spectralMode, dp(96.0f));
    ui::tooltip(L("La table remodelée dans ses harmoniques : ses aigus ôtés ou gardés seuls, ses harmoniques impaires "
                  "ou paires seules, un peigne, une octave au-dessus, son formant monté",
                  "The table reshaped in its harmonics: its highs taken away or kept alone, its odd or even harmonics "
                  "alone, a comb, an octave above, its formant moved up"));
    ImGui::EndGroup();
    ImGui::SameLine();
    if (value(prism::spectralBase(osc) + prism::spectralMode) < 0.5f)
        ImGui::BeginDisabled();
    knob(prism::spectralBase(osc) + prism::spectralAmount, L("SPECTRE", "SPECTRAL"));
    if (value(prism::spectralBase(osc) + prism::spectralMode) < 0.5f)
        ImGui::EndDisabled();
    knob(base + prism::oscPhase, "PHASE");
    ImGui::SameLine();
    knob(base + prism::oscRandom, "RAND");
    ImGui::SameLine();
    integer(base + prism::oscUnison, L("UNISSON", "UNISON"), dp(48.0f));
    ImGui::SameLine();
    knob(base + prism::oscDetune, L("DÉSACC.", "DETUNE"));
    ImGui::SameLine();
    knob(base + prism::oscBlend, L("MÉLANGE", "BLEND"));
    ImGui::SameLine();
    knob(base + prism::oscWidth, L("LARGEUR", "WIDTH"));
    knob(base + prism::oscPan, "PAN");
    ImGui::SameLine();
    knob(base + prism::oscLevel, L("NIVEAU", "LEVEL"));
    ImGui::SameLine();
    integer(base + prism::oscOctave, "OCT", dp(40.0f));
    ImGui::SameLine();
    integer(base + prism::oscSemi, L("DEMI", "SEMI"), dp(40.0f));
    ImGui::SameLine();
    knob(base + prism::oscFine, L("FIN", "FINE"), 26.0f);
    ImGui::SameLine();
    knob(base + prism::oscCoarse, L("HAUTEUR", "COARSE"), 26.0f);
    (void) p;
    ImGui::EndChild();
    ImGui::PopID();
}

void PrismView::drawSubAndNoise(const float width, const float height)
{
    ImGui::BeginChild("##subNoise", ImVec2(width, height), ImGuiChildFlags_Borders);
    toggle(prism::subBase + prism::subOn, "SUB", ImVec2(dp(48.0f), 0.0f));
    ImGui::SameLine();
    choice(prism::subBase + prism::subShape, ImGui::GetContentRegionAvail().x);
    integer(prism::subBase + prism::subOctave, "OCT", dp(40.0f));
    ImGui::SameLine();
    knob(prism::subBase + prism::subLevel, L("NIVEAU", "LEVEL"), 28.0f);
    ImGui::SameLine();
    knob(prism::subBase + prism::subPan, "PAN", 28.0f);
    toggle(prism::subBase + prism::subDirect, L("SORTIE DIRECTE", "DIRECT OUT"));
    ImGui::Separator();
    toggle(prism::noiseBase + prism::noiseOn, L("BRUIT", "NOISE"), ImVec2(dp(56.0f), 0.0f));
    ImGui::SameLine();
    choice(prism::noiseBase + prism::noiseType, ImGui::GetContentRegionAvail().x);
    knob(prism::noiseBase + prism::noiseTone, L("COULEUR", "TONE"), 28.0f);
    ImGui::SameLine();
    knob(prism::noiseBase + prism::noisePitch, L("VITESSE", "PITCH"), 28.0f);
    ImGui::SameLine();
    knob(prism::noiseBase + prism::noiseLevel, L("NIVEAU", "LEVEL"), 28.0f);
    knob(prism::noiseBase + prism::noisePan, "PAN", 28.0f);
    ImGui::SameLine();
    toggle(prism::noiseBase + prism::noiseToFilter, L("→ FILTRE", "→ FILTER"));
    ImGui::EndChild();
}

void PrismView::drawFilter(const float width, const float height)
{
    ImGui::BeginChild("##filter", ImVec2(width, height), ImGuiChildFlags_Borders);
    toggle(prism::filterBase + prism::filterOn, L("FILTRE", "FILTER"), ImVec2(dp(62.0f), 0.0f));
    ImGui::SameLine();
    choice(prism::filterBase + prism::filterType, ImGui::GetContentRegionAvail().x);
    // What goes through it: A, B, the noise, the sub.
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.82f);
    ImGui::TextDisabled("%s", L("Passent par lui :", "Through it:"));
    ImGui::SameLine();
    toggle(prism::oscBase(0) + prism::oscToFilter, "A", ImVec2(dp(24.0f), 0.0f));
    ImGui::SameLine(0.0f, dp(2.0f));
    toggle(prism::oscBase(1) + prism::oscToFilter, "B", ImVec2(dp(24.0f), 0.0f));
    ImGui::SameLine(0.0f, dp(2.0f));
    toggle(prism::oscBase(2) + prism::oscToFilter, "C", ImVec2(dp(24.0f), 0.0f));
    ImGui::SameLine(0.0f, dp(2.0f));
    toggle(prism::noiseBase + prism::noiseToFilter, "N", ImVec2(dp(24.0f), 0.0f));
    ImGui::SameLine(0.0f, dp(2.0f));
    {
        // The sub's is the other way round: "direct" leaves the filter out.
        ImGui::PushID("subRoute");
        auto through = value(prism::subBase + prism::subDirect) < 0.5f;
        if (ui::toggle("S", &through, prismOrange, ImVec2(dp(24.0f), 0.0f), L("Sub", "Sub")))
            set(prism::subBase + prism::subDirect, through ? 0.0f : 1.0f);
        ImGui::PopID();
    }
    ImGui::PopFont();
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 displaySize(ImGui::GetContentRegionAvail().x, std::max(dp(70.0f), height * 0.32f));
    drawFilterResponse(origin, displaySize);
    ImGui::Dummy(displaySize);
    knob(prism::filterBase + prism::filterCutoff, L("FRÉQ.", "CUTOFF"));
    ImGui::SameLine();
    knob(prism::filterBase + prism::filterResonance, L("RÉSO.", "RES"));
    ImGui::SameLine();
    knob(prism::filterBase + prism::filterDrive, "DRIVE");
    knob(prism::filterBase + prism::filterVariation, "VAR");
    ImGui::SameLine();
    knob(prism::filterBase + prism::filterMix, L("MÉLANGE", "MIX"));
    ImGui::SameLine();
    knob(prism::filterBase + prism::filterKeytrack, L("CLAVIER", "KEY"));
    ImGui::EndChild();
}

void PrismView::drawModulators(const float width, const float height)
{
    ImGui::BeginChild("##modulators", ImVec2(width, height), ImGuiChildFlags_Borders);
    // The sources: a click shows one, a drag takes it onto a knob.
    const char* names[8] = { "ENV 1", "ENV 2", "ENV 3", "ENV 4", "LFO 1", "LFO 2", "LFO 3", "LFO 4" };
    const ImVec2 chip(dp(54.0f), dp(22.0f));
    for (int at = 0; at < 8; ++at) {
        auto clicked = false;
        sourceChip(at + 1, names[at], chip, modulator_ == at, &clicked);
        if (clicked)
            modulator_ = at;
        ImGui::SameLine(0.0f, dp(3.0f));
    }
    ImGui::NewLine();
    const auto displayWidth = std::min(dp(260.0f), ImGui::GetContentRegionAvail().x * 0.42f);
    const ImVec2 displaySize(displayWidth, std::max(dp(70.0f), height - dp(70.0f)));
    const auto origin = ImGui::GetCursorScreenPos();
    if (modulator_ < 4) {
        const auto env = modulator_;
        drawEnvelope(env, origin, displaySize);
        ImGui::Dummy(displaySize);
        ImGui::SameLine();
        ImGui::BeginGroup();
        const auto base = prism::envBase(env);
        knob(base + prism::envAttack, L("ATTAQUE", "ATTACK"));
        ImGui::SameLine();
        knob(base + prism::envHold, L("MAINTIEN", "HOLD"));
        ImGui::SameLine();
        knob(base + prism::envDecay, L("DÉCLIN", "DECAY"));
        ImGui::SameLine();
        knob(base + prism::envSustain, L("SOUTIEN", "SUSTAIN"));
        ImGui::SameLine();
        knob(base + prism::envRelease, L("RELÂCH.", "RELEASE"));
        knob(base + prism::envAttackCurve, L("COURBE A", "CURVE A"), 24.0f);
        ImGui::SameLine();
        knob(base + prism::envDecayCurve, L("COURBE D", "CURVE D"), 24.0f);
        ImGui::SameLine();
        knob(base + prism::envReleaseCurve, L("COURBE R", "CURVE R"), 24.0f);
        if (env == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", L("L'env 1 fait le volume", "Env 1 shapes the level"));
        }
        ImGui::EndGroup();
    } else {
        const auto lfo = modulator_ - 4;
        drawLfo(lfo, origin, displaySize);
        if (static_cast<prism::LfoShape>(std::lround(value(prism::lfoBase(lfo) + prism::lfoShape))) == prism::LfoShape::drawn)
            editDrawnLfo(lfo, origin, displaySize);
        else
            ImGui::Dummy(displaySize);
        ImGui::SameLine();
        ImGui::BeginGroup();
        const auto base = prism::lfoBase(lfo);
        choice(base + prism::lfoShape, dp(130.0f));
        ImGui::SameLine();
        choice(base + prism::lfoMode, dp(110.0f));
        ImGui::SameLine();
        toggle(base + prism::lfoSync, "BPM");
        ImGui::SameLine();
        toggle(base + prism::lfoUnipolar, L("0..1", "0..1"));
        if (value(base + prism::lfoSync) >= 0.5f) {
            ImGui::BeginGroup();
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
            ImGui::TextDisabled("%s", L("DIVISION", "DIVISION"));
            ImGui::PopFont();
            choice(base + prism::lfoDivision, dp(70.0f));
            ImGui::EndGroup();
        } else {
            knob(base + prism::lfoRate, L("VITESSE", "RATE"));
        }
        ImGui::SameLine();
        knob(base + prism::lfoRise, L("MONTÉE", "RISE"));
        ImGui::SameLine();
        knob(base + prism::lfoDelay, L("RETARD", "DELAY"));
        ImGui::SameLine();
        knob(base + prism::lfoPhase, "PHASE");
        ImGui::SameLine();
        knob(base + prism::lfoSmooth, L("LISSAGE", "SMOOTH"));
        ImGui::SameLine();
        knob(prism::lfoStereo(lfo), L("STÉRÉO", "STEREO"));
        ui::tooltip(L("La droite en avance sur la gauche (jusqu'à une demi-période) : le filtre et le volume bougent "
                      "différemment de chaque côté",
                      "The right side ahead of the left (up to half a cycle): the filter and the volume move "
                      "differently on each side"));
        ImGui::EndGroup();
    }
    ImGui::EndChild();
}

void PrismView::drawMacros(const float width, const float height)
{
    ImGui::BeginChild("##macros", ImVec2(width, height), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("%s", L("MACROS", "MACROS"));
    for (int macro = 0; macro < prism::macroCount; ++macro) {
        ImGui::BeginGroup();
        char label[16];
        std::snprintf(label, sizeof(label), "MACRO %d", macro + 1);
        knob(prism::macroBase + macro, label, 34.0f);
        auto clicked = false;
        char chip[16];
        std::snprintf(chip, sizeof(chip), "M%d", macro + 1);
        sourceChip(static_cast<int>(prism::Source::macro1) + macro, chip, ImVec2(dp(46.0f), dp(18.0f)), false, &clicked);
        ImGui::EndGroup();
        if (macro % 2 == 0)
            ImGui::SameLine(0.0f, dp(10.0f));
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", L("AUTRES SOURCES", "OTHER SOURCES"));
    auto clicked = false;
    sourceChip(static_cast<int>(prism::Source::velocity), L("VÉLO", "VELO"), ImVec2(dp(46.0f), dp(18.0f)), false, &clicked);
    ImGui::SameLine(0.0f, dp(3.0f));
    sourceChip(static_cast<int>(prism::Source::note), "NOTE", ImVec2(dp(46.0f), dp(18.0f)), false, &clicked);
    ImGui::SameLine(0.0f, dp(3.0f));
    sourceChip(static_cast<int>(prism::Source::modWheel), L("MOLETTE", "WHEEL"), ImVec2(dp(56.0f), dp(18.0f)), false, &clicked);
    sourceChip(static_cast<int>(prism::Source::aftertouch), "AT", ImVec2(dp(46.0f), dp(18.0f)), false, &clicked);
    ImGui::SameLine(0.0f, dp(3.0f));
    sourceChip(static_cast<int>(prism::Source::random), L("ALÉA", "RAND"), ImVec2(dp(46.0f), dp(18.0f)), false, &clicked);
    ImGui::EndChild();
}

void PrismView::drawOscTab()
{
    const auto available = ImGui::GetContentRegionAvail();
    const auto gap = ImGui::GetStyle().ItemSpacing.x;
    const auto topHeight = std::max(dp(300.0f), available.y * 0.6f - gap);
    const auto sideWidth = dp(205.0f);
    const auto filterWidth = dp(255.0f);
    const auto three = available.x - sideWidth - filterWidth - gap * 4.0f >= dp(3.0f * 330.0f);
    const auto oscWidth = std::max(dp(250.0f), (available.x - sideWidth - filterWidth - gap * (three ? 4.0f : 3.0f)) / (three ? 3.0f : 2.0f));
    drawSubAndNoise(sideWidth, topHeight);
    ImGui::SameLine();
    drawOscillator(0, oscWidth, topHeight);
    ImGui::SameLine();
    if (three) {
        drawOscillator(1, oscWidth, topHeight);
        ImGui::SameLine();
        drawOscillator(2, oscWidth, topHeight);
    } else {
        // B or C, two tabs over their place (a dot on the one playing).
        ImGui::BeginGroup();
        const auto tabHeight = ImGui::GetFrameHeight() + dp(2.0f);
        for (int osc = 1; osc <= 2; ++osc) {
            if (osc == 2)
                ImGui::SameLine(0.0f, dp(2.0f));
            const auto playing = value(prism::oscBase(osc) + prism::oscOn) >= 0.5f;
            const auto label = juce::String(osc == 1 ? "OSC B" : "OSC C") + (playing ? juce::String::fromUTF8(" \xe2\x97\x8f") : "");
            auto shown = secondOscillator_ == osc;
            if (ui::toggle((label + "##second" + juce::String(osc)).toRawUTF8(), &shown, prismOrange, ImVec2(dp(80.0f), 0.0f)))
                secondOscillator_ = osc;
        }
        drawOscillator(secondOscillator_, oscWidth, topHeight - tabHeight);
        ImGui::EndGroup();
    }
    ImGui::SameLine();
    drawFilter(filterWidth, topHeight);
    const auto bottomHeight = std::max(dp(180.0f), ImGui::GetContentRegionAvail().y);
    drawModulators(available.x - sideWidth - gap, bottomHeight);
    ImGui::SameLine();
    drawMacros(sideWidth, bottomHeight);
}

void PrismView::drawMatrixTab()
{
    const auto& p = theme::palette();
    ImGui::TextDisabled("%s", L("Chaque ligne : une source, de combien, sur quoi. Une source glissée sur un potard en remplit une.",
                                "Each row: a source, how much, onto what. A source dragged onto a knob fills one."));
    if (!ImGui::BeginTable("##matrix", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        return;
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, dp(26.0f));
    ImGui::TableSetupColumn(L("Source", "Source"), ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn(L("Quantité", "Amount"), ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn(L("Destination", "Destination"), ImGuiTableColumnFlags_WidthStretch, 1.8f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, dp(28.0f));
    ImGui::TableHeadersRow();
    for (int slot = 0; slot < prism::slotCount; ++slot) {
        const auto base = prism::slotBase(slot);
        ImGui::PushID(slot);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const auto source = static_cast<int>(std::lround(value(base + prism::slotSource)));
        ImGui::TextColored(theme::vec(source > 0 ? sourceColour(source) : p.textFaint), "%d", slot + 1);
        ImGui::TableNextColumn();
        choice(base + prism::slotSource, -1.0f);
        ImGui::TableNextColumn();
        auto amount = value(base + prism::slotAmount) * 100.0f;
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::SliderFloat("##amount", &amount, -100.0f, 100.0f, "%.0f %%"))
            set(base + prism::slotAmount, amount / 100.0f);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            set(base + prism::slotAmount, 0.0f);
        ImGui::TableNextColumn();
        // The destinations, by section.
        {
            const auto destination = static_cast<int>(std::lround(value(base + prism::slotDestination)));
            const auto shown = prism::displayValue(base + prism::slotDestination, static_cast<float>(destination), ui::french());
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##destination", shown.c_str(), ImGuiComboFlags_HeightLargest)) {
                if (ImGui::Selectable(L("Aucune", "None"), destination == 0))
                    set(base + prism::slotDestination, 0.0f);
                const auto& list = prism::destinations();
                std::string group;
                auto open = false;
                for (std::size_t at = 1; at < list.size(); ++at) {
                    const auto name = prism::displayValue(base + prism::slotDestination, static_cast<float>(at), ui::french());
                    const auto split = name.find(" · ");
                    const auto section = split != std::string::npos ? name.substr(0, split) : std::string();
                    if (section != group) {
                        if (open)
                            ImGui::EndMenu();
                        group = section;
                        open = ImGui::BeginMenu(group.c_str());
                    }
                    if (open && ImGui::MenuItem(split != std::string::npos ? name.substr(split + 3).c_str() : name.c_str(), nullptr,
                                                static_cast<int>(at) == destination))
                        set(base + prism::slotDestination, static_cast<float>(at));
                }
                if (open)
                    ImGui::EndMenu();
                ImGui::EndCombo();
            }
        }
        ImGui::TableNextColumn();
        if (ImGui::SmallButton(icon::close)) {
            set(base + prism::slotSource, 0.0f);
            set(base + prism::slotDestination, 0.0f);
            set(base + prism::slotAmount, 0.0f);
        }
        ui::tooltip(L("Vider la ligne", "Clear the row"));
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void PrismView::drawEffectsTab()
{
    // The effects a wavetable synth's rack has, as CyDAW's devices.
    static constexpr instrument_effects::Choice effects[] = {
        { "drive", "Distorsion", "Distortion" },  { "bitcrusher", "Réducteur", "Bitcrusher" },
        { "flanger", "Flanger", "Flanger" },      { "phaser", "Phaser", "Phaser" },
        { "chorus", "Chorus", "Chorus" },         { "ping-pong", "Délai", "Delay" },
        { "comp-pro", "Compresseur", "Compressor" }, { "eq-pro", "Égaliseur", "EQ" },
        { "multimode-filter", "Filtre", "Filter" }, { "reverb", "Réverbe", "Reverb" },
        { "limiter", "Limiteur", "Limiter" },
    };
    instrument_effects::draw(studio_, state_, card_.track, "prism-synth", "Prisme", effects, prismOrange);
}

void PrismView::drawGlobalTab()
{
    ImGui::BeginChild("##global", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    ui::sectionTitle(L("Jeu", "Voicing"));
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", L("MODE", "MODE"));
    choice(prism::voiceBase + prism::voiceMode, dp(130.0f));
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, dp(14.0f));
    integer(prism::voiceBase + prism::voiceCount, L("VOIX", "VOICES"), dp(56.0f));
    ImGui::SameLine(0.0f, dp(14.0f));
    knob(prism::voiceBase + prism::voiceGlide, L("GLISSANDO", "GLIDE"));
    ImGui::SameLine();
    toggle(prism::voiceBase + prism::voiceGlideAlways, L("Toujours glisser", "Always glide"));
    ImGui::SameLine(0.0f, dp(14.0f));
    integer(prism::voiceBase + prism::voiceBendRange, L("PITCH BEND", "BEND RANGE"), dp(56.0f));
    ImGui::SameLine(0.0f, dp(14.0f));
    knob(prism::voiceBase + prism::voiceVelocity, L("VÉLOCITÉ", "VELOCITY"));
    ImGui::SameLine();
    knob(prism::voiceBase + prism::voiceMaster, L("VOLUME", "MASTER"));
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s",
                        L("Mono : une voix, chaque note la relance. Legato : une voix, les notes liées glissent sans la "
                          "relancer. Le glissando glisse entre notes liées, ou toujours. La molette (CC 1), l'aftertouch et "
                          "le pitch bend sont des sources de modulation.",
                          "Mono: one voice, every note starts it again. Legato: one voice, tied notes slide without starting "
                          "it again. Glide slides between tied notes, or always. The mod wheel (CC 1), aftertouch and pitch "
                          "bend are modulation sources."));
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ui::sectionTitle(L("Son", "Sound"));
    if (ImGui::Button(L("Initialiser le son", "Initialise the sound"))) {
        const auto defaults = prism::defaultValues();
        for (int index = 0; index < prism::parameterCount; ++index)
            if (value(index) != defaults[static_cast<std::size_t>(index)])
                set(index, defaults[static_cast<std::size_t>(index)]);
    }
    ui::tooltip(L("Tous les réglages à leur valeur de départ (Ctrl+Z l'annule)", "Every setting back to its start (Ctrl+Z undoes it)"));
    ImGui::EndChild();
}

// ---- The card's pane, the window -------------------------------------------------------------

void PrismView::drawPane(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId, const ImVec2 origin,
                         const ImVec2 size, const ImU32 colour)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    if (!bind(track, graph, nodeId))
        return;
    const auto& p = theme::palette();
    (void) colour;
    ImGui::PushID(static_cast<int>(nodeId) + 31'000);
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginGroup();
    // Its window opens from the card's header.
    if (const auto table = tableOf(0))
        ImGui::TextDisabled("%s", table->name.c_str());
    else
        ImGui::NewLine();
    const auto displayOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 displaySize(size.x, std::max(dp(36.0f), size.y * 0.24f));
    drawWavetable(0, displayOrigin, displaySize, true);
    ImGui::Dummy(displaySize);
    // Two rows of the knobs that matter most.
    knob(prism::oscBase(0) + prism::oscPosition, "A POS", 26.0f);
    ImGui::SameLine();
    knob(prism::oscBase(1) + prism::oscPosition, "B POS", 26.0f);
    ImGui::SameLine();
    knob(prism::filterBase + prism::filterCutoff, L("FRÉQ.", "CUTOFF"), 26.0f);
    ImGui::SameLine();
    knob(prism::filterBase + prism::filterResonance, L("RÉSO.", "RES"), 26.0f);
    ImGui::SameLine();
    knob(prism::envBase(0) + prism::envAttack, L("ATTAQUE", "ATTACK"), 26.0f);
    ImGui::SameLine();
    knob(prism::envBase(0) + prism::envRelease, L("RELÂCH.", "RELEASE"), 26.0f);
    for (int macro = 0; macro < prism::macroCount; ++macro) {
        char label[16];
        std::snprintf(label, sizeof(label), "MACRO %d", macro + 1);
        knob(prism::macroBase + macro, label, 26.0f);
        ImGui::SameLine();
    }
    knob(prism::voiceBase + prism::voiceMaster, L("VOLUME", "MASTER"), 26.0f);
    (void) p;
    ImGui::EndGroup();
    ImGui::PopID();
}

void PrismView::openEditor(const TrackRef& track, const std::uint64_t nodeId, const bool detached, const bool here)
{
    const auto id = windows_.open(track, nodeId, state_.manyInstrumentWindows);
    placeHere_ = here && !detached ? id : std::string();
    if (detached)
        ui::setDetached(id.c_str(), true);
    else if (here)
        ui::setDetached(id.c_str(), false);
}

void PrismView::drawEditor()
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    windows_.each([this](const TrackRef& track, const std::uint64_t nodeId, const bool focus) {
        editor_ = Target { track, nodeId };
        windowId_ = windows_.idOf(track, nodeId);
        const auto open = drawEditorWindow(focus);
        editor_.reset();
        return open;
    });
}

bool PrismView::drawEditorWindow(const bool focus)
{
    const auto target = *editor_;
    auto* slot = studio_.graphFor(target.track);
    if (slot == nullptr || !studio_.isValid(target.track) || !bind(target.track, slot->graph(), target.nodeId)) {
        return false;
    }
    const auto& p = theme::palette();
    ui::beforeDetachable(windowId_.c_str());
    windows_.placeFirst(target.track, target.nodeId);
    if (!placeHere_.empty() && placeHere_ == windowId_) {
        placeHere_.clear();
        const auto* main = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(main->Pos.x + dp(40.0f), main->Pos.y + dp(60.0f)), ImGuiCond_Always);
        ImGui::SetNextWindowViewport(main->ID);
    }
    ImGui::SetNextWindowSize(ImVec2(dp(1'240.0f), dp(800.0f)), ImGuiCond_FirstUseEver);
    if (focus)
        ImGui::SetNextWindowFocus();
    auto open = true;
    const auto title = juce::String::fromUTF8("Prisme — ") + studio_.trackName(target.track) + juce::String(windowId_);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, theme::mix(prismOrange, p.bg2 | 0xff000000u, 0.65f));
    const auto visible = ui::beginEditorWindow(title.toRawUTF8(), &open, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();
    if (visible) {
        // Out of the studio's window, as a plug-in's (another screen), and back.
        {
            const auto headerY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - dp(24.0f));
            (void) ui::detachButton(windowId_.c_str(), ImVec2(dp(24.0f), dp(22.0f)));
            ImGui::SetCursorPosY(headerY);
        }
        // The header: the name, its presets, the master.
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextColored(theme::vec(prismOrange), "PRISME");
        ImGui::PopFont();
        ImGui::SameLine(0.0f, dp(16.0f));
        if (const auto holder = studio_.devicePresetHolder(target.track, target.nodeId)) {
            const auto track = target.track;
            const auto nodeId = target.nodeId;
            presets_.bar("prismPresets", *holder, [this, track, nodeId] { return studio_.devicePresetState(track, nodeId); },
                         [this, track, nodeId](const juce::var& preset, juce::String& error) {
                             return studio_.applyDevicePreset(track, nodeId, preset, error);
                         },
                         dp(330.0f));
        }
        ImGui::SameLine(0.0f, dp(16.0f));
        const char* modes[] = { "POLY", "MONO", "LEGATO" };
        ImGui::TextDisabled("%s  ·  %d %s", modes[std::clamp(static_cast<int>(std::lround(value(prism::voiceBase + prism::voiceMode))), 0, 2)],
                            static_cast<int>(std::lround(value(prism::voiceBase + prism::voiceCount))), L("voix", "voices"));
        if (ImGui::BeginTabBar("##prismTabs")) {
            if (ImGui::BeginTabItem("OSC")) {
                drawOscTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("MATRICE", "MATRIX"))) {
                drawMatrixTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("EFFETS", "FX"))) {
                drawEffectsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("GLOBAL")) {
                drawGlobalTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    return open;
}

} // namespace cydaw::v2
