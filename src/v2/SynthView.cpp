#include "SynthView.h"
#include "InstrumentPanels.h"

#include "InstrumentEffects.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <string>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
namespace synth = engine::synth;
using synth_layout::Control;
using synth_layout::Picture;

namespace {

constexpr double kPi = std::numbers::pi;
constexpr double lowestHz = 20.0;
constexpr double highestHz = 20'000.0;
constexpr float windowKnob = 34.0f;
constexpr float cardKnob = 24.0f;

// A section's colour (stored as 0xAARRGGBB) as ImGui draws it.
ImU32 fromArgb(const std::uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xffu, (argb >> 8) & 0xffu, argb & 0xffu, (argb >> 24) & 0xffu);
}

std::string upperCase(const std::string& text)
{
    auto upper = juce::String::fromUTF8(text.c_str()).toUpperCase();
    static const char* const accents[][2] { { "\xc3\xa0", "\xc3\x80" }, { "\xc3\xa2", "\xc3\x82" }, { "\xc3\xa7", "\xc3\x87" },
                                            { "\xc3\xa8", "\xc3\x88" }, { "\xc3\xa9", "\xc3\x89" }, { "\xc3\xaa", "\xc3\x8a" },
                                            { "\xc3\xab", "\xc3\x8b" }, { "\xc3\xae", "\xc3\x8e" }, { "\xc3\xaf", "\xc3\x8f" },
                                            { "\xc3\xb4", "\xc3\x94" }, { "\xc3\xb9", "\xc3\x99" }, { "\xc3\xbb", "\xc3\x9b" } };
    for (const auto& [lower, capital] : accents)
        upper = upper.replace(juce::String::fromUTF8(lower), juce::String::fromUTF8(capital));
    return upper.toStdString();
}

const char* textOf(const char* french, const char* english)
{
    return french == nullptr ? nullptr : L(french, english);
}

// One of a few classic waves at `phase` (periods), its width for a pulse.
double classicWave(const int shape, double phase, const double width)
{
    phase -= std::floor(phase);
    switch (shape) {
    case 0: return 2.0 * phase - 1.0;
    case 1: return phase < width ? 1.0 : -1.0;
    case 2: return phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
    default: return std::sin(2.0 * kPi * phase);
    }
}

// An LFO's shapes as the family draws them: sine, triangle, saw, square, random.
double lfoWave(const int shape, double phase)
{
    phase -= std::floor(phase);
    switch (shape) {
    case 0: return std::sin(2.0 * kPi * phase);
    case 1: return phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
    case 2: return 1.0 - 2.0 * phase;
    case 3: return phase < 0.5 ? 1.0 : -1.0;
    default: return 0.0;
    }
}

// A pseudo-random number in -1..1 for a sample index (the noise drawn).
double hashNoise(const int index)
{
    auto x = static_cast<std::uint32_t>(index) * 2'654'435'761u;
    x ^= x >> 15;
    x *= 2'246'822'519u;
    x ^= x >> 13;
    return static_cast<double>(x & 0xffffu) / 32'767.5 - 1.0;
}

} // namespace

SynthView::SynthView(DeviceHost& studio, UiState& state) : studio_(studio), state_(state), presets_(studio, state) {}

bool SynthView::handles(const juce::String& typeKey)
{
    return synth::modelForType(typeKey.toStdString()) != synth::Model::none;
}

float SynthView::paneWidth(app::NodeGraphComponent& graph, const std::uint64_t nodeId, const float height)
{
    const auto values = graph.editorSynthValues(nodeId);
    if (!values || values->first == synth::Model::none)
        return dp(360.0f);
    // Measured on its settings (a kind's controls come and go); the card's
    // own binding follows when it is drawn.
    card_.model = values->first;
    card_.values = values->second;
    auto width = 0.0f;
    for (const auto& section : cardSections(height))
        width += section.width + dp(4.0f);
    return std::max(dp(120.0f), width - dp(4.0f));
}

bool SynthView::bind(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId)
{
    const auto values = graph.editorSynthValues(nodeId);
    if (!values || values->first == synth::Model::none)
        return false;
    card_.track = track;
    card_.graph = &graph;
    card_.nodeId = nodeId;
    card_.model = values->first;
    card_.values = values->second;
    card_.live = studio_.voiceLive(track, card_.heard);
    // As it plays: the lanes at the playhead, then the voice's modulations.
    card_.played = card_.values;
    const auto prefix = "node." + juce::String(static_cast<juce::int64>(nodeId)) + "." + juce::String(synth::info(card_.model).prefix) + ".";
    for (const auto& binding : studio_.automatedParameters(track))
        if (binding.startsWith(prefix))
            if (const auto index = synth::parameterIndex(card_.model, binding.substring(prefix.length()).toStdString()); index >= 0)
                if (const auto now = studio_.laneValueNow(track, binding))
                    card_.played[static_cast<std::size_t>(index)] = synth::clampValue(card_.model, index, *now);
    if (card_.live)
        for (int index = 0; index < synth::parameterCount(card_.model) && static_cast<std::size_t>(index) < card_.heard.values.size(); ++index)
            if (const auto now = card_.heard.values[static_cast<std::size_t>(index)]; std::isfinite(now))
                card_.played[static_cast<std::size_t>(index)] = now;
    return true;
}

std::optional<float> SynthView::heard(const int index) const
{
    if (index < 0 || index >= synth::parameterCount(card_.model))
        return std::nullopt;
    const auto played = card_.played[static_cast<std::size_t>(index)];
    return played != card_.values[static_cast<std::size_t>(index)] ? std::optional<float>(played) : std::nullopt;
}

int SynthView::indexOf(const char* key) const
{
    return key == nullptr ? -1 : synth::parameterIndex(card_.model, key);
}

float SynthView::value(const int index) const
{
    if (index < 0 || index >= synth::parameterCount(card_.model))
        return 0.0f;
    return card_.values[static_cast<std::size_t>(index)];
}

float SynthView::valueOf(const char* key) const
{
    return value(indexOf(key));
}

juce::String SynthView::bindingOf(const int index) const
{
    return "node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + "." + juce::String(synth::info(card_.model).prefix) + "."
           + juce::String(synth::parameter(card_.model, index).key);
}

void SynthView::set(const int index, const float newValue)
{
    if (card_.graph == nullptr || index < 0 || index >= synth::parameterCount(card_.model))
        return;
    auto clamped = synth::clampValue(card_.model, index, newValue);
    if (synth::parameter(card_.model, index).kind != synth::Kind::continuous)
        clamped = std::round(clamped);
    if (clamped == value(index))
        return;
    card_.values[static_cast<std::size_t>(index)] = clamped;
    (void) card_.graph->editorSetParameter(bindingOf(index), clamped);
    state_.focused = Panel::devices;
}

ImU32 SynthView::colourOf(const int index) const
{
    const auto& info = synth::info(card_.model);
    if (index < 0 || index >= synth::parameterCount(card_.model))
        return synth_layout::layoutFor(card_.model).colour;
    const auto section = synth::parameter(card_.model, index).section;
    if (section < 0 || section >= static_cast<int>(info.sections.size()))
        return synth_layout::layoutFor(card_.model).colour;
    return fromArgb(info.sections[static_cast<std::size_t>(section)].colour);
}

// ---- The controls ---------------------------------------------------------------------------------

bool SynthView::met(const Control& item) const
{
    if (item.onlyWhen == nullptr)
        return true;
    const auto other = valueOf(item.onlyWhen);
    return other >= item.from - 1.0e-4f && other <= item.to + 1.0e-4f;
}

ImVec2 SynthView::controlSize(const Control& item, const float diameter) const
{
    const auto index = indexOf(item.key);
    if (index < 0 || (item.hide && !met(item)))
        return ImVec2(0.0f, 0.0f);
    switch (synth::parameter(card_.model, index).kind) {
    case synth::Kind::choice: {
        const auto wide = item.width > 0.0f ? item.width : 104.0f;
        return ImVec2(dp(compact_ ? std::min(wide, 92.0f) : wide), ImGui::GetFrameHeight());
    }
    case synth::Kind::toggle: {
        const auto* label = textOf(item.french, item.english);
        const auto text = ImGui::CalcTextSize(label != nullptr ? label : "").x;
        return ImVec2(std::max(dp(62.0f), text + dp(18.0f)), ImGui::GetFrameHeight());
    }
    default: {
        // On the card, as wide as its name (within reason): no name over another.
        auto width = dp(diameter + 14.0f);
        if (compact_) {
            const auto* label = textOf(item.french, item.english);
            ImGui::PushFont(nullptr, ui::currentFontBase() * labelScale());
            width = std::max(width, std::min(dp(64.0f), ImGui::CalcTextSize(label != nullptr ? label : "").x + dp(4.0f)));
            ImGui::PopFont();
        }
        return ImVec2(width, dp(diameter) + ui::currentFontBase() * labelScale() + dp(3.0f));
    }
    }
}

