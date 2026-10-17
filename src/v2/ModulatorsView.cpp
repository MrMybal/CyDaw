#include "ModulatorsView.h"

#include "Icons.h"
#include "Localization.h"
#include "Theme.h"

#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/Modulators.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

namespace modulator = engine::modulator;

// The browser's shelves, as Bitwig sorts its modulators.
struct Shelf {
    const char* name;
    std::vector<const char*> types;
};
const std::vector<Shelf>& shelves()
{
    static const std::vector<Shelf> all {
        { "LFO", { "mod-lfo", "mod-random" } },  { "Sequence", { "mod-steps" } },
        { "Envelope", { "mod-adsr" } },          { "Note", { "mod-note", "mod-expression" } },
        { "Interface", { "mod-macro", "mod-button" } }, { "Modifier", { "mod-math" } },
        { "Audio", { "mod-follower" } },
    };
    return all;
}

// A modulator's setting: its catalogue entry, from its binding.
const engine::EffectParameter* settingOf(const juce::String& binding)
{
    if (!binding.startsWith("node."))
        return nullptr;
    const auto separator = binding.indexOfChar(5, '.');
    return separator < 0 ? nullptr : engine::effectParameterForSuffix(binding.substring(separator + 1).toStdString());
}

bool hidden(const juce::String& binding)
{
    return binding.endsWith(".ownerLow") || binding.endsWith(".ownerHigh");
}

// The names of a setting's choices, or none for a knob.
std::vector<std::string> choicesOf(const juce::String& typeKey, const std::string_view key)
{
    std::vector<std::string> names;
    if (key == "sync")
        for (auto index = 0; index < modulator::divisionCount; ++index)
            names.emplace_back(modulator::divisionName(index));
    else if (key == "shape")
        for (auto index = 0; index < modulator::shapeCount; ++index)
            names.emplace_back(modulator::shapeName(index));
    else if (key == "mode")
        for (auto index = 0; index < modulator::mathModeCount; ++index)
            names.emplace_back(modulator::mathModeName(index));
    else if (key == "source" && typeKey == "mod-note")
        names = { "Key", "Velocity", "Gate" };
    else if (key == "source" && typeKey == "mod-expression")
        names = { "Mod wheel", "Aftertouch", "Pitch bend", "Controller" };
    return names;
}

bool switchSetting(const std::string_view key)
{
    return key == "retrigger" || key == "bipolar" || key == "legato" || key == "on";
}

// An arrow pointing up and right, in `min`..`max`.
void arrow(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto inset = (max.x - min.x) * 0.24f;
    const ImVec2 from(min.x + inset, max.y - inset);
    const ImVec2 to(max.x - inset, min.y + inset);
    const auto thickness = std::max(1.5f, dp(1.6f));
    list->AddLine(from, to, colour, thickness);
    list->AddLine(to, ImVec2(to.x - (max.x - min.x) * 0.36f, to.y), colour, thickness);
    list->AddLine(to, ImVec2(to.x, to.y + (max.y - min.y) * 0.36f), colour, thickness);
}

} // namespace

ModulatorsView::ModulatorsView(Studio& studio, UiState& state) : studio_(studio), state_(state)
{
    host_.view = this;
}

float ModulatorsView::width()
{
    return dp(184.0f);
}