void SynthView::describeCondition(const Control& item)
{
    const auto other = indexOf(item.onlyWhen);
    if (other < 0)
        return;
    const auto& waited = synth::parameter(card_.model, other);
    const auto from = synth::displayValue(card_.model, other, item.from, ui::french());
    const auto to = synth::displayValue(card_.model, other, item.to, ui::french());
    const auto upward = item.to >= waited.maximum - 1.0e-4f;
    const auto name = ui::french() ? waited.french : waited.english;
    // Its short name: the one under the knob it waits on ("UNISSON"), else
    // the last part of its own ("FORME"), and the values that wake it.
    std::string who;
    for (const auto& page : synth_layout::layoutFor(card_.model).pages)
        for (const auto& row : page.rows)
            for (const auto& panel : row)
                for (const auto& control : panel.controls)
                    if (who.empty() && control.key != nullptr && std::string_view(control.key) == item.onlyWhen)
                        if (const auto* label = textOf(control.french, control.english))
                            who = label;
    if (who.empty()) {
        who = name;
        if (const auto dot = who.rfind("\xc2\xb7 "); dot != std::string::npos)
            who = who.substr(dot + 3);
        who = upperCase(who);
    }
    greyedWho_ = (ui::french() ? "si " : "if ") + who;
    greyedWhen_ = item.from == item.to ? "= " + from : upward ? "\xe2\x89\xa5 " + from : from + " \xe2\x80\x93 " + to;
    // A few choices by their names ("= Multimode / SVF").
    if (waited.kind == synth::Kind::choice && item.from != item.to && item.to - item.from <= 2.5f) {
        greyedWhen_ = "=";
        for (auto choice = std::lround(item.from); choice <= std::lround(item.to); ++choice)
            greyedWhen_ += (choice == std::lround(item.from) ? " " : " / ")
                           + synth::displayValue(card_.model, other, static_cast<float>(choice), ui::french());
    }
    if (ui::french())
        greyedBecause_ = "Grisé : agit quand « " + name + " » "
                         + (item.from == item.to ? "vaut " + from : upward ? "vaut " + from + " ou plus" : "est entre " + from + " et " + to);
    else
        greyedBecause_ = "Greyed: acts when \"" + name + "\" "
                         + (item.from == item.to ? "is " + from : upward ? "is " + from + " or more" : "is between " + from + " and " + to);
}

void SynthView::control(const Control& item, const float diameter)
{
    const auto index = indexOf(item.key);
    if (index < 0)
        return;
    const auto enabled = met(item);
    if (!enabled && item.hide)
        return;
    if (!enabled) {
        // Plainly off: far dimmer than a control that acts; what would make
        // it act said on it (or over its group) and in its tooltip.
        ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, 0.32f);
        ImGui::BeginDisabled();
        describeCondition(item);
    }
    const auto* label = textOf(item.french, item.english);
    switch (synth::parameter(card_.model, index).kind) {
    case synth::Kind::choice: choice(index, controlSize(item, diameter).x); break;
    case synth::Kind::toggle: toggle(index, label != nullptr ? label : "", controlSize(item, diameter).x); break;
    default:
        knobCell_ = controlSize(item, diameter).x;
        knob(index, label, diameter);
        knobCell_ = 0.0f;
        break;
    }
    if (!enabled) {
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
    }
    greyedBecause_.clear();
    greyedWho_.clear();
    greyedWhen_.clear();
}

void SynthView::markGreyed(const bool knob) const
{
    // A knob says it under itself; a group says it once over all.
    if (greyedWho_.empty() || knob || quietGreyed_)
        return;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    // In place of its value, inside it: all of it when it fits, else whom
    // it waits on.
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.7f);
    const auto whole = greyedWho_ + " " + greyedWhen_;
    const auto& text = ImGui::CalcTextSize(whole.c_str()).x + dp(8.0f) <= max.x - min.x ? whole : greyedWho_;
    list->AddRectFilled(min, max, theme::alpha(p.bg1, 0.96f), dp(3.0f));
    list->AddRect(min, max, theme::alpha(p.warn, 0.7f), dp(3.0f), dp(1.0f));
    list->PushClipRect(ImVec2(min.x + dp(2.0f), min.y), ImVec2(max.x - dp(2.0f), max.y), true);
    const auto size = ImGui::CalcTextSize(text.c_str());
    list->AddText(ImVec2(std::max(min.x + dp(4.0f), (min.x + max.x - size.x) * 0.5f), (min.y + max.y - size.y) * 0.5f), p.warn,
                  text.c_str());
    list->PopClipRect();
    ImGui::PopFont();
}

bool SynthView::knob(const int index, const char* label, const float diameter)
{
    const auto& p = theme::palette();
    const auto& parameter = synth::parameter(card_.model, index);
    ImGui::PushID(index);
    ImGui::BeginGroup();
    auto normalised = synth::normalise(card_.model, index, value(index));
    const auto defaultNormalised = synth::normalise(card_.model, index, parameter.defaultValue);
    ui::KnobStyle style;
    style.diameter = diameter;
    style.flags = ui::knobNoLabel;
    if (parameter.minimum < 0.0f && parameter.maximum > 0.0f)
        style.flags |= ui::knobBipolar;
    // A whole number (voices, octaves): the knob lands on each.
    if (parameter.kind != synth::Kind::continuous)
        style.steps = static_cast<int>(std::lround(parameter.maximum - parameter.minimum));
    const auto greyed = !greyedWho_.empty();
    style.colour = greyed ? p.textFaint : colourOf(index);
    if (const auto moved = heard(index); moved && !greyed)
        style.modulation = synth::normalise(card_.model, index, *moved);
    const auto origin = ImGui::GetCursorScreenPos();
    // In a cell as wide as its name (on the card), the knob in its middle.
    const auto width = std::max(dp(diameter + 14.0f), knobCell_);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (width - dp(diameter + 14.0f)) * 0.5f, origin.y));
    const auto changed = ui::knob("##knob", &normalised, 0.0f, 1.0f, defaultNormalised, style);
    markGreyed(true);
    const auto hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const auto active = ImGui::IsItemActive();
    if (changed)
        set(index, synth::denormalise(card_.model, index, normalised));
    const auto name = juce::String::fromUTF8((ui::french() ? parameter.french : parameter.english).c_str());
    const auto shown = synth::displayValue(card_.model, index, value(index), ui::french());
    // Right click: its default, a lane in the arrangement.
    if (ImGui::BeginPopupContextItem("##knobMenu")) {
        ImGui::TextDisabled("%s", name.toRawUTF8());
        if (ImGui::MenuItem(L("Valeur par défaut", "Default value")))
            set(index, parameter.defaultValue);
        const auto binding = bindingOf(index);
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
        ImGui::EndPopup();
    }
    if (hovered && !active)
        ImGui::SetTooltip("%s : %s\n%s", name.toRawUTF8(), shown.c_str(),
                          !greyedBecause_.empty() ? greyedBecause_.c_str()
                                                  : L("Double-clic : valeur de départ · clic droit : automatiser",
                                                      "Double click: default · right click: automate"));
    // Its name, its value while touched; greyed alone, what it waits on.
    const auto waits = greyed && !quietGreyed_ && !hovered && !active;
    const auto text = hovered || active ? juce::String::fromUTF8(shown.c_str())
                      : waits           ? juce::String::fromUTF8(greyedWho_.c_str())
                                        : juce::String::fromUTF8(label != nullptr ? label : "");
    auto* list = ImGui::GetWindowDrawList();
    ImGui::PushFont(nullptr, ui::currentFontBase() * labelScale());
    const auto textHeight = ImGui::GetFontSize();
    const auto size = ImGui::CalcTextSize(text.toRawUTF8());
    const auto y = origin.y + dp(diameter) + dp(1.0f);
    const auto spill = compact_ ? 0.0f : dp(8.0f);
    list->PushClipRect(ImVec2(origin.x - spill, y), ImVec2(origin.x + width + spill, y + textHeight + dp(2.0f)), true);
    list->AddText(ImVec2(origin.x + std::max(-dp(8.0f), (width - size.x) * 0.5f), y),
                  waits ? theme::alpha(p.warn, 0.85f) : greyed ? p.textFaint : hovered || active ? p.text : p.textDim, text.toRawUTF8());
    list->PopClipRect();
    ImGui::PopFont();
    // Its name, or its number: a click on it types the value in.
    auto typed = static_cast<double>(value(index));
    if (ui::valueEntryButton("##number", ImVec2(width, textHeight + dp(2.0f)), typed, parameter.minimum, parameter.maximum,
                             parameter.unit.empty() && parameter.minimum >= -1.0f && parameter.maximum <= 1.0f ? 100.0 : 1.0, parameter.unit.c_str()))
        set(index, static_cast<float>(typed));
    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

void SynthView::choice(const int index, const float width)
{
    const auto& parameter = synth::parameter(card_.model, index);
    ImGui::PushID(index);
    const auto count = static_cast<int>(parameter.choices.size());
    const auto current = std::clamp(static_cast<int>(std::lround(value(index))), 0, std::max(0, count - 1));
    ImGui::SetNextItemWidth(width);
    const auto shown = synth::displayValue(card_.model, index, static_cast<float>(current), ui::french());
    if (ImGui::BeginCombo("##choice", shown.c_str())) {
        for (int at = 0; at < count; ++at) {
            const auto name = synth::displayValue(card_.model, index, static_cast<float>(at), ui::french());
            if (ImGui::Selectable(name.c_str(), at == current))
                set(index, static_cast<float>(at));
        }
        ImGui::EndCombo();
    }
    markGreyed(false);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s%s%s", (ui::french() ? parameter.french : parameter.english).c_str(), greyedBecause_.empty() ? "" : "\n",
                          greyedBecause_.c_str());
    ImGui::PopID();
}

void SynthView::toggle(const int index, const char* label, const float width)
{
    const auto& parameter = synth::parameter(card_.model, index);
    ImGui::PushID(index);
    auto on = value(index) >= 0.5f;
    if (ui::toggle(label, &on, colourOf(index), ImVec2(width, 0.0f), (ui::french() ? parameter.french : parameter.english).c_str()))
        set(index, on ? 1.0f : 0.0f);
    markGreyed(false);
    ImGui::PopID();
}

float SynthView::flow(const std::vector<Control>& controls, const ImVec2 origin, const float width, const float diameter, const bool draw,
                      const float maxHeight)
{
    const auto gap = dp(6.0f);
    struct Placed {
        const Control* item {};
        ImVec2 at;
        ImVec2 size;
        int line {};
    };
    std::vector<Placed> placed;
    auto x = origin.x;
    auto y = origin.y;
    auto lineHeight = 0.0f;
    auto line = 0;
    auto bottom = origin.y;
    clipped_ = false;
    for (const auto& item : controls) {
        const auto size = controlSize(item, diameter);
        if (size.x <= 0.0f)
            continue;
        if (x > origin.x && ((item.newLine && !compact_) || x + size.x > origin.x + width)) {
            x = origin.x;
            y += lineHeight + gap;
            lineHeight = 0.0f;
            ++line;
        }
        if (maxHeight > 0.0f && y + size.y - origin.y > maxHeight + 0.5f) {
            // No room left: the rest is in the window.
            clipped_ = true;
            break;
        }
        placed.push_back({ &item, ImVec2(x, y), size, line });
        x += size.x + gap;
        lineHeight = std::max(lineHeight, size.y);
        bottom = std::max(bottom, y + lineHeight);
    }
    if (!draw)
        return bottom - origin.y;
    // Knobs side by side on a line, greyed for the same reason: one group
    // (a choice or a switch says it inside itself).
    const auto knobbed = [this](const Control& item) {
        const auto index = indexOf(item.key);
        return index >= 0 && synth::parameter(card_.model, index).kind != synth::Kind::choice
               && synth::parameter(card_.model, index).kind != synth::Kind::toggle;
    };
    const auto same = [this, &knobbed](const Control& a, const Control& b) {
        return a.onlyWhen != nullptr && b.onlyWhen != nullptr && std::string_view(a.onlyWhen) == b.onlyWhen && a.from == b.from
               && a.to == b.to && !met(a) && knobbed(a) && knobbed(b);
    };
    std::vector<std::pair<std::size_t, std::size_t>> groups;
    for (std::size_t at = 0; at < placed.size();) {
        auto end = at + 1;
        while (end < placed.size() && placed[end].line == placed[at].line && same(*placed[at].item, *placed[end].item))
            ++end;
        if (end - at >= 2)
            groups.emplace_back(at, end);
        at = end;
    }
    for (std::size_t at = 0; at < placed.size(); ++at) {
        quietGreyed_ = std::any_of(groups.begin(), groups.end(), [at](const auto& group) { return at >= group.first && at < group.second; });
        ImGui::SetCursorScreenPos(placed[at].at);
        control(*placed[at].item, diameter);
    }
    quietGreyed_ = false;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    for (const auto& [first, end] : groups) {
        // Over the group's knobs (or switches): what would wake them.
        const auto left = placed[first].at.x;
        const auto right = placed[end - 1].at.x + placed[end - 1].size.x;
        auto top = placed[first].at.y;
        auto low = top;
        for (auto at = first; at < end; ++at) {
            low = std::max(low, placed[at].at.y + dp(diameter));
            top = std::min(top, placed[at].at.y);
        }
        describeCondition(*placed[first].item);
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.66f);
        const auto lineSize = ImGui::GetFontSize();
        const auto whole = greyedWho_ + " " + greyedWhen_;
        const auto oneLine = ImGui::CalcTextSize(whole.c_str()).x + dp(10.0f) <= right - left;
        const auto widest = oneLine ? ImGui::CalcTextSize(whole.c_str()).x
                                    : std::max(ImGui::CalcTextSize(greyedWho_.c_str()).x, ImGui::CalcTextSize(greyedWhen_.c_str()).x);
        const auto half = std::min(widest * 0.5f + dp(5.0f), (right - left) * 0.5f);
        const auto centre = ImVec2((left + right) * 0.5f, (top + low) * 0.5f);
        const auto lines = oneLine ? 1.0f : 2.0f;
        const ImVec2 boxMin(centre.x - half, centre.y - lineSize * lines * 0.5f - dp(2.0f));
        const ImVec2 boxMax(centre.x + half, centre.y + lineSize * lines * 0.5f + dp(2.0f));
        list->AddRectFilled(boxMin, boxMax, theme::alpha(p.bg1, 0.94f), dp(4.0f));
        list->AddRect(boxMin, boxMax, theme::alpha(p.warn, 0.7f), dp(4.0f), dp(1.0f));
        list->PushClipRect(boxMin, boxMax, true);
        const auto put = [&](const std::string& words, const float at, const ImU32 colour) {
            const auto size = ImGui::CalcTextSize(words.c_str());
            list->AddText(ImVec2(std::max(boxMin.x + dp(4.0f), centre.x - size.x * 0.5f), at), colour, words.c_str());
        };
        if (oneLine) {
            put(whole, centre.y - lineSize * 0.5f, p.warn);
        } else {
            put(greyedWho_, centre.y - lineSize, p.warn);
            put(greyedWhen_, centre.y, p.text);
        }
        list->PopClipRect();
        ImGui::PopFont();
        if (ImGui::IsMouseHoveringRect(boxMin, boxMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            ImGui::SetTooltip("%s", greyedBecause_.c_str());
        greyedBecause_.clear();
        greyedWho_.clear();
        greyedWhen_.clear();
    }
    return bottom - origin.y;
}

float SynthView::pictureHeight(const Picture picture) const
{
    switch (picture) {
    case Picture::wave: return dp(64.0f);
    case Picture::filter: return dp(84.0f);
    case Picture::envelope: return dp(64.0f);
    case Picture::lfo: return dp(52.0f);
    case Picture::steps: return dp(96.0f);
    case Picture::orb: return dp(150.0f);
    default: return 0.0f;
    }
}

float SynthView::panelHeight(const synth_layout::Panel& item, const float width) const
{
    const auto padding = ImGui::GetStyle().WindowPadding;
    const auto inner = width - padding.x * 2.0f;
    auto height = padding.y * 2.0f + ImGui::GetTextLineHeightWithSpacing();
    if (item.picture != Picture::none)
        height += pictureHeight(item.picture) + dp(6.0f);
    // Measuring only: the flow draws nothing.
    height += const_cast<SynthView*>(this)->flow(item.controls, ImVec2(0.0f, 0.0f), inner, windowKnob, false);
    return height;
}

ImU32 SynthView::sectionColour(const int section) const
{
    const auto& info = synth::info(card_.model);
    return section >= 0 && section < static_cast<int>(info.sections.size()) ? fromArgb(info.sections[static_cast<std::size_t>(section)].colour)
                                                                            : synth_layout::layoutFor(card_.model).colour;
}

float SynthView::labelScale() const
{
    return compact_ ? 0.7f : 0.78f;
}

float SynthView::cardTitleHeight() const
{
    return ui::currentFontBase() * 0.78f + dp(8.0f);
}

float SynthView::compactPictureHeight(const Picture picture) const
{
    switch (picture) {
    case Picture::wave: return dp(40.0f);
    case Picture::filter: return dp(50.0f);
    case Picture::envelope: return dp(40.0f);
    case Picture::lfo: return dp(32.0f);
    case Picture::steps: return dp(50.0f);
    case Picture::orb: return dp(90.0f);
    default: return 0.0f;
    }
}

std::vector<SynthView::CardSection> SynthView::cardSections(const float height)
{
    const auto& layout = synth_layout::layoutFor(card_.model);
    std::vector<const synth_layout::Panel*> panels;
    for (const auto& item : layout.card)
        panels.push_back(&item);
    compact_ = true;
    const auto padding = dp(5.0f);
    const auto inner = height - cardTitleHeight() - padding;
    const auto knobHeight = dp(cardKnob) + ui::currentFontBase() * labelScale() + dp(3.0f);
    const auto cell = dp(cardKnob + 14.0f + 6.0f);
    std::vector<CardSection> sections;
    for (const auto* item : panels) {
        const auto pictureHeight = compactPictureHeight(item->picture);
        const auto pictured = item->picture != Picture::none && inner - pictureHeight - dp(4.0f) >= knobHeight;
        const auto room = inner - (pictured ? pictureHeight + dp(4.0f) : 0.0f);
        // The narrowest that holds all its controls in that height.
        auto widest = 0.0f;
        for (const auto& control : item->controls)
            widest = std::max(widest, controlSize(control, cardKnob).x);
        auto width = std::max(widest, pictured ? dp(item->picture == Picture::filter ? 150.0f : 110.0f) : cell);
        while (width < dp(560.0f) && flow(item->controls, ImVec2(0.0f, 0.0f), width, cardKnob, false) > room + 0.5f)
            width += cell;
        sections.push_back({ item, width + padding * 2.0f, pictured });
    }
    compact_ = false;
    return sections;
}

void SynthView::panel(const synth_layout::Panel& item, const float width, const float height)
{
    const auto colour = sectionColour(item.section);
    ImGui::BeginChild(item.id, ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextColored(theme::vec(colour), "%s", L(item.french, item.english));
    ImGui::PopFont();
    const auto inner = ImGui::GetContentRegionAvail();
    auto origin = ImGui::GetCursorScreenPos();
    if (item.picture != Picture::none) {
        // The picture takes what the controls leave.
        const auto controlsHeight = flow(item.controls, origin, inner.x, windowKnob, false);
        const auto pictureSize = ImVec2(inner.x, std::max(pictureHeight(item.picture), inner.y - controlsHeight - dp(6.0f)));
        drawPicture(item.picture, item.pictureKey, item.secondKey, origin, pictureSize, true);
        origin.y += pictureSize.y + dp(6.0f);
    }
    const auto used = flow(item.controls, origin, inner.x, windowKnob, true);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + used));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
}