void ModulatorsView::begin(const TrackRef& track, const std::uint64_t trackKey, app::NodeGraphComponent& graph,
                           const std::vector<app::NodeGraphComponent::EditorNode>& nodes)
{
    juce::ignoreUnused(track);
    graph_ = &graph;
    trackKey_ = trackKey;
    owners_.clear();
    modulators_.clear();
    names_.clear();
    colours_.clear();
    byBinding_.clear();
    std::set<std::uint64_t> present;
    for (const auto& node : nodes)
        present.insert(node.id);
    // Each card's modulators, in the order they were made; "LFO", "LFO 2".
    std::map<std::pair<std::uint64_t, juce::String>, int> made;
    std::map<std::pair<std::uint64_t, juce::String>, int> total;
    for (const auto& node : nodes)
        if (const auto owner = graph.editorModulatorOwner(node.id); owner != 0 && present.contains(owner))
            ++total[{ owner, node.typeKey }];
    for (const auto& node : nodes) {
        colours_[node.id] = theme::fromArgb(node.colour.getARGB());
        const auto owner = graph.editorModulatorOwner(node.id);
        if (owner == 0 || !present.contains(owner)) {
            names_[node.id] = node.title.toStdString();
            continue;
        }
        owners_[node.id] = owner;
        modulators_.emplace(node.id, node);
        const auto number = ++made[{ owner, node.typeKey }];
        const auto* effect = engine::effectForType(node.typeKey.toStdString());
        auto name = effect != nullptr ? std::string(effect->displayName) : node.title.toStdString();
        if (total[{ owner, node.typeKey }] > 1)
            name += " " + std::to_string(number);
        names_[node.id] = name;
    }
    for (const auto& modulation : graph.editorModulations()) {
        if (modulation.mode != 1)
            continue;
        const auto colour = colours_.find(modulation.source);
        byBinding_[modulation.binding.toStdString()].push_back(
            { modulation.source, modulation.amount, colour != colours_.end() ? colour->second : 0u, nameOf(modulation.source) });
    }
    // An armed modulator of another track, or gone: nothing is armed.
    if (state_.armedModulator && (state_.armedModulator->first != trackKey_ || !present.contains(state_.armedModulator->second)))
        state_.armedModulator.reset();
    if (state_.armedModulator && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        state_.armedModulator.reset();
}

void ModulatorsView::end()
{
    drawBrowser();
}

bool ModulatorsView::owned(const std::uint64_t node) const
{
    return owners_.contains(node);
}

std::size_t ModulatorsView::countFor(const std::uint64_t owner) const
{
    return static_cast<std::size_t>(std::count_if(owners_.begin(), owners_.end(), [owner](const auto& entry) { return entry.second == owner; }));
}

bool ModulatorsView::open(const std::uint64_t owner) const
{
    return state_.modulatorColumns.contains(key(owner));
}

void ModulatorsView::setOpen(const std::uint64_t owner, const bool open)
{
    if (open)
        state_.modulatorColumns.insert(key(owner));
    else
        state_.modulatorColumns.erase(key(owner));
}

std::string ModulatorsView::nameOf(const std::uint64_t modulator) const
{
    const auto found = names_.find(modulator);
    return found != names_.end() ? found->second : std::string("?");
}

void ModulatorsView::removeWith(const std::uint64_t owner)
{
    if (graph_ == nullptr)
        return;
    std::vector<std::uint64_t> going;
    for (const auto& [node, card] : owners_)
        if (card == owner)
            going.push_back(node);
    if (!going.empty())
        (void) graph_->editorRemoveNodes(going);
}

void ModulatorsView::report(const juce::String& problem)
{
    if (problem == lastProblem_ && ImGui::GetTime() - lastProblemTime_ < 2.0)
        return;
    lastProblem_ = problem;
    lastProblemTime_ = ImGui::GetTime();
    state_.status = problem;
    state_.statusIsError = true;
    state_.statusTime = ImGui::GetTime();
}

void ModulatorsView::draw(const std::uint64_t owner, const ImVec2 at, const ImVec2 size, const ImU32 colour)
{
    if (graph_ == nullptr)
        return;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), p.bg1, dp(4.0f));
    ImGui::PushID("##modulators");
    // Its title, its "+".
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.72f);
    list->AddText(ImVec2(at.x + dp(6.0f), at.y + dp(5.0f)), p.textDim, "MODULATORS");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(at.x + size.x - dp(22.0f), at.y + dp(2.0f)));
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
    if (ui::iconButton(icon::add, "Add a modulator: an LFO, steps, an envelope, a macro...", false, 0, ImVec2(dp(20.0f), dp(18.0f)))) {
        browserOwner_ = owner;
        openBrowser_ = true;
        browserSearch_[0] = 0;
    }
    ImGui::PopFont();

    // Its modulators, a row each.
    std::vector<std::uint64_t> mine;
    for (const auto& [node, card] : owners_)
        if (card == owner)
            mine.push_back(node);
    auto& shown = state_.shownModulator[key(owner)];
    if (!mine.empty() && std::find(mine.begin(), mine.end(), shown) == mine.end())
        shown = mine.front();
    const auto top = at.y + dp(24.0f);
    ImGui::SetCursorScreenPos(ImVec2(at.x, top));
    if (ImGui::BeginChild("##column", ImVec2(size.x, at.y + size.y - top - dp(2.0f)), ImGuiChildFlags_None)) {
        auto* rows = ImGui::GetWindowDrawList();
        const auto rowHeight = dp(22.0f);
        std::optional<std::uint64_t> removeModulator;
        std::optional<std::uint64_t> clearModulator;
        for (const auto id : mine) {
            const auto& node = modulators_.at(id);
            const auto name = nameOf(id);
            const auto rowColour = colours_[id];
            ImGui::PushID(static_cast<int>(id));
            const auto rowTop = ImGui::GetCursorScreenPos();
            const ImVec2 rowEnd(rowTop.x + size.x - dp(4.0f), rowTop.y + rowHeight);
            const auto selected = shown == id;
            const auto armed = state_.armedModulator && state_.armedModulator->second == id;
            if (selected)
                rows->AddRectFilled(rowTop, rowEnd, theme::alpha(colour, 0.22f), dp(3.0f));
            rows->AddRectFilled(ImVec2(rowTop.x + dp(3.0f), rowTop.y + dp(4.0f)), ImVec2(rowTop.x + dp(6.0f), rowEnd.y - dp(4.0f)),
                                rowColour, dp(1.0f));
            // Its arrow: dragged onto a knob, or clicked to arm it.
            const ImVec2 arrowMin(rowEnd.x - dp(22.0f), rowTop.y + dp(2.0f));
            const ImVec2 arrowMax(rowEnd.x - dp(2.0f), rowEnd.y - dp(2.0f));
            // Its value now, a bar between its name and its arrow.
            const auto barRight = arrowMin.x - dp(4.0f);
            const auto barLeft = barRight - dp(34.0f);
            const auto barY = rowTop.y + rowHeight * 0.5f;
            rows->AddRectFilled(ImVec2(barLeft, barY - dp(2.5f)), ImVec2(barRight, barY + dp(2.5f)), p.bg3, dp(2.0f));
            if (const auto live = graph_->editorControlValue(id)) {
                const auto bipolar = std::any_of(node.parameters.begin(), node.parameters.end(), [](const auto& parameter) {
                    return parameter.binding.endsWith(".bipolar") && parameter.value >= 0.5;
                }) || node.typeKey == "mod-note" || node.typeKey == "mod-math";
                const auto value = std::clamp(*live, bipolar ? -1.0f : 0.0f, 1.0f);
                const auto zero = bipolar ? (barLeft + barRight) * 0.5f : barLeft;
                const auto x = bipolar ? zero + value * (barRight - barLeft) * 0.5f : barLeft + value * (barRight - barLeft);
                rows->AddRectFilled(ImVec2(std::min(zero, x), barY - dp(2.5f)), ImVec2(std::max(zero, x), barY + dp(2.5f)), rowColour, dp(2.0f));
            }
            // Its name: shows its settings; a right click, its menu.
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x + dp(9.0f), rowTop.y));
            ImGui::InvisibleButton("##name", ImVec2(std::max(1.0f, barLeft - rowTop.x - dp(12.0f)), rowHeight));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                shown = id;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##modulatorMenu");
            if (ImGui::IsItemHovered())
                ui::tooltip(node.description.isNotEmpty() ? node.description.toRawUTF8() : name.c_str());
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.88f);
            ui::clippedText(rows, ImVec2(rowTop.x + dp(10.0f), rowTop.y + (rowHeight - ImGui::GetFontSize()) * 0.5f),
                            barLeft - rowTop.x - dp(14.0f), selected ? p.text : p.textDim, name.c_str());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(arrowMin);
            ImGui::InvisibleButton("##arrow", ImVec2(arrowMax.x - arrowMin.x, arrowMax.y - arrowMin.y));
            ui::recordSpot("modulator-arrow:" + std::to_string(id));
            const auto hovered = ImGui::IsItemHovered();
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                arrowDragged_ = true;
                ImGui::SetDragDropPayload(ui::modulatorPayload, &id, sizeof(id));
                ImGui::TextUnformatted(name.c_str());
                ImGui::TextDisabled("Drop it on a knob to modulate it");
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemDeactivated()) {
                if (!std::exchange(arrowDragged_, false)) {
                    if (armed)
                        state_.armedModulator.reset();
                    else
                        state_.armedModulator = std::make_pair(trackKey_, id);
                }
            }
            if (hovered && !ImGui::IsItemActive())
                ui::tooltip(armed ? "Armed: dragging a knob sets how much this modulator moves it (double click: none). Click "
                                    "again, or Escape, to stop."
                                  : "Drag this arrow onto any knob to modulate it, or click it: then dragging a knob sets how "
                                    "much it moves it.");
            rows->AddRectFilled(arrowMin, arrowMax, armed ? rowColour : hovered ? p.bg4 : p.bg3, dp(3.0f));
            arrow(rows, arrowMin, arrowMax, armed ? p.bg0 : rowColour);
            if (ImGui::BeginPopup("##modulatorMenu")) {
                ImGui::TextDisabled("%s", name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Remove its modulations"))
                    clearModulator = id;
                if (ImGui::MenuItem("Remove"))
                    removeModulator = id;
                ImGui::EndPopup();
            }
            ImGui::SetCursorScreenPos(ImVec2(rowTop.x, rowEnd.y + dp(1.0f)));
            ImGui::PopID();
        }
        if (mine.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + size.x - dp(10.0f));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + dp(6.0f));
            ImGui::TextDisabled("%s", "None yet. + adds one; drag its arrow onto a knob.");
            ImGui::PopTextWrapPos();
        }
        // The shown one's settings.
        if (const auto found = modulators_.find(shown); found != modulators_.end()) {
            const auto y = ImGui::GetCursorScreenPos().y + dp(2.0f);
            rows->AddLine(ImVec2(at.x + dp(6.0f), y), ImVec2(at.x + size.x - dp(6.0f), y), p.line, dp(1.0f));
            ImGui::SetCursorScreenPos(ImVec2(at.x, y + dp(4.0f)));
            host_.quiet = true;
            drawSettings(found->second, ImGui::GetCursorScreenPos(), size.x - dp(4.0f), colours_[shown]);
            host_.quiet = false;
        }
        // The last row's end, held by an item.
        ImGui::Dummy(ImVec2(0.0f, dp(2.0f)));
        if (clearModulator)
            for (const auto& modulation : graph_->editorModulations())
                if (modulation.source == *clearModulator)
                    (void) graph_->editorDisconnect(modulation.source, modulation.sourcePort, modulation.node, modulation.port);
        if (removeModulator) {
            if (state_.armedModulator && state_.armedModulator->second == *removeModulator)
                state_.armedModulator.reset();
            (void) graph_->editorRemoveNodes({ *removeModulator });
        }
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void ModulatorsView::drawSettings(const app::NodeGraphComponent::EditorNode& node, const ImVec2 at, const float width,
                                  const ImU32 colour)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto set = [this](const juce::String& binding, const double value) {
        (void) graph_->editorSetParameter(binding, value);
        state_.focused = Panel::devices;
    };
    std::vector<const app::NodeGraphComponent::EditorParameter*> choices;
    std::vector<const app::NodeGraphComponent::EditorParameter*> switches;
    std::vector<const app::NodeGraphComponent::EditorParameter*> knobs;
    std::vector<const app::NodeGraphComponent::EditorParameter*> steps;
    auto stepCount = modulator::stepCount;
    for (const auto& parameter : node.parameters) {
        if (hidden(parameter.binding))
            continue;
        const auto* setting = settingOf(parameter.binding);
        const auto key = setting != nullptr ? setting->key : std::string_view {};
        if (key.starts_with("step") && key != "steps")
            steps.push_back(&parameter);
        else if (!choicesOf(node.typeKey, key).empty())
            choices.push_back(&parameter);
        else if (switchSetting(key))
            switches.push_back(&parameter);
        else
            knobs.push_back(&parameter);
        if (key == "steps")
            stepCount = std::clamp(static_cast<int>(std::lround(parameter.value)), 1, modulator::stepCount);
    }
    auto y = at.y;
    const auto labelWidth = dp(56.0f);
    // Its choices: a name, a list.
    for (const auto* parameter : choices) {
        const auto* setting = settingOf(parameter->binding);
        const auto names = choicesOf(node.typeKey, setting->key);
        const auto index = std::clamp(static_cast<int>(std::lround(parameter->value)), 0, static_cast<int>(names.size()) - 1);
        ImGui::PushID(parameter->binding.toRawUTF8());
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.85f);
        list->AddText(ImVec2(at.x + dp(6.0f), y + dp(3.0f)), p.textDim, parameter->label.toRawUTF8());
        ImGui::SetCursorScreenPos(ImVec2(at.x + labelWidth, y));
        ImGui::SetNextItemWidth(width - labelWidth - dp(4.0f));
        {
            const ui::LearnParameterScope learn(parameter->binding, parameter->label);
            if (ImGui::BeginCombo("##choice", names[static_cast<std::size_t>(index)].c_str(), ImGuiComboFlags_HeightLarge)) {
                for (std::size_t choice = 0; choice < names.size(); ++choice)
                    if (ImGui::Selectable(names[choice].c_str(), static_cast<int>(choice) == index))
                        set(parameter->binding, static_cast<double>(choice));
                ImGui::EndCombo();
            }
            ui::learnItem();
        }
        ImGui::PopFont();
        ImGui::PopID();
        y += dp(24.0f);
    }
    // Its knobs, four a row.
    const auto cell = (width - dp(4.0f)) / 4.0f;
    auto column = 0;
    for (const auto* parameter : knobs) {
        // A free rate is no use while it follows the tempo.
        const auto* setting = settingOf(parameter->binding);
        const auto synced = setting != nullptr && setting->key == "rate" && std::any_of(choices.begin(), choices.end(), [](const auto* choice) {
            return choice->binding.endsWith(".sync") && choice->value >= 0.5;
        });
        ImGui::PushID(parameter->binding.toRawUTF8());
        ImGui::SetCursorScreenPos(ImVec2(at.x + dp(2.0f) + cell * static_cast<float>(column), y));
        ui::KnobStyle style;
        style.diameter = 26.0f;
        style.colour = colour;
        style.label = parameter->label.toRawUTF8();
        style.unit = parameter->unit.isNotEmpty() ? parameter->unit.toRawUTF8() : nullptr;
        style.labelWidth = cell;
        if (parameter->minimum < 0.0 && parameter->maximum > 0.0)
            style.flags |= ui::knobBipolar;
        if (parameter->minimum > 0.0 && parameter->maximum / parameter->minimum >= 100.0)
            style.flags |= ui::knobLogarithmic;
        if (setting != nullptr && (setting->key == "controller" || setting->key == "root"))
            style.flags |= ui::knobInteger;
        auto value = static_cast<float>(parameter->value);
        ImGui::BeginDisabled(synced);
        {
            const ui::LearnParameterScope learn(parameter->binding, parameter->label);
            if (ui::knob("##knob", &value, static_cast<float>(parameter->minimum), static_cast<float>(parameter->maximum),
                         static_cast<float>(parameter->defaultValue), style))
                set(parameter->binding, value);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        if (++column == 4) {
            column = 0;
            y += dp(26.0f) + ImGui::GetFontSize() * 0.86f + dp(6.0f);
        }
    }
    if (column != 0)
        y += dp(26.0f) + ImGui::GetFontSize() * 0.86f + dp(6.0f);
    // Its switches, side by side.
    if (!switches.empty()) {
        ImGui::SetCursorScreenPos(ImVec2(at.x + dp(4.0f), y));
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
        for (std::size_t index = 0; index < switches.size(); ++index) {
            const auto* parameter = switches[index];
            if (index > 0)
                ImGui::SameLine(0.0f, dp(3.0f));
            ImGui::PushID(parameter->binding.toRawUTF8());
            auto on = parameter->value >= 0.5;
            const ui::LearnParameterScope learn(parameter->binding, parameter->label);
            if (ui::toggle(parameter->label.toRawUTF8(), &on, colour))
                set(parameter->binding, on ? 1.0 : 0.0);
            ImGui::PopID();
        }
        ImGui::PopFont();
        y += dp(24.0f);
    }
    // Its steps: bars drawn with the pointer.
    if (!steps.empty()) {
        const ImVec2 min(at.x + dp(4.0f), y + dp(2.0f));
        const ImVec2 max(at.x + width - dp(4.0f), y + dp(52.0f));
        list->AddRectFilled(min, max, p.bg2, dp(3.0f));
        const auto barWidth = (max.x - min.x) / static_cast<float>(stepCount);
        const auto live = graph_->editorControlValue(node.id);
        for (auto index = 0; index < stepCount && index < static_cast<int>(steps.size()); ++index) {
            const auto value = std::clamp(static_cast<float>(steps[static_cast<std::size_t>(index)]->value), 0.0f, 1.0f);
            const auto left = min.x + barWidth * static_cast<float>(index) + dp(1.0f);
            const auto right = left + barWidth - dp(2.0f);
            const auto lit = live && std::abs(*live - value) < 1.0e-3f;
            list->AddRectFilled(ImVec2(left, max.y - (max.y - min.y) * value), ImVec2(right, max.y),
                                lit ? theme::brighter(colour, 0.25f) : theme::alpha(colour, 0.75f), dp(1.0f));
        }
        ImGui::SetCursorScreenPos(min);
        ImGui::InvisibleButton("##steps", ImVec2(max.x - min.x, max.y - min.y));
        if (ImGui::IsItemActive()) {
            const auto mouse = ImGui::GetIO().MousePos;
            const auto index = std::clamp(static_cast<int>((mouse.x - min.x) / barWidth), 0, stepCount - 1);
            const auto value = std::clamp(1.0f - (mouse.y - min.y) / (max.y - min.y), 0.0f, 1.0f);
            if (index < static_cast<int>(steps.size()))
                set(steps[static_cast<std::size_t>(index)]->binding, value);
        }
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            ui::tooltip("Draw the steps with the pointer");
        y = max.y + dp(4.0f);
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x, y));
}