// ---- The pictures ---------------------------------------------------------------------------------

void SynthView::drawPicture(const Picture picture, const char* key, const char* second, const ImVec2 origin, const ImVec2 size,
                            const bool interactive)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    list->AddRectFilled(origin, end, p.bg0, dp(4.0f));
    list->AddRect(origin, end, p.line, dp(4.0f));
    list->PushClipRect(origin, end, true);
    switch (picture) {
    case Picture::wave:
        drawWave(list, key, origin, size, key != nullptr ? colourOf(indexOf((std::string(key) + ".level").c_str()))
                                                          : synth_layout::layoutFor(card_.model).colour);
        break;
    case Picture::filter: drawFilter(list, key, second, origin, size, interactive); break;
    case Picture::envelope: drawEnvelope(list, key, origin, size, interactive); break;
    case Picture::lfo: drawLfo(list, key, origin, size); break;
    case Picture::steps: drawSteps(list, key, origin, size, interactive); break;
    case Picture::orb: drawOrb(list, key, origin, size, interactive); break;
    default: break;
    }
    list->PopClipRect();
}

double SynthView::waveAt(const double phase) const
{
    switch (card_.model) {
    case synth::Model::ambre: {
        const auto offset = [this](const char* octave, const char* semi, const char* fine) {
            return 12.0 * std::round(valueOf(octave)) + (semi != nullptr ? std::round(valueOf(semi)) : 0.0)
                   + (fine != nullptr ? valueOf(fine) / 100.0 : 0.0);
        };
        const auto r1 = std::pow(2.0, offset("osc1.octave", nullptr, nullptr) / 12.0);
        const auto r2 = std::pow(2.0, offset("osc2.octave", "osc2.semi", "osc2.fine") / 12.0);
        const auto r3 = std::pow(2.0, offset("osc3.octave", "osc3.semi", "osc3.fine") / 12.0);
        const auto shape = [this](const char* key) { return static_cast<int>(std::lround(valueOf(key))); };
        const auto o3 = classicWave(shape("osc3.wave"), phase * r3, 0.5);
        const auto o1 = classicWave(shape("osc1.wave"), phase * r1 + 0.3 * valueOf("fm.amount") * o3, valueOf("osc1.pw"));
        const auto within = phase * r1 - std::floor(phase * r1);
        const auto p2 = valueOf("osc2.sync") >= 0.5f ? within * r2 / r1 : phase * r2;
        const auto o2 = classicWave(shape("osc2.wave"), p2, valueOf("osc2.pw"));
        return valueOf("osc1.level") * o1 + valueOf("osc2.level") * o2 + valueOf("osc3.level") * o3;
    }
    default: return std::sin(2.0 * kPi * phase);
    }
}

void SynthView::drawWave(ImDrawList* list, const char* key, const ImVec2 origin, const ImVec2 size, const ImU32 colour)
{
    const auto& p = theme::palette();
    const auto left = origin.x + dp(4.0f);
    const auto width = std::max(2.0f, size.x - dp(8.0f));
    const auto middle = origin.y + size.y * 0.5f;
    const auto half = size.y * 0.42f;
    list->AddLine(ImVec2(left, middle), ImVec2(left + width, middle), p.grid);
    const auto count = static_cast<int>(width);
    // As it plays now (a table's position automated, swept): the settings
    // played stand in for the knobs while it is drawn.
    struct Playing {
        Card& card;
        explicit Playing(Card& c) : card(c) { std::swap(card.values, card.played); }
        ~Playing() { std::swap(card.values, card.played); }
    } playing { card_ };
    // One oscillator as its engine draws it, or the oscillators' sum.
    const auto drawn = key != nullptr ? synth::picture(card_.model, card_.values, key, count + 1) : std::vector<float> {};
    std::vector<double> samples(static_cast<std::size_t>(count) + 1);
    auto peak = 1.0e-6;
    const auto noise = card_.model == synth::Model::ambre && key == nullptr ? static_cast<double>(valueOf("noise.level")) : 0.0;
    for (int at = 0; at <= count; ++at) {
        // Two periods of the note.
        const auto x = static_cast<std::size_t>(at) < drawn.size()
                           ? static_cast<double>(drawn[static_cast<std::size_t>(at)])
                           : waveAt(2.0 * static_cast<double>(at) / static_cast<double>(count)) + noise * 0.6 * hashNoise(at);
        samples[static_cast<std::size_t>(at)] = x;
        peak = std::max(peak, std::abs(x));
    }
    // An oscillator keeps its own scale (a quiet frame looks quiet); a sum fills the picture.
    const auto scale = drawn.empty() ? peak : std::max(1.0, peak);
    const auto sourceIndex = key != nullptr ? indexOf((std::string(key) + ".source").c_str()) : -1;
    const auto granular = sourceIndex >= 0 && std::lround(value(sourceIndex)) == 1;
    if (granular) {
        // A texture from end to end, as loud as it is at each place; where it
        // reads, and a grain's length there.
        for (int at = 0; at <= count; ++at) {
            const auto amount = static_cast<float>(samples[static_cast<std::size_t>(at)] / std::max(1.0e-6, peak)) * half;
            list->AddLine(ImVec2(left + static_cast<float>(at), middle - amount), ImVec2(left + static_cast<float>(at), middle + amount + 1.0f),
                          (colour & 0x00ffffffu) | 0x90000000u);
        }
        const auto scan = std::clamp(valueOf((std::string(key) + ".scan").c_str()), 0.0f, 1.0f);
        const auto grain = static_cast<float>(valueOf((std::string(key) + ".grain").c_str()) / engine::synth::textureSeconds);
        const auto x = left + scan * width;
        list->AddRectFilled(ImVec2(x, origin.y + dp(3.0f)), ImVec2(std::min(left + width, x + std::max(dp(2.0f), grain * width)),
                                                                     origin.y + size.y - dp(3.0f)),
                            (p.text & 0x00ffffffu) | 0x30000000u);
        list->AddLine(ImVec2(x, origin.y + dp(3.0f)), ImVec2(x, origin.y + size.y - dp(3.0f)), p.text, dp(1.5f));
    } else {
        std::vector<ImVec2> points;
        points.reserve(samples.size());
        for (int at = 0; at <= count; ++at)
            points.emplace_back(left + static_cast<float>(at), middle - static_cast<float>(samples[static_cast<std::size_t>(at)] / scale) * half);
        list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(1.6f));
    }
    // What it reads: an oscillator's table and position; a sum, its unison.
    std::string text;
    if (key != nullptr) {
        // An engine of several kinds (or a layer of two sources) says which;
        // its tables only when it reads them, its texture when it has grains.
        const auto kind = sourceIndex >= 0 ? sourceIndex : indexOf((std::string(key) + ".type").c_str());
        const auto table = indexOf((std::string(key) + ".table").c_str());
        const auto position = indexOf((std::string(key) + ".position").c_str());
        const auto reads = kind < 0 || std::lround(value(kind)) == (sourceIndex >= 0 ? 0 : 1);
        if (kind >= 0)
            text = synth::displayValue(card_.model, kind, value(kind), ui::french());
        if (granular)
            if (const auto texture = indexOf((std::string(key) + ".texture").c_str()); texture >= 0)
                text += " \xc2\xb7 " + synth::displayValue(card_.model, texture, value(texture), ui::french());
        if (reads && table >= 0)
            text += (text.empty() ? "" : " \xc2\xb7 ") + synth::displayValue(card_.model, table, value(table), ui::french());
        if (reads && position >= 0)
            text += " \xc2\xb7 " + synth::displayValue(card_.model, position, value(position), ui::french());
    } else if (indexOf("unison.voices") >= 0 && valueOf("unison.voices") >= 1.5f) {
        text = "\xc3\x97" + std::to_string(static_cast<int>(std::lround(valueOf("unison.voices"))));
    }
    if (!text.empty()) {
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        list->AddText(ImVec2(origin.x + size.x - ImGui::CalcTextSize(text.c_str()).x - dp(5.0f), origin.y + dp(2.0f)), p.textDim, text.c_str());
        ImGui::PopFont();
    }
}

double SynthView::filterDb(const double hz) const
{
    return instrument_ui::filterResponse(card_.model,card_.values,hz);
}

void SynthView::drawFilter(ImDrawList* list, const char* key, const char* second, const ImVec2 origin, const ImVec2 size,
                           const bool interactive)
{
    const auto& p = theme::palette();
    const auto prefix = std::string(key != nullptr ? key : "filter");
    struct Handle {
        int cutoff { -1 };
        int resonance { -1 };
    };
    std::array<Handle, 2> handles {};
    handles[0] = { indexOf((prefix + ".cutoff").c_str()), indexOf((prefix + ".resonance").c_str()) };
    if (second != nullptr)
        handles[1] = { indexOf((std::string(second) + ".cutoff").c_str()), indexOf((std::string(second) + ".resonance").c_str()) };
    if (handles[0].cutoff < 0)
        return;
    const auto colour = colourOf(handles[0].cutoff);
    const auto left = origin.x + dp(4.0f);
    const auto right = origin.x + size.x - dp(4.0f);
    const auto top = origin.y + dp(6.0f);
    const auto bottom = origin.y + size.y - dp(4.0f);
    const auto width = std::max(1.0f, right - left);
    constexpr double topDb = 24.0;
    constexpr double bottomDb = -48.0;
    const auto xOf = [&](const double hz) {
        return left + static_cast<float>(std::log(std::clamp(hz, lowestHz, highestHz) / lowestHz) / std::log(highestHz / lowestHz)) * width;
    };
    const auto hzOf = [&](const float x) {
        return lowestHz * std::pow(highestHz / lowestHz, std::clamp(static_cast<double>((x - left) / width), 0.0, 1.0));
    };
    const auto yOf = [&](const double db) {
        return top + static_cast<float>((topDb - std::clamp(db, bottomDb, topDb)) / (topDb - bottomDb)) * (bottom - top);
    };
    // The grid: decades, and 0 dB.
    for (const auto hz : { 100.0, 1'000.0, 10'000.0 })
        list->AddLine(ImVec2(xOf(hz), top), ImVec2(xOf(hz), bottom), p.grid);
    list->AddLine(ImVec2(left, yOf(0.0)), ImVec2(right, yOf(0.0)), p.gridStrong);
    // The wave faintly behind on the card.
    if (!interactive) {
        std::vector<ImVec2> wave;
        const auto count = static_cast<int>(width);
        auto peak = 1.0e-6;
        std::vector<double> samples(static_cast<std::size_t>(count) + 1);
        for (int at = 0; at <= count; ++at) {
            samples[static_cast<std::size_t>(at)] = waveAt(3.0 * at / std::max(1, count));
            peak = std::max(peak, std::abs(samples[static_cast<std::size_t>(at)]));
        }
        const auto middle = (top + bottom) * 0.5f;
        for (int at = 0; at <= count; ++at)
            wave.emplace_back(left + static_cast<float>(at),
                              middle - static_cast<float>(samples[static_cast<std::size_t>(at)] / peak) * (bottom - top) * 0.3f);
        list->AddPolyline(wave.data(), static_cast<int>(wave.size()), (p.textFaint & 0x00ffffffu) | 0x50000000u, dp(1.0f));
    }
    // The curve, filled under.
    std::vector<ImVec2> curve;
    const auto steps = static_cast<int>(width / 2.0f);
    for (int at = 0; at <= steps; ++at) {
        const auto x = left + width * static_cast<float>(at) / static_cast<float>(std::max(1, steps));
        curve.emplace_back(x, yOf(filterDb(hzOf(x))));
    }
    for (std::size_t at = 1; at < curve.size(); ++at)
        list->AddQuadFilled(curve[at - 1], curve[at], ImVec2(curve[at].x, bottom), ImVec2(curve[at - 1].x, bottom),
                            (colour & 0x00ffffffu) | 0x30000000u);
    list->AddPolyline(curve.data(), static_cast<int>(curve.size()), colour, dp(1.8f));
    // Where the newest note's modulations put it now.
    if (card_.live) {
        auto moved = card_.values;
        auto any = false;
        for (const auto& handle : handles)
            for (const auto index : { handle.cutoff, handle.resonance })
                if (const auto now = heard(index)) {
                    moved[static_cast<std::size_t>(index)] = *now;
                    any = true;
                }
        if (any) {
            std::swap(card_.values, moved);
            std::vector<ImVec2> live;
            for (int at = 0; at <= steps; ++at) {
                const auto x = left + width * static_cast<float>(at) / static_cast<float>(std::max(1, steps));
                live.emplace_back(x, yOf(filterDb(hzOf(x))));
            }
            const auto hz = static_cast<double>(value(handles[0].cutoff));
            const ImVec2 point(xOf(hz), std::clamp(yOf(filterDb(hz)), top + dp(5.0f), bottom - dp(5.0f)));
            std::swap(card_.values, moved);
            list->AddPolyline(live.data(), static_cast<int>(live.size()), theme::alpha(p.modulation, 0.9f), dp(1.4f));
            list->AddCircleFilled(point, dp(3.5f), p.modulation);
        }
    }
    // The handles: each filter's cutoff on the curve.
    const auto handleCount = handles[1].cutoff >= 0 ? 2 : 1;
    std::array<ImVec2, 2> points {};
    for (int at = 0; at < handleCount; ++at) {
        const auto hz = static_cast<double>(value(handles[static_cast<std::size_t>(at)].cutoff));
        points[static_cast<std::size_t>(at)] = ImVec2(xOf(hz), std::clamp(yOf(filterDb(hz)), top + dp(5.0f), bottom - dp(5.0f)));
    }
    {
        auto text = synth::displayValue(card_.model, handles[0].cutoff, value(handles[0].cutoff), ui::french());
        if (handleCount == 2)
            text = "1 : " + text + "   2 : " + synth::displayValue(card_.model, handles[1].cutoff, value(handles[1].cutoff), ui::french());
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        list->AddText(ImVec2(right - ImGui::CalcTextSize(text.c_str()).x - dp(2.0f), top - dp(3.0f)), p.textDim, text.c_str());
        ImGui::PopFont();
    }
    if (!interactive) {
        list->AddCircleFilled(points[0], dp(3.0f), colour);
        return;
    }
    // Dragged: the nearest handle, across its cutoff, up and down its resonance.
    const auto id = "##filter." + prefix;
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(id.c_str(), size);
    const auto widget = ImGui::GetItemID();
    const auto hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    auto nearest = 0;
    if (handleCount == 2) {
        const auto distance = [&io](const ImVec2 at) { return std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y); };
        nearest = distance(points[1]) < distance(points[0]) ? 1 : 0;
    }
    if (ImGui::IsItemActivated()) {
        dragging_ = widget;
        draggingPoint_ = nearest;
    }
    if (!ImGui::IsItemActive() && dragging_ == widget)
        dragging_ = 0;
    const auto chosen = dragging_ == widget ? std::clamp(draggingPoint_, 0, handleCount - 1) : nearest;
    const auto& handle = handles[static_cast<std::size_t>(chosen)];
    if (ImGui::IsItemActive()) {
        set(handle.cutoff, static_cast<float>(hzOf(io.MousePos.x)));
        if (handle.resonance >= 0 && io.MouseDelta.y != 0.0f)
            set(handle.resonance, value(handle.resonance) - io.MouseDelta.y / std::max(1.0f, (bottom - top) * 0.8f));
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    if (hovered && io.MouseWheel != 0.0f && handle.resonance >= 0) {
        set(handle.resonance, value(handle.resonance) + io.MouseWheel * 0.04f);
        ui::takeWheel();
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        set(handle.cutoff, synth::parameter(card_.model, handle.cutoff).defaultValue);
        if (handle.resonance >= 0)
            set(handle.resonance, synth::parameter(card_.model, handle.resonance).defaultValue);
    }
    for (int at = 0; at < handleCount; ++at) {
        const auto lit = (hovered || ImGui::IsItemActive()) && at == chosen;
        const auto& point = points[static_cast<std::size_t>(at)];
        list->AddCircleFilled(point, dp(lit ? 6.0f : 4.5f), lit ? colour : p.bg2);
        list->AddCircle(point, dp(lit ? 6.0f : 4.5f), colour, 0, dp(1.6f));
        if (handleCount == 2) {
            const char* label = at == 0 ? "1" : "2";
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.7f);
            list->AddText(ImVec2(point.x + dp(7.0f), point.y - dp(14.0f)), p.textDim, label);
            ImGui::PopFont();
        }
    }
    if (hovered && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", L("Glisser : fréquence (gauche, droite), résonance (haut, bas) · molette : résonance · double-clic : départ",
                                  "Drag: cutoff (left, right), resonance (up, down) · wheel: resonance · double click: default"));
}