void ModulatorsView::drawBrowser()
{
    const auto popup = ImGui::GetID("##modulatorBrowser");
    if (std::exchange(openBrowser_, false))
        ImGui::OpenPopup(popup);
    ImGui::SetNextWindowSizeConstraints(ImVec2(dp(260.0f), 0.0f), ImVec2(dp(320.0f), dp(520.0f)));
    if (!ImGui::BeginPopupEx(popup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings))
        return;
    const ui::ModuleText english;
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Find a modulator...", browserSearch_.data(), browserSearch_.size());
    const auto filter = juce::String::fromUTF8(browserSearch_.data()).trim();
    std::optional<juce::String> chosen;
    for (const auto& shelf : shelves()) {
        auto header = false;
        for (const auto* type : shelf.types) {
            const auto* effect = engine::effectForType(type);
            if (effect == nullptr)
                continue;
            const auto name = juce::String(std::string(effect->displayName));
            if (filter.isNotEmpty() && !name.containsIgnoreCase(filter) && !juce::String(shelf.name).containsIgnoreCase(filter))
                continue;
            if (!header) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", shelf.name);
                header = true;
            }
            ImGui::PushID(type);
            const auto colour = theme::fromArgb(effect->colour);
            const auto start = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start.x, start.y + dp(4.0f)), ImVec2(start.x + dp(3.0f), start.y + dp(16.0f)),
                                                      colour, dp(1.0f));
            ImGui::SetCursorScreenPos(ImVec2(start.x + dp(9.0f), start.y));
            if (ImGui::Selectable(name.toRawUTF8()))
                chosen = juce::String(type);
            if (ImGui::IsItemHovered()) {
                const auto about = app::tr(juce::String(std::string(effect->descriptionKey)));
                ui::tooltip(about.isNotEmpty() ? about.toRawUTF8() : name.toRawUTF8());
            }
            ImGui::PopID();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (chosen && graph_ != nullptr) {
        const auto made = graph_->editorAddModulator(browserOwner_, *chosen);
        if (made == 0) {
            report("The modulator could not be added");
            return;
        }
        state_.shownModulator[key(browserOwner_)] = made;
        setOpen(browserOwner_, true);
        state_.focused = Panel::devices;
    }
}