void SynthView::drawEnvelope(ImDrawList* list, const char* prefix, const ImVec2 origin, const ImVec2 size, const bool interactive)
{
    const auto& p = theme::palette();
    const auto keyOf = [prefix](const char* stage) { return std::string(prefix != nullptr ? prefix : "") + "." + stage; };
    const auto attack = synth::parameterIndex(card_.model, keyOf("attack"));
    const auto decay = synth::parameterIndex(card_.model, keyOf("decay"));
    const auto sustain = synth::parameterIndex(card_.model, keyOf("sustain"));
    const auto release = synth::parameterIndex(card_.model, keyOf("release"));
    if (attack < 0 || decay < 0 || sustain < 0 || release < 0)
        return;
    const auto colour = colourOf(attack);
    const auto left = origin.x + dp(6.0f);
    const auto right = origin.x + size.x - dp(6.0f);
    const auto top = origin.y + dp(8.0f);
    const auto bottom = origin.y + size.y - dp(5.0f);
    const auto width = std::max(1.0f, right - left);
    const auto height = std::max(1.0f, bottom - top);
    // Each stage as wide as its time (on the knob's scale), the sustain fixed.
    const auto stretch = width * 0.27f;
    const auto lengthOf = [&](const int index) { return std::max(dp(2.0f), synth::normalise(card_.model, index, value(index)) * stretch); };
    const auto level = std::clamp(value(sustain), 0.0f, 1.0f);
    const auto x0 = left;
    const auto x1 = x0 + lengthOf(attack);
    const auto x2 = x1 + lengthOf(decay);
    const auto x3 = x2 + width * 0.16f;
    const auto x4 = x3 + lengthOf(release);
    const auto yOf = [&](const double amount) { return bottom - static_cast<float>(amount) * height; };
    std::vector<ImVec2> line;
    constexpr int pieces = 24;
    for (int at = 0; at <= pieces; ++at) {
        const auto u = static_cast<double>(at) / pieces;
        line.emplace_back(x0 + (x1 - x0) * static_cast<float>(u), yOf(1.0 - (1.0 - u) * (1.0 - u)));
    }
    for (int at = 1; at <= pieces; ++at) {
        const auto u = static_cast<double>(at) / pieces;
        line.emplace_back(x1 + (x2 - x1) * static_cast<float>(u), yOf(level + (1.0 - level) * std::exp(-4.6 * u)));
    }
    line.emplace_back(x3, yOf(level));
    for (int at = 1; at <= pieces; ++at) {
        const auto u = static_cast<double>(at) / pieces;
        line.emplace_back(x3 + (x4 - x3) * static_cast<float>(u), yOf(level * std::exp(-4.6 * u)));
    }
    for (std::size_t at = 1; at < line.size(); ++at)
        list->AddQuadFilled(line[at - 1], line[at], ImVec2(line[at].x, bottom), ImVec2(line[at - 1].x, bottom),
                            (colour & 0x00ffffffu) | 0x2a000000u);
    list->AddPolyline(line.data(), static_cast<int>(line.size()), colour, dp(1.7f));
    list->AddLine(ImVec2(x2, yOf(level)), ImVec2(x3, yOf(level)), colour, dp(1.7f));
    // Where the newest note's envelope is now: a line at its level.
    if (card_.live) {
        const auto name = std::string(prefix != nullptr ? prefix : "");
        const auto which = name == "amp" || name == "amp1"     ? 0
                           : name == "fenv" || name == "env2" || name == "fenv1" ? 1
                           : name == "env3" || name == "amp2"  ? 2
                           : name == "fenv2"                   ? 3
                                                               : -1;
        if (which >= 0) {
            const auto now = card_.heard.envelopes[static_cast<std::size_t>(which)];
            if (std::isfinite(now)) {
                const auto y = yOf(std::clamp(static_cast<double>(now), 0.0, 1.0));
                list->AddLine(ImVec2(left, y), ImVec2(right, y), theme::alpha(p.modulation, 0.55f), dp(1.0f));
                list->AddCircleFilled(ImVec2(right, y), dp(3.5f), p.modulation);
            }
        }
    }
    if (!interactive)
        return;
    // The points: the attack's top, the decay's end (with the sustain), the release's end.
    const ImVec2 points[] = { ImVec2(x1, yOf(1.0)), ImVec2(x2, yOf(level)), ImVec2(x4, yOf(0.0)) };
    const auto id = std::string("##envelope.") + (prefix != nullptr ? prefix : "");
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(id.c_str(), size);
    const auto widget = ImGui::GetItemID();
    const auto& io = ImGui::GetIO();
    auto nearest = -1;
    auto nearestDistance = dp(14.0f);
    for (int at = 0; at < 3; ++at) {
        const auto dx = io.MousePos.x - points[at].x;
        const auto dy = io.MousePos.y - points[at].y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = at;
        }
    }
    if (ImGui::IsItemActivated() && nearest >= 0) {
        dragging_ = widget;
        draggingPoint_ = nearest;
    }
    if (!ImGui::IsItemActive() && dragging_ == widget)
        dragging_ = 0;
    const auto mine = dragging_ == widget;
    if (mine) {
        const auto toTime = [&](const int index, const float from) {
            set(index, synth::denormalise(card_.model, index, std::clamp((io.MousePos.x - from) / stretch, 0.0f, 1.0f)));
        };
        switch (draggingPoint_) {
        case 0: toTime(attack, x0); break;
        case 1:
            toTime(decay, x1);
            set(sustain, std::clamp((bottom - io.MousePos.y) / height, 0.0f, 1.0f));
            break;
        default: toTime(release, x3); break;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    for (int at = 0; at < 3; ++at) {
        const auto lit = (mine && at == draggingPoint_) || (!mine && at == nearest && ImGui::IsItemHovered());
        list->AddCircleFilled(points[at], dp(lit ? 5.5f : 4.0f), lit ? colour : p.bg2);
        list->AddCircle(points[at], dp(lit ? 5.5f : 4.0f), colour, 0, dp(1.5f));
    }
    const auto shownPoint = mine ? draggingPoint_ : (ImGui::IsItemHovered() ? nearest : -1);
    if (shownPoint >= 0) {
        const auto index = shownPoint == 0 ? attack : shownPoint == 1 ? decay : release;
        auto text = synth::displayValue(card_.model, index, value(index), ui::french());
        if (shownPoint == 1)
            text += " \xc2\xb7 " + synth::displayValue(card_.model, sustain, value(sustain), ui::french());
        const auto& parameter = synth::parameter(card_.model, index);
        ImGui::SetTooltip("%s : %s", (ui::french() ? parameter.french : parameter.english).c_str(), text.c_str());
    }
}

void SynthView::drawLfo(ImDrawList* list, const char* prefix, const ImVec2 origin, const ImVec2 size)
{
    const auto& p = theme::palette();
    const auto base = std::string(prefix != nullptr ? prefix : "");
    const auto shapeIndex = synth::parameterIndex(card_.model, base + ".shape");
    const auto rateIndex = synth::parameterIndex(card_.model, base + ".rate");
    const auto syncIndex = synth::parameterIndex(card_.model, base + ".sync");
    if (shapeIndex < 0)
        return;
    const auto colour = colourOf(shapeIndex);
    const auto shape = static_cast<int>(std::lround(value(shapeIndex)));
    const auto left = origin.x + dp(5.0f);
    const auto width = std::max(2.0f, size.x - dp(10.0f));
    const auto middle = origin.y + size.y * 0.5f;
    const auto half = size.y * 0.38f;
    list->AddLine(ImVec2(left, middle), ImVec2(left + width, middle), p.grid);
    std::vector<ImVec2> line;
    const auto count = static_cast<int>(width);
    static constexpr double held[] = { 0.35, -0.7, 0.85, -0.15, 0.55, -0.9, 0.1, 0.7 };
    for (int at = 0; at <= count; ++at) {
        const auto phase = 2.0 * static_cast<double>(at) / std::max(1, count);
        // Dérive has two more: a rising saw, a wandering line.
        const auto derive = card_.model == synth::Model::derive;
        auto y = shape >= 4 ? held[static_cast<int>(phase * 4.0) % 8] : lfoWave(shape, phase);
        if (derive && shape == 5)
            y = -lfoWave(2, phase);
        if (derive && shape == 6) {
            // Wandering: the random steps joined smoothly.
            const auto step = phase * 4.0;
            const auto from = held[static_cast<int>(step) % 8];
            const auto to = held[(static_cast<int>(step) + 1) % 8];
            y = from + (to - from) * (0.5 - 0.5 * std::cos(kPi * (step - std::floor(step))));
        }
        line.emplace_back(left + static_cast<float>(at), middle - static_cast<float>(y) * half);
    }
    list->AddPolyline(line.data(), static_cast<int>(line.size()), colour, dp(1.6f));
    // Where the newest note's LFO is now, on its first cycle.
    if (card_.live) {
        const auto which = base == "lfo1" ? 0 : base == "lfo2" ? 1 : -1;
        if (which >= 0) {
            const auto now = card_.heard.lfos[static_cast<std::size_t>(which)];
            const auto phase = card_.heard.lfoPhases[static_cast<std::size_t>(which)];
            if (std::isfinite(now) && std::isfinite(phase)) {
                const ImVec2 point(left + width * 0.5f * std::clamp(phase, 0.0f, 1.0f), middle - std::clamp(now, -1.0f, 1.0f) * half);
                list->AddCircleFilled(point, dp(4.0f), p.modulation);
                list->AddCircle(point, dp(4.0f), p.bg0, 0, dp(1.0f));
            }
        }
    }
    // Its pace: the note value when synced, its rate otherwise.
    std::string pace;
    if (syncIndex >= 0 && value(syncIndex) >= 0.5f)
        pace = synth::displayValue(card_.model, syncIndex, value(syncIndex), ui::french());
    else if (rateIndex >= 0)
        pace = synth::displayValue(card_.model, rateIndex, value(rateIndex), ui::french());
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    list->AddText(ImVec2(origin.x + size.x - ImGui::CalcTextSize(pace.c_str()).x - dp(5.0f), origin.y + dp(2.0f)), p.textDim, pace.c_str());
    ImGui::PopFont();
}

void SynthView::drawSteps(ImDrawList* list, const char* prefix, const ImVec2 origin, const ImVec2 size, const bool interactive)
{
    const auto& p = theme::palette();
    const auto base = std::string(prefix != nullptr ? prefix : "");
    // A sequence's steps, or a function's points.
    auto first = indexOf((base + ".step1").c_str());
    auto countIndex = indexOf((base + ".steps").c_str());
    if (first < 0) {
        first = indexOf((base + ".point1").c_str());
        countIndex = indexOf((base + ".points").c_str());
    }
    if (first < 0)
        return;
    constexpr int slots = 16;
    const auto used = countIndex >= 0 ? std::clamp(static_cast<int>(std::lround(value(countIndex))), 1, slots) : slots;
    const auto& setting = synth::parameter(card_.model, first);
    const auto bipolar = setting.minimum < 0.0f;
    const auto colour = colourOf(first);
    const auto left = origin.x + dp(6.0f);
    const auto width = std::max(16.0f, size.x - dp(12.0f));
    const auto top = origin.y + dp(6.0f);
    const auto bottom = origin.y + size.y - dp(6.0f);
    const auto baseline = bipolar ? (top + bottom) * 0.5f : bottom;
    const auto reach = bipolar ? (bottom - top) * 0.5f : bottom - top;
    const auto slot = width / static_cast<float>(slots);
    const auto yOf = [&](const float amount) { return baseline - std::clamp(amount, setting.minimum, setting.maximum) * reach; };
    list->AddLine(ImVec2(left, baseline), ImVec2(left + width, baseline), p.gridStrong);
    for (int step = 0; step < slots; ++step) {
        const auto x0 = left + slot * static_cast<float>(step) + dp(1.5f);
        const auto x1 = left + slot * static_cast<float>(step + 1) - dp(1.5f);
        const auto y = yOf(value(first + step));
        const auto on = step < used;
        const auto fill = on ? (colour & 0x00ffffffu) | 0xb0000000u : (p.textFaint & 0x00ffffffu) | 0x40000000u;
        list->AddRectFilled(ImVec2(x0, std::min(y, baseline)), ImVec2(x1, std::max(y, baseline) + 1.0f), fill, dp(1.5f));
        if (step % 4 == 0)
            list->AddLine(ImVec2(x0 - dp(1.5f), top), ImVec2(x0 - dp(1.5f), bottom), p.grid);
    }
    // Eased from step to step: the line the modulation follows.
    if (const auto smoothIndex = indexOf((base + ".smooth").c_str()); smoothIndex >= 0) {
        const auto smooth = std::clamp(value(smoothIndex), 0.0f, 1.0f);
        const auto loopIndex = indexOf((base + ".loop").c_str());
        const auto loops = loopIndex < 0 || value(loopIndex) >= 0.5f;
        std::vector<ImVec2> line;
        const auto span = slot * static_cast<float>(used);
        const auto pieces = std::max(8, static_cast<int>(span / 2.0f));
        for (int at = 0; at <= pieces; ++at) {
            const auto along = static_cast<float>(at) / static_cast<float>(pieces) * static_cast<float>(used);
            const auto step = std::min(static_cast<int>(along), used - 1);
            const auto next = loops ? (step + 1) % used : std::min(step + 1, used - 1);
            const auto within = along - static_cast<float>(step);
            const auto from = value(first + step);
            const auto to = value(first + next);
            const auto ease = smooth > 1.0e-3f ? std::clamp((within - (1.0f - smooth)) / smooth, 0.0f, 1.0f) : 0.0f;
            line.emplace_back(left + span * static_cast<float>(at) / static_cast<float>(pieces), yOf(from + (to - from) * ease));
        }
        list->AddPolyline(line.data(), static_cast<int>(line.size()), p.text, dp(1.4f));
    }
    if (!interactive)
        return;
    // Drawn with the pointer: each bar it crosses takes its height.
    const auto id = "##steps." + base;
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(id.c_str(), size);
    const auto& io = ImGui::GetIO();
    const auto step = std::clamp(static_cast<int>((io.MousePos.x - left) / slot), 0, slots - 1);
    const auto amount = std::clamp((baseline - io.MousePos.y) / std::max(1.0f, reach), setting.minimum, setting.maximum);
    if (ImGui::IsItemActive()) {
        set(first + step, std::abs(amount) < 0.03f ? 0.0f : amount);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        set(first + step, 0.0f);
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        const auto shown = synth::displayValue(card_.model, first + step, value(first + step), ui::french());
        ImGui::SetTooltip("%s %d : %s\n%s", L("Pas", "Step"), step + 1, shown.c_str(),
                          L("Glisser pour dessiner les pas · double-clic : zéro", "Drag to draw the steps · double click: zero"));
    }
}

void SynthView::drawOrb(ImDrawList* list, const char* prefix, const ImVec2 origin, const ImVec2 size, const bool interactive)
{
    const auto& p = theme::palette();
    const auto base = std::string(prefix != nullptr ? prefix : "orb");
    const auto xIndex = indexOf((base + ".x").c_str());
    const auto yIndex = indexOf((base + ".y").c_str());
    if (xIndex < 0 || yIndex < 0)
        return;
    const auto colour = colourOf(xIndex);
    const ImVec2 centre(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    const auto radius = std::max(dp(10.0f), std::min(size.x, size.y) * 0.5f - dp(8.0f));
    list->AddCircleFilled(centre, radius, (colour & 0x00ffffffu) | 0x18000000u, 64);
    list->AddCircle(centre, radius, (colour & 0x00ffffffu) | 0x90000000u, 64, dp(1.5f));
    list->AddLine(ImVec2(centre.x - radius, centre.y), ImVec2(centre.x + radius, centre.y), p.grid);
    list->AddLine(ImVec2(centre.x, centre.y - radius), ImVec2(centre.x, centre.y + radius), p.grid);
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.75f);
    list->AddText(ImVec2(centre.x + radius - dp(10.0f), centre.y + dp(2.0f)), p.textDim, "X");
    list->AddText(ImVec2(centre.x + dp(4.0f), centre.y - radius + dp(1.0f)), p.textDim, "Y");
    ImGui::PopFont();
    // Its point, the circle it turns round, where it is now.
    const auto x = value(xIndex);
    const auto y = value(yIndex);
    const ImVec2 point(centre.x + x * radius, centre.y - y * radius);
    const auto motionIndex = indexOf((base + ".motion").c_str());
    const auto rateIndex = indexOf((base + ".rate").c_str());
    const auto motion = motionIndex >= 0 ? value(motionIndex) : 0.0f;
    if (motion > 0.001f) {
        list->AddCircle(point, motion * radius, (colour & 0x00ffffffu) | 0x60000000u, 48, dp(1.0f));
        const auto turn = 2.0 * kPi * static_cast<double>(rateIndex >= 0 ? value(rateIndex) : 0.0f) * ImGui::GetTime();
        const auto movedX = std::clamp(x + motion * static_cast<float>(std::cos(turn)), -1.0f, 1.0f);
        const auto movedY = std::clamp(y + motion * static_cast<float>(std::sin(turn)), -1.0f, 1.0f);
        const ImVec2 moved(centre.x + movedX * radius, centre.y - movedY * radius);
        list->AddLine(point, moved, (colour & 0x00ffffffu) | 0x80000000u, dp(1.0f));
        list->AddCircleFilled(moved, dp(4.0f), p.text);
    }
    if (!interactive) {
        list->AddCircleFilled(point, dp(4.0f), colour);
        return;
    }
    // Dragged anywhere in the circle: the point follows.
    const auto id = "##orb." + base;
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(id.c_str(), size);
    const auto& io = ImGui::GetIO();
    if (ImGui::IsItemActive()) {
        auto dx = (io.MousePos.x - centre.x) / radius;
        auto dy = (centre.y - io.MousePos.y) / radius;
        const auto length = std::sqrt(dx * dx + dy * dy);
        if (length > 1.0f) {
            dx /= length;
            dy /= length;
        }
        set(xIndex, dx);
        set(yIndex, dy);
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        set(xIndex, 0.0f);
        set(yIndex, 0.0f);
    }
    const auto lit = ImGui::IsItemActive() || ImGui::IsItemHovered();
    list->AddCircleFilled(point, dp(lit ? 7.0f : 6.0f), colour);
    list->AddCircle(point, dp(lit ? 7.0f : 6.0f), p.text, 0, dp(1.5f));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetTooltip("%s X %+.2f \xc2\xb7 Y %+.2f\n%s", L("Orbe", "Orb"), static_cast<double>(x), static_cast<double>(y),
                          L("Glisser le point · le mouvement le fait tourner seul · double-clic : au centre",
                            "Drag the point · motion turns it by itself · double click: to the centre"));
}

// ---- The card's pane, the window ------------------------------------------------------------------

void SynthView::drawPane(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId, const ImVec2 origin,
                         const ImVec2 size, const ImU32 colour)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    if (!bind(track, graph, nodeId))
        return;
    (void) colour;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    ImGui::PushID(static_cast<int>(nodeId) + 41'000);
    const auto sections = cardSections(size.y);
    compact_ = true;
    const auto padding = dp(5.0f);
    auto x = origin.x;
    for (const auto& section : sections) {
        const auto& item = *section.panel;
        const ImVec2 min(x, origin.y);
        const ImVec2 max(x + section.width, origin.y + size.y);
        const auto tint = sectionColour(item.section);
        list->AddRectFilled(min, max, p.bg1, dp(4.0f));
        list->AddRect(min, max, theme::alpha(tint, 0.35f), dp(4.0f));
        ImGui::PushID(item.id);
        const auto inner = section.width - padding * 2.0f;
        auto y = min.y + cardTitleHeight();
        if (section.pictured) {
            const auto height = compactPictureHeight(item.picture);
            drawPicture(item.picture, item.pictureKey, item.secondKey, ImVec2(min.x + padding, y), ImVec2(inner, height), true);
            y += height + dp(4.0f);
        }
        (void) flow(item.controls, ImVec2(min.x + padding, y), inner, cardKnob, true, max.y - padding - y);
        const auto more = clipped_;
        // Its name in its colour; a double click opens the window (where
        // the rest is, when it did not all fit).
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.78f);
        const auto title = juce::String::fromUTF8(L(item.french, item.english)) + (more ? juce::String::fromUTF8(" \xe2\x80\xa6") : juce::String());
        ui::clippedText(list, ImVec2(min.x + padding, min.y + dp(3.0f)), inner, tint, title.toRawUTF8());
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(ImVec2(min.x, min.y));
        ImGui::InvisibleButton("##title", ImVec2(section.width, cardTitleHeight() - dp(2.0f)));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            openEditor(track, nodeId);
        ui::tooltip(more ? L("D'autres réglages dans la grande fenêtre : double-clic.", "More settings in the large window: double click.")
                         : L("Double-clic : la grande fenêtre.", "Double click: the large window."));
        ImGui::PopID();
        x += section.width + dp(4.0f);
    }
    compact_ = false;
    ImGui::PopID();
}

void SynthView::openEditor(const TrackRef& track, const std::uint64_t nodeId, const bool detached)
{
    const auto id = windows_.open(track, nodeId, state_.manyInstrumentWindows);
    if (detached)
        ui::setDetached(id.c_str(), true);
}

void SynthView::drawEditor()
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

bool SynthView::drawEditorWindow(const bool focus)
{
    const auto target = *editor_;
    auto* slot = studio_.graphFor(target.track);
    if (slot == nullptr || !studio_.isValid(target.track) || !bind(target.track, slot->graph(), target.nodeId)) {
        return false;
    }
    const auto& p = theme::palette();
    const auto& layout = synth_layout::layoutFor(card_.model);
    const auto& info = synth::info(card_.model);
    ui::beforeDetachable(windowId_.c_str());
    windows_.placeFirst(target.track, target.nodeId);
    ImGui::SetNextWindowSize(ImVec2(dp(1'180.0f), dp(780.0f)), ImGuiCond_FirstUseEver);
    if (focus)
        ImGui::SetNextWindowFocus();
    auto open = true;
    const auto title = juce::String::fromUTF8((ui::french() ? info.french : info.english).c_str()) + juce::String::fromUTF8(" \xe2\x80\x94 ")
                       + studio_.trackName(target.track) + juce::String(windowId_);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, theme::mix(layout.colour, p.bg2 | 0xff000000u, 0.65f));
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
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextColored(theme::vec(layout.colour), "%s", layout.title != nullptr ? layout.title : info.english.c_str());
        ImGui::PopFont();
        ImGui::SameLine(0.0f, dp(16.0f));
        if (const auto holder = studio_.devicePresetHolder(target.track, target.nodeId)) {
            const auto track = target.track;
            const auto nodeId = target.nodeId;
            presets_.bar("synthPresets", *holder, [this, track, nodeId] { return studio_.devicePresetState(track, nodeId); },
                         [this, track, nodeId](const juce::var& preset, juce::String& error) {
                             return studio_.applyDevicePreset(track, nodeId, preset, error);
                         },
                         dp(330.0f));
        }
        ImGui::SameLine(0.0f, dp(16.0f));
        if (ImGui::Button(L("Initialiser", "Initialise"))) {
            const auto defaults = synth::defaultValues(card_.model);
            for (int index = 0; index < synth::parameterCount(card_.model); ++index)
                set(index, defaults[static_cast<std::size_t>(index)]);
        }
        ui::tooltip(L("Tous les réglages à leur valeur de départ (Ctrl+Z l'annule)", "Every setting back to its start (Ctrl+Z undoes it)"));
        ImGui::SameLine(0.0f, dp(16.0f));
        {
            // What it is, cut short before the window's own buttons.
            const auto hint = ui::french() ? info.frenchHint : info.englishHint;
            const auto at = ImGui::GetCursorScreenPos();
            const auto room = ImGui::GetContentRegionAvail().x - dp(34.0f);
            ui::clippedText(ImGui::GetWindowDrawList(), ImVec2(at.x, at.y + ImGui::GetStyle().FramePadding.y), room,
                            theme::palette().textDim, hint.c_str());
            ImGui::Dummy(ImVec2(std::max(0.0f, room), ImGui::GetFrameHeight()));
        }
        if (ImGui::BeginTabBar("##synthTabs")) {
            for (const auto& page : layout.pages)
                if (ImGui::BeginTabItem(L(page.french, page.english))) {
                    drawSynthTab(page);
                    ImGui::EndTabItem();
                }
            if (ImGui::BeginTabItem(L("EFFETS", "FX"))) {
                static constexpr instrument_effects::Choice effects[] = {
                    { "chorus", "Chorus", "Chorus" },           { "phaser", "Phaser", "Phaser" },
                    { "flanger", "Flanger", "Flanger" },        { "fournaise", "Fournaise", "Fournaise" },
                    { "patine", "Patine", "Patine" },           { "echo-bande", "Écho Bande", "Écho Bande" },
                    { "ping-pong", "Délai", "Delay" },          { "halo", "Halo", "Halo" },
                    { "comp-pro", "Compresseur", "Compressor" }, { "eq-pro", "Égaliseur", "EQ" },
                    { "limiter", "Limiteur", "Limiter" },
                };
                ImGui::BeginChild("##synthEffects", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                instrument_effects::draw(studio_, state_, target.track, juce::String(info.typeKey),
                                         juce::String::fromUTF8((ui::french() ? info.french : info.english).c_str()), effects, layout.colour);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    return open;
}

void SynthView::drawSynthTab(const synth_layout::Page& page)
{
    ImGui::BeginChild("##synthBody", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
    const auto available = ImGui::GetContentRegionAvail();
    const auto gap = ImGui::GetStyle().ItemSpacing;
    // Each row as tall as its tallest panel; what is left goes to the rows
    // with pictures, which grow.
    std::vector<float> heights;
    std::vector<bool> pictured;
    auto total = 0.0f;
    for (const auto& row : page.rows) {
        auto weights = 0.0f;
        for (const auto& item : row)
            weights += item.weight;
        auto tallest = 0.0f;
        auto withPicture = false;
        for (const auto& item : row) {
            const auto width = (available.x - gap.x * static_cast<float>(row.size() - 1)) * item.weight / std::max(0.01f, weights);
            tallest = std::max(tallest, panelHeight(item, width));
            withPicture = withPicture || item.picture != Picture::none;
        }
        heights.push_back(tallest);
        pictured.push_back(withPicture);
        total += tallest + gap.y;
    }
    const auto growing = static_cast<int>(std::count(pictured.begin(), pictured.end(), true));
    const auto extra = growing > 0 ? std::clamp((available.y - total) / static_cast<float>(growing), 0.0f, dp(140.0f)) : 0.0f;
    for (std::size_t at = 0; at < page.rows.size(); ++at) {
        const auto& row = page.rows[at];
        auto weights = 0.0f;
        for (const auto& item : row)
            weights += item.weight;
        const auto height = heights[at] + (pictured[at] ? extra : 0.0f);
        auto used = 0.0f;
        for (std::size_t column = 0; column < row.size(); ++column) {
            const auto& item = row[column];
            // The last panel takes what the others leave (no gap of rounding).
            const auto width = column + 1 == row.size()
                                   ? available.x - used
                                   : (available.x - gap.x * static_cast<float>(row.size() - 1)) * item.weight / std::max(0.01f, weights);
            if (column > 0)
                ImGui::SameLine();
            panel(item, width, height);
            used += width + gap.x;
        }
    }
    ImGui::EndChild();
}

} // namespace cydaw::v2