// ---- Host: the knobs' side -----------------------------------------------------------------------

void ModulatorsView::Host::modulationsOf(const std::string& binding, std::vector<ui::KnobModulation>& into)
{
    if (const auto found = view->byBinding_.find(binding); found != view->byBinding_.end())
        into.insert(into.end(), found->second.begin(), found->second.end());
}

std::uint64_t ModulatorsView::Host::armed() const
{
    return view->state_.armedModulator && !quiet ? view->state_.armedModulator->second : 0;
}

void ModulatorsView::Host::dropped(const std::string& binding, const std::uint64_t source)
{
    auto& listed = view->byBinding_[binding];
    if (view->graph_ == nullptr || source == 0
        || std::any_of(listed.begin(), listed.end(), [source](const auto& modulation) { return modulation.source == source; }))
        return;
    const auto problem = view->graph_->editorModulate(source, juce::String(binding), 0.25f);
    if (problem.isNotEmpty()) {
        view->report(problem);
        return;
    }
    listed.push_back({ source, 0.25f, view->colours_[source], view->nameOf(source) });
    view->state_.focused = Panel::devices;
}

void ModulatorsView::Host::setAmount(const std::string& binding, const std::uint64_t source, const float amount)
{
    if (view->graph_ == nullptr || source == 0)
        return;
    auto& listed = view->byBinding_[binding];
    const auto entry = std::find_if(listed.begin(), listed.end(), [source](const auto& modulation) { return modulation.source == source; });
    if (amount == 0.0f) {
        (void) view->graph_->editorUnmodulate(source, juce::String(binding));
        if (entry != listed.end())
            listed.erase(entry);
        return;
    }
    const auto problem = view->graph_->editorModulate(source, juce::String(binding), amount);
    if (problem.isNotEmpty()) {
        view->report(problem);
        return;
    }
    if (entry != listed.end())
        entry->amount = amount;
    else
        listed.push_back({ source, amount, view->colours_[source], view->nameOf(source) });
    view->state_.focused = Panel::devices;
}

} // namespace cydaw::v2
