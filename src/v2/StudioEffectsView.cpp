#include "StudioEffectsView.h"

#include "Theme.h"
#include "Widgets.h"

#include <cydaw/engine/AmpDesign.h>
#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/Fft.h>
#include <cydaw/engine/HaloDesign.h>
#include <cydaw/engine/MixDesign.h>
#include <cydaw/engine/ProEffectsDesign.h>
#include <cydaw/engine/SuiteDesign.h>
#include <cydaw/engine/VoiceDesign.h>
#include <cydaw/engine/PitchDesign.h>
#include <cydaw/engine/SpaceDesign.h>
#include <cydaw/engine/RhythmDesign.h>
#include <cydaw/engine/MotionDesign.h>
#include <cydaw/engine/ResonanceDesign.h>
#include <cydaw/engine/UtilityDesign.h>
#include <cydaw/engine/MidiEffects.h>
#include <cydaw/engine/MidiEffectsDesign.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <map>
#include <numbers>
#include <string>

namespace cydaw::v2::visual {

using ui::dp;
using ui::L;
namespace pro = engine::pro;

namespace {

constexpr double kLowHz = 20.0;
constexpr double kHighHz = 20'000.0;
constexpr ImU32 kDark = IM_COL32(18, 20, 24, 255);
constexpr ImU32 kReduction = IM_COL32(255, 92, 92, 255);
// The four filters' colours.
constexpr std::array<ImU32, pro::filterUnits> kUnitColours { IM_COL32(71, 215, 168, 255), IM_COL32(255, 170, 70, 255),
                                                             IM_COL32(120, 170, 255, 255), IM_COL32(236, 110, 200, 255) };

struct Box {
    ImVec2 min;
    ImVec2 max;
    [[nodiscard]] float width() const { return max.x - min.x; }
    [[nodiscard]] float height() const { return max.y - min.y; }
    [[nodiscard]] bool contains(const ImVec2 point) const
    {
        return point.x >= min.x && point.x < max.x && point.y >= min.y && point.y < max.y;
    }
    [[nodiscard]] Box inset(const float amount) const
    {
        return { ImVec2(min.x + amount, min.y + amount), ImVec2(max.x - amount, max.y - amount) };
    }
};

// The file of the card being drawn (setCardFile).
const CardFile* cardFile = nullptr;

float xOfHz(const Box& box, const double hz)
{
    const auto t = std::log(std::clamp(hz, kLowHz, kHighHz) / kLowHz) / std::log(kHighHz / kLowHz);
    return box.min.x + static_cast<float>(t) * box.width();
}

double hzOfX(const Box& box, const float x)
{
    const auto t = std::clamp((x - box.min.x) / std::max(1.0f, box.width()), 0.0f, 1.0f);
    return kLowHz * std::pow(kHighHz / kLowHz, static_cast<double>(t));
}

void text(ImDrawList* list, const ImVec2 position, const ImU32 colour, const char* value, const float scale = 0.72f)
{
    list->AddText(ImGui::GetFont(), ImGui::GetFontSize() * scale, position, colour, value);
}

ImVec2 textSize(const char* value, const float scale = 0.72f)
{
    return ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize() * scale, FLT_MAX, 0.0f, value);
}

void centred(ImDrawList* list, const ImVec2 centre, const ImU32 colour, const char* value, const float scale = 0.72f)
{
    const auto size = textSize(value, scale);
    text(list, ImVec2(centre.x - size.x * 0.5f, centre.y - size.y * 0.5f), colour, value, scale);
}

void title(ImDrawList* list, const Box& box, const char* value)
{
    const auto& p = theme::palette();
    text(list, ImVec2(box.min.x + dp(6.0f), box.min.y + dp(3.0f)), theme::alpha(p.textDim, 0.9f), value, 0.68f);
}

void section(ImDrawList* list, const ImVec2 position, const char* value)
{
    const auto& p = theme::palette();
    text(list, position, theme::alpha(p.textFaint, 0.95f), value, 0.62f);
}

Param* at(std::vector<Param>& parameters, const Pane& pane, const int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= pane.parameters.size())
        return nullptr;
    const auto slot = pane.parameters[static_cast<std::size_t>(index)];
    return slot >= 0 && static_cast<std::size_t>(slot) < parameters.size() ? &parameters[static_cast<std::size_t>(slot)] : nullptr;
}

double get(std::vector<Param>& parameters, const Pane& pane, const int index, const double fallback)
{
    const auto* parameter = at(parameters, pane, index);
    return parameter != nullptr ? parameter->value : fallback;
}

bool apply(Param* parameter, double value, const Setter& set)
{
    if (parameter == nullptr)
        return false;
    value = std::clamp(value, parameter->minimum, parameter->maximum);
    if (studioStepped(*parameter))
        value = std::round(value);
    if (std::abs(value - parameter->value) < 1.0e-9)
        return false;
    parameter->value = value;
    set(*parameter, value);
    return true;
}

// Where a cable or the track's modulation puts the parameter now.
double liveValue(const Param& parameter, const Live& live)
{
    if (live.modulated != nullptr)
        if (const auto found = live.modulated->find(parameter.binding); found != live.modulated->end())
            return found->second;
    return parameter.value;
}

bool isLogarithmic(const Param& parameter)
{
    return parameter.minimum > 0.0
           && (parameter.unit == "Hz" || parameter.unit == "ms" || parameter.unit == "s" || parameter.unit == "cm");
}

float knobHeight(const float diameter)
{
    return dp(diameter) + ImGui::GetFontSize() * 1.75f;
}

// How many knobs a row of that width holds, none narrower than `narrowest`:
// the rest go on the next rows rather than shrink.
std::size_t knobColumns(const float width, const std::size_t count, const float narrowest)
{
    const auto fits = static_cast<std::size_t>(std::max(1.0f, width / std::max(1.0f, narrowest)));
    return std::clamp<std::size_t>(fits, 1, std::max<std::size_t>(1, count));
}

// A knob centred in a cell `width` wide: its label under it, its value under
// that; a right click for the parameter's menu (automate it).
bool knobCell(const ImVec2 origin, const float width, Param* parameter, const char* label, const ImU32 colour, const Live& live,
              const Setter& set, const float diameter = 34.0f)
{
    if (parameter == nullptr)
        return false;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto knobWidth = dp(diameter + 12.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (width - knobWidth) * 0.5f, origin.y));
    auto value = static_cast<float>(parameter->value);
    const auto unit = parameter->unit.toStdString();
    ui::KnobStyle style;
    style.diameter = diameter;
    style.label = label;
    style.labelWidth = width;
    style.colour = colour;
    style.format = parameter->maximum - parameter->minimum >= 100.0 ? "%.0f" : parameter->maximum - parameter->minimum >= 10.0 ? "%.1f" : "%.2f";
    style.unit = unit.c_str();
    if (isLogarithmic(*parameter))
        style.flags |= ui::knobLogarithmic;
    if (parameter->minimum < 0.0 && std::abs(parameter->minimum + parameter->maximum) < 1.0e-6)
        style.flags |= ui::knobBipolar;
    if (studioStepped(*parameter))
        style.flags |= ui::knobInteger;
    if (live.modulated != nullptr)
        if (const auto found = live.modulated->find(parameter->binding); found != live.modulated->end())
            style.modulation = static_cast<float>(positionOf(*parameter, found->second));
    ImGui::PushID(parameter->binding.toRawUTF8());
    auto changed = false;
    {
        const ui::LearnParameterScope learn(parameter->binding, parameter->label);
        if (ui::knob("##knob", &value, static_cast<float>(parameter->minimum), static_cast<float>(parameter->maximum),
                     static_cast<float>(parameter->defaultValue), style))
            changed = apply(parameter, value, set);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openParameterMenu(*parameter);
    ImGui::PopID();
    const auto shown = formatValue(*parameter, parameter->value);
    const auto size = textSize(shown.toRawUTF8(), 0.7f);
    const auto valueTop = origin.y + dp(diameter) + ImGui::GetFontSize() * 0.88f;
    text(list, ImVec2(origin.x + (width - size.x) * 0.5f, valueTop), p.text, shown.toRawUTF8(), 0.7f);
    // A click on the value types it in.
    ImGui::PushID(parameter->binding.toRawUTF8());
    changed |= typedValue("##typed", *parameter, ImVec2(origin.x + dp(2.0f), valueTop), ImVec2(width - dp(4.0f), size.y), set);
    ImGui::PopID();
    return changed;
}

// A row of buttons for a parameter of whole steps (a style, a mode).
bool segmented(const char* id, Param* parameter, const std::vector<const char*>& names, const ImVec2 origin, const float width,
               const float height, const ImU32 colour, const Setter& set, const char* tooltip = nullptr)
{
    if (parameter == nullptr || names.empty())
        return false;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto current = static_cast<int>(std::lround(parameter->value - parameter->minimum));
    const auto cell = width / static_cast<float>(names.size());
    auto changed = false;
    ImGui::PushID(id);
    for (std::size_t index = 0; index < names.size(); ++index) {
        const ImVec2 min(origin.x + cell * static_cast<float>(index), origin.y);
        const ImVec2 max(min.x + cell - dp(2.0f), origin.y + height);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(static_cast<int>(index));
        if (ImGui::InvisibleButton("##segment", ImVec2(std::max(1.0f, max.x - min.x), height)))
            changed |= apply(parameter, parameter->minimum + static_cast<double>(index), set);
        const auto hovered = ImGui::IsItemHovered();
        if (hovered && tooltip != nullptr)
            ImGui::SetTooltip("%s", tooltip);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            openParameterMenu(*parameter);
        ImGui::PopID();
        const auto on = static_cast<int>(index) == current;
        list->AddRectFilled(min, max, on ? colour : hovered ? p.bg4 : p.bg3, dp(3.0f));
        list->PushClipRect(min, max, true);
        centred(list, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), on ? kDark : p.textDim, names[index], 0.66f);
        list->PopClipRect();
    }
    ImGui::PopID();
    return changed;
}

// An on/off switch for a parameter of 0 and 1.
bool switchCell(const char* label, Param* parameter, const ImVec2 origin, const ImVec2 size, const ImU32 colour, const Setter& set,
                const char* tooltip = nullptr)
{
    if (parameter == nullptr)
        return false;
    ImGui::SetCursorScreenPos(origin);
    auto on = parameter->value >= 0.5;
    ImGui::PushID(parameter->binding.toRawUTF8());
    auto changed = false;
    if (ui::toggle(label, &on, colour, size, tooltip))
        changed = apply(parameter, on ? 1.0 : 0.0, set);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
        openParameterMenu(*parameter);
    ImGui::PopID();
    return changed;
}

// A drop-down list for a parameter of whole steps.
bool comboCell(const char* id, Param* parameter, const std::vector<juce::String>& names, const ImVec2 origin, const float width,
               const Setter& set)
{
    if (parameter == nullptr || names.empty())
        return false;
    ImGui::SetCursorScreenPos(origin);
    ImGui::SetNextItemWidth(width);
    const auto current = std::clamp(static_cast<int>(std::lround(parameter->value - parameter->minimum)), 0,
                                    static_cast<int>(names.size()) - 1);
    auto changed = false;
    ImGui::PushID(id);
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
    if (ImGui::BeginCombo("##combo", names[static_cast<std::size_t>(current)].toRawUTF8(), ImGuiComboFlags_HeightLarge)) {
        for (std::size_t index = 0; index < names.size(); ++index)
            if (ImGui::Selectable(names[index].toRawUTF8(), static_cast<int>(index) == current))
                changed |= apply(parameter, parameter->minimum + static_cast<double>(index), set);
        ImGui::EndCombo();
    }
    ImGui::PopFont();
    ImGui::PopID();
    return changed;
}

// The area between a curve and a level line, in strips drawn without
// anti-aliasing (its fringes would show as seams between them).
void fillUnder(ImDrawList* list, const std::vector<ImVec2>& points, const float baseline, const ImU32 colour)
{
    const auto flags = list->Flags;
    list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    for (std::size_t index = 0; index + 1 < points.size(); ++index) {
        const auto& a = points[index];
        const auto& b = points[index + 1];
        list->AddQuadFilled(ImVec2(a.x, std::min(a.y, baseline)), ImVec2(b.x, std::min(b.y, baseline)),
                            ImVec2(b.x, std::max(b.y, baseline)), ImVec2(a.x, std::max(a.y, baseline)), colour);
    }
    list->Flags = flags;
}

void frequencyGrid(ImDrawList* list, const Box& box, const bool labels)
{
    const auto& p = theme::palette();
    for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1'000.0, 2'000.0, 5'000.0, 10'000.0 }) {
        const auto x = xOfHz(box, hz);
        list->AddLine(ImVec2(x, box.min.y), ImVec2(x, box.max.y), hz == 100.0 || hz == 1'000.0 || hz == 10'000.0 ? p.gridStrong : p.grid);
        if (labels) {
            char label[16];
            std::snprintf(label, sizeof(label), hz >= 1'000.0 ? "%.0fk" : "%.0f", hz >= 1'000.0 ? hz / 1'000.0 : hz);
            text(list, ImVec2(x + dp(2.0f), box.max.y - ImGui::GetFontSize() * 0.75f), p.textFaint, label, 0.6f);
        }
    }
}

void spectrumBehind(ImDrawList* list, const Box& box, const Spectrum* spectrum, const ImU32 colour)
{
    if (spectrum == nullptr || !spectrum->visible())
        return;
    std::vector<ImVec2> points;
    points.reserve(Spectrum::binCount);
    const auto& levels = spectrum->levels();
    for (auto bin = 0; bin < Spectrum::binCount; ++bin)
        points.emplace_back(xOfHz(box, Spectrum::frequencyOfBin(static_cast<float>(bin))),
                            box.max.y - levels[static_cast<std::size_t>(bin)] * box.height());
    fillUnder(list, points, box.max.y, theme::alpha(colour, 0.1f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), theme::alpha(colour, 0.35f), dp(1.0f));
}

void dashedLine(ImDrawList* list, const ImVec2 a, const ImVec2 b, const ImU32 colour, const float thickness = 1.0f)
{
    const auto length = std::hypot(b.x - a.x, b.y - a.y);
    const auto dash = dp(5.0f);
    for (auto t = 0.0f; t < length; t += dash * 2.0f) {
        const auto t0 = t / length;
        const auto t1 = std::min(1.0f, (t + dash) / length);
        list->AddLine(ImVec2(a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0), ImVec2(a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1), colour,
                      thickness);
    }
}

void handleDot(ImDrawList* list, const ImVec2 centre, const ImU32 colour, const bool hot, const char* label = nullptr)
{
    const auto radius = dp(hot ? 7.5f : 6.0f);
    list->AddCircleFilled(centre, radius + dp(1.5f), theme::alpha(kDark, 0.8f));
    list->AddCircleFilled(centre, radius, colour);
    if (label != nullptr)
        centred(list, centre, kDark, label, 0.56f);
}

// A frame for a display: its background and border; the inside returned.
Box frame(ImDrawList* list, const Box& box)
{
    const auto& p = theme::palette();
    list->AddRectFilled(box.min, box.max, theme::mix(p.bg0, p.bg1, 0.35f), dp(4.0f));
    list->AddRect(box.min, box.max, theme::alpha(p.line, 0.8f), dp(4.0f));
    return box.inset(dp(1.0f));
}

// The display's state between frames, by its parameters (two devices,
// two states).
struct DragState {
    int active { -1 };
    int selected {};
};
DragState& dragState(std::vector<Param>& parameters, const Pane& pane)
{
    static std::map<juce::String, DragState> states;
    const auto* first = at(parameters, pane, 0);
    return states[first != nullptr ? first->binding : juce::String("?")];
}

juce::String percent(const double value)
{
    return juce::String(juce::roundToInt(value * 100.0)) + " %";
}

// ---- Réverbe Pro ------------------------------------------------------------------------------

struct ReverbView {
    double decay {};
    double lowMultiplier {};
    double lowFrequency {};
    double midMultiplier {};
    double midFrequency {};
    double highMultiplier {};
    double highFrequency {};
};

ReverbView reverbOf(std::vector<Param>& parameters, const Pane& pane)
{
    return { get(parameters, pane, pro::reverbDecay, 2.2),         get(parameters, pane, pro::reverbLowDecay, 1.3),
             get(parameters, pane, pro::reverbLowFrequency, 220.0), get(parameters, pane, pro::reverbMidDecay, 1.0),
             get(parameters, pane, pro::reverbMidFrequency, 1'200.0), get(parameters, pane, pro::reverbHighDecay, 0.55),
             get(parameters, pane, pro::reverbHighFrequency, 4'500.0) };
}

// The decay over frequency: what the network's loop filters make of each
// band; three nodes to drag (the band's frequency sideways, its decay up and
// down), the cuts at the sides, the main decay by dragging elsewhere.
bool reverbDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                   const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    auto view = reverbOf(parameters, pane);
    constexpr auto shortest = 0.05;
    constexpr auto longest = 60.0;
    const auto top = box.min.y + box.height() * 0.08f;
    const auto bottom = box.max.y - box.height() * 0.06f;
    const auto yOfSeconds = [&](const double seconds) {
        const auto t = std::log(std::clamp(seconds, shortest, longest) / shortest) / std::log(longest / shortest);
        return bottom - static_cast<float>(t) * (bottom - top);
    };
    const auto secondsOfY = [&](const float y) {
        const auto t = std::clamp((bottom - y) / std::max(1.0f, bottom - top), 0.0f, 1.0f);
        return shortest * std::pow(longest / shortest, static_cast<double>(t));
    };
    frequencyGrid(list, box, large);
    for (const auto seconds : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0 }) {
        const auto y = yOfSeconds(seconds);
        list->AddLine(ImVec2(box.min.x, y), ImVec2(box.max.x, y), p.grid);
        if (large) {
            char label[16];
            std::snprintf(label, sizeof(label), seconds < 1.0 ? "%.2g s" : "%.0f s", seconds);
            text(list, ImVec2(box.min.x + dp(4.0f), y - ImGui::GetFontSize() * 0.7f), p.textFaint, label, 0.58f);
        }
    }
    spectrumBehind(list, box, live.after, colour);

    // The cuts: shaded beyond them.
    auto* lowCut = at(parameters, pane, pro::reverbLowCut);
    auto* highCut = at(parameters, pane, pro::reverbHighCut);
    const auto lowCutX = lowCut != nullptr ? xOfHz(box, lowCut->value) : box.min.x;
    const auto highCutX = highCut != nullptr ? xOfHz(box, highCut->value) : box.max.x;
    if (lowCut != nullptr && lowCut->value > 10.5)
        list->AddRectFilled(box.min, ImVec2(lowCutX, box.max.y), theme::alpha(kDark, 0.45f));
    if (highCut != nullptr && highCut->value < 19'900.0)
        list->AddRectFilled(ImVec2(highCutX, box.min.y), box.max, theme::alpha(kDark, 0.45f));

    // The curve.
    std::vector<ImVec2> points;
    const auto steps = std::max(48, static_cast<int>(box.width() / dp(3.0f)));
    points.reserve(static_cast<std::size_t>(steps + 1));
    for (auto step = 0; step <= steps; ++step) {
        const auto x = box.min.x + box.width() * static_cast<float>(step) / static_cast<float>(steps);
        const auto seconds = pro::reverbDecayAt(hzOfX(box, x), view.decay, view.lowMultiplier, view.lowFrequency, view.midMultiplier,
                                                view.midFrequency, view.highMultiplier, view.highFrequency);
        points.emplace_back(x, yOfSeconds(seconds));
    }
    fillUnder(list, points, bottom, theme::alpha(colour, 0.16f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(2.0f));
    dashedLine(list, ImVec2(box.min.x, yOfSeconds(view.decay)), ImVec2(box.max.x, yOfSeconds(view.decay)), theme::alpha(p.text, 0.25f));

    struct Node {
        int multiplier;
        int frequency;
        const char* label;
    };
    const std::array<Node, 3> nodes { { { pro::reverbLowDecay, pro::reverbLowFrequency, "G" },
                                        { pro::reverbMidDecay, pro::reverbMidFrequency, "M" },
                                        { pro::reverbHighDecay, pro::reverbHighFrequency, "A" } } };
    const auto nodeAt = [&](const std::size_t index) {
        const auto* multiplier = at(parameters, pane, nodes[index].multiplier);
        const auto* frequency = at(parameters, pane, nodes[index].frequency);
        if (multiplier == nullptr || frequency == nullptr)
            return ImVec2(-1.0e6f, -1.0e6f);
        return ImVec2(xOfHz(box, frequency->value), yOfSeconds(view.decay * multiplier->value));
    };

    auto& state = dragState(parameters, pane);
    ImGui::SetCursorScreenPos(box.min);
    ImGui::InvisibleButton("##reverbDisplay", ImVec2(std::max(1.0f, box.width()), std::max(1.0f, box.height())));
    const auto hovered = ImGui::IsItemHovered();
    // What the pointer is over: a node (0-2), a cut's edge (4, 5), or the
    // curve as a whole (3).
    auto hot = -1;
    if (hovered || ImGui::IsItemActive()) {
        auto best = dp(11.0f);
        for (std::size_t index = 0; index < nodes.size(); ++index) {
            const auto centre = nodeAt(index);
            const auto distance = std::hypot(centre.x - io.MousePos.x, centre.y - io.MousePos.y);
            if (distance < best) {
                best = distance;
                hot = static_cast<int>(index);
            }
        }
        if (hot < 0 && lowCut != nullptr && std::abs(io.MousePos.x - lowCutX) < dp(5.0f) && lowCut->value > 10.5)
            hot = 4;
        if (hot < 0 && highCut != nullptr && std::abs(io.MousePos.x - highCutX) < dp(5.0f) && highCut->value < 19'900.0)
            hot = 5;
        if (hot < 0)
            hot = 3;
    }
    if (ImGui::IsItemActivated())
        state.active = hot;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
        if (state.active >= 0 && state.active <= 2) {
            const auto& node = nodes[static_cast<std::size_t>(state.active)];
            changed |= apply(at(parameters, pane, node.frequency), hzOfX(box, io.MousePos.x), set);
            changed |= apply(at(parameters, pane, node.multiplier), secondsOfY(io.MousePos.y) / std::max(0.05, view.decay), set);
        } else if (state.active == 3) {
            changed |= apply(at(parameters, pane, pro::reverbDecay), view.decay * std::exp(-io.MouseDelta.y * 0.01), set);
        } else if (state.active == 4) {
            changed |= apply(lowCut, hzOfX(box, io.MousePos.x), set);
        } else if (state.active == 5) {
            changed |= apply(highCut, hzOfX(box, io.MousePos.x), set);
        }
    }
    if (ImGui::IsItemDeactivated())
        state.active = -1;
    if (hovered && io.MouseWheel != 0.0f) {
        if (hot >= 0 && hot <= 2) {
            auto* multiplier = at(parameters, pane, nodes[static_cast<std::size_t>(hot)].multiplier);
            if (multiplier != nullptr)
                changed |= apply(multiplier, multiplier->value * std::exp(io.MouseWheel * 0.08), set);
        } else {
            changed |= apply(at(parameters, pane, pro::reverbDecay), view.decay * std::exp(io.MouseWheel * 0.08), set);
        }
        ui::takeWheel();
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hot >= 0 && hot <= 2)
        changed |= apply(at(parameters, pane, nodes[static_cast<std::size_t>(hot)].multiplier), 1.0, set);
    if (hovered && hot == 4)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (hovered && hot == 5)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    view = reverbOf(parameters, pane);
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto centre = nodeAt(index);
        const auto isHot = state.active == static_cast<int>(index) || (state.active < 0 && hot == static_cast<int>(index));
        handleDot(list, centre, isHot ? theme::brighter(colour, 0.25f) : colour, isHot, nodes[index].label);
        if (isHot) {
            const auto* frequency = at(parameters, pane, nodes[index].frequency);
            const auto* multiplier = at(parameters, pane, nodes[index].multiplier);
            if (frequency != nullptr && multiplier != nullptr) {
                const auto label = formatValue(*frequency, frequency->value) + "  " + juce::String(view.decay * multiplier->value, 2)
                                   + " s";
                text(list, ImVec2(centre.x + dp(10.0f), centre.y - dp(18.0f)), p.text, label.toRawUTF8(), 0.68f);
            }
        }
    }
    char summary[64];
    std::snprintf(summary, sizeof(summary), "%.2f s", view.decay);
    const auto size = textSize(summary, 0.9f);
    text(list, ImVec2(box.max.x - size.x - dp(8.0f), box.min.y + dp(4.0f)), p.text, summary, 0.9f);
    if (get(parameters, pane, pro::reverbFreeze, 0.0) >= 0.5) {
        const auto label = L("GEL", "FREEZE");
        const auto badge = textSize(label, 0.7f);
        const ImVec2 min(box.max.x - badge.x - dp(16.0f), box.min.y + dp(6.0f) + size.y);
        list->AddRectFilled(min, ImVec2(min.x + badge.x + dp(8.0f), min.y + badge.y + dp(4.0f)), IM_COL32(120, 200, 255, 255), dp(3.0f));
        text(list, ImVec2(min.x + dp(4.0f), min.y + dp(2.0f)), kDark, label, 0.7f);
    }
    return changed;
}

// The impulse as the settings shape it: the gap of the predelay, the early
// reflections as ticks, the late tail falling over the decay.
void reverbImpulse(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const double tempo)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(box.min, box.max, theme::alpha(kDark, 0.85f), dp(4.0f));
    list->AddRect(box.min, box.max, theme::alpha(p.line, 0.9f), dp(4.0f));
    const auto inner = box.inset(dp(6.0f));
    const auto decay = get(parameters, pane, pro::reverbDecay, 2.2);
    auto predelay = get(parameters, pane, pro::reverbPredelay, 12.0) * 0.001;
    if (const auto* division = engine::noteDivisionFor(static_cast<float>(get(parameters, pane, pro::reverbSync, 0.0))))
        predelay = division->beats * 60.0 / std::max(20.0, tempo);
    const auto distance = get(parameters, pane, pro::reverbDistance, 0.35);
    const auto space = get(parameters, pane, pro::reverbSpace, 0.5);
    const auto early = std::pow(10.0, get(parameters, pane, pro::reverbEarly, 0.0) / 20.0) * (1.0 - 0.85 * distance);
    const auto freeze = get(parameters, pane, pro::reverbFreeze, 0.0) >= 0.5;
    const auto span = std::max(0.2, predelay + 0.015 * distance + std::min(decay, 12.0) * 0.9);
    const auto xOf = [&](const double seconds) { return inner.min.x + static_cast<float>(seconds / span) * inner.width(); };
    const auto yOfDb = [&](const double db) { return inner.max.y - static_cast<float>(std::clamp((db + 60.0) / 60.0, 0.0, 1.0)) * inner.height(); };
    // The dry hit, then the reflections.
    list->AddLine(ImVec2(inner.min.x, inner.max.y), ImVec2(inner.min.x, inner.min.y), theme::alpha(p.text, 0.7f), dp(1.5f));
    constexpr std::array<double, 12> taps { 7.1, 11.3, 17.9, 23.3, 29.2, 37.7, 43.1, 51.9, 59.3, 66.7, 75.1, 83.9 };
    constexpr std::array<double, 12> levels { 0.9, 0.82, 0.74, 0.66, 0.6, 0.54, 0.48, 0.43, 0.38, 0.34, 0.3, 0.27 };
    for (std::size_t tap = 0; tap < taps.size(); ++tap) {
        const auto t = predelay + taps[tap] * 0.001 * (0.3 + 0.9 * space);
        const auto db = 20.0 * std::log10(std::max(1.0e-6, levels[tap] * early * 0.7));
        list->AddLine(ImVec2(xOf(t), inner.max.y), ImVec2(xOf(t), yOfDb(db)), theme::alpha(colour, 0.9f), dp(1.2f));
    }
    // The tail.
    const auto start = predelay + 0.015 * distance;
    std::vector<ImVec2> tail;
    for (auto step = 0; step <= 64; ++step) {
        const auto t = start + (span - start) * static_cast<double>(step) / 64.0;
        const auto rise = std::min(1.0, (t - start) / 0.04);
        const auto db = freeze ? -9.0 : -9.0 - 60.0 * (t - start) / std::max(0.1, decay) + 20.0 * std::log10(std::max(1.0e-3, rise));
        tail.emplace_back(xOf(t), yOfDb(db));
    }
    fillUnder(list, tail, inner.max.y, theme::alpha(colour, 0.22f));
    list->AddPolyline(tail.data(), static_cast<int>(tail.size()), colour, dp(1.5f));
    char label[48];
    std::snprintf(label, sizeof(label), "%s %.0f ms", L("pré-délai", "pre-delay"), predelay * 1'000.0);
    text(list, ImVec2(inner.min.x + dp(4.0f), inner.min.y), p.textDim, label, 0.6f);
}

bool reverbLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                 const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 34.0f;
    const auto panelHeight = knobHeight(diameter) * 2.0f + dp(44.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(170.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= reverbDisplay(parameters, pane, inner, colour, live, set, true);
    const auto insetWidth = std::min(dp(280.0f), inner.width() * 0.32f);
    const auto insetHeight = std::min(dp(110.0f), inner.height() * 0.4f);
    const Box inset { ImVec2(inner.max.x - insetWidth - dp(10.0f), inner.min.y + dp(30.0f)),
                      ImVec2(inner.max.x - dp(10.0f), inner.min.y + dp(30.0f) + insetHeight) };
    reverbImpulse(parameters, pane, inset, colour, live.tempo);
    title(list, inner, L("RÉVERBE PRO  ·  décroissance par fréquence", "PRO REVERB  ·  decay over frequency"));
    list->PopClipRect();

    // Two rows: the space, then the decay's bands and what follows it.
    auto y = outer.max.y + dp(8.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 9> first { { { pro::reverbSpace, L("Espace", "Space") },
                                        { pro::reverbDecay, L("Décroissance", "Decay") },
                                        { pro::reverbPredelay, L("Pré-délai", "Pre-delay") },
                                        { pro::reverbSync, L("Synchro", "Sync") },
                                        { pro::reverbDistance, L("Distance", "Distance") },
                                        { pro::reverbBrightness, L("Brillance", "Brightness") },
                                        { pro::reverbCharacter, L("Caractère", "Character") },
                                        { pro::reverbWidth, L("Largeur", "Width") },
                                        { pro::reverbMix, L("Mélange", "Mix") } } };
    const std::array<Item, 13> second { { { pro::reverbLowDecay, L("Graves x", "Low x") },
                                          { pro::reverbLowFrequency, L("Fréq. graves", "Low freq") },
                                          { pro::reverbMidDecay, L("Médiums x", "Mid x") },
                                          { pro::reverbMidFrequency, L("Fréq. méd.", "Mid freq") },
                                          { pro::reverbHighDecay, L("Aigus x", "High x") },
                                          { pro::reverbHighFrequency, L("Fréq. aigus", "High freq") },
                                          { pro::reverbLowCut, L("Coupe-bas", "Low cut") },
                                          { pro::reverbHighCut, L("Coupe-haut", "High cut") },
                                          { pro::reverbDiffusion, L("Diffusion", "Diffusion") },
                                          { pro::reverbEarly, L("Réflexions", "Early") },
                                          { pro::reverbDucking, L("Ducking", "Ducking") },
                                          { pro::reverbDuckRelease, L("Retour duck.", "Duck release") },
                                          { pro::reverbOutput, L("Sortie", "Output") } } };
    section(list, ImVec2(position.x + dp(4.0f), y), L("ESPACE", "SPACE"));
    y += dp(12.0f);
    auto cell = std::min(dp(92.0f), (size.x - dp(8.0f)) / static_cast<float>(first.size()));
    for (std::size_t index = 0; index < first.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index), y), cell,
                            at(parameters, pane, first[index].index), first[index].label, colour, live, set, diameter);
    y += knobHeight(diameter) + dp(6.0f);
    section(list, ImVec2(position.x + dp(4.0f), y),
            L("DÉCROISSANCE PAR BANDE  ·  APRÈS", "DECAY PER BAND  ·  AFTER"));
    y += dp(12.0f);
    const auto freezeWidth = dp(64.0f);
    cell = std::min(dp(84.0f), (size.x - dp(8.0f) - freezeWidth) / static_cast<float>(second.size()));
    for (std::size_t index = 0; index < second.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index), y), cell,
                            at(parameters, pane, second[index].index), second[index].label, colour, live, set, diameter);
    changed |= switchCell(L("Gel", "Freeze"), at(parameters, pane, pro::reverbFreeze),
                          ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(second.size()) + dp(8.0f), y + dp(8.0f)),
                          ImVec2(freezeWidth - dp(12.0f), dp(24.0f)), IM_COL32(120, 200, 255, 255), set,
                          L("Fige la queue : elle tient sans fin, rien de nouveau n'y entre.",
                            "Freezes the tail: it holds forever, nothing new goes in."));
    return changed;
}

// ---- Limiteur Pro -------------------------------------------------------------------------------

// The loudness the interface measures on the limiter's output: K-weighted,
// in blocks of 100 ms (momentary over 400 ms, short-term over 3 s,
// integrated with the two gates of BS.1770), and the true peak by
// four-times interpolation. It reads the analysis frames, the newest samples
// since the last one.
class Loudness {
public:
    void reset()
    {
        *this = Loudness {};
    }

    void feed(const engine::AudioAnalysisFrame* frame, const double now)
    {
        if (frame == nullptr || frame->sequence == sequence_ || frame->waveformCount == 0)
            return;
        sequence_ = frame->sequence;
        const auto rate = std::max(8'000.0, static_cast<double>(frame->sampleRate));
        if (rate != rate_) {
            rate_ = rate;
            design();
        }
        const auto count = static_cast<std::size_t>(frame->waveformCount);
        auto fresh = last_ > 0.0 ? static_cast<std::size_t>(std::lround((now - last_) * rate)) : count / 4;
        last_ = now;
        fresh = std::clamp<std::size_t>(fresh, 1, count);
        const auto blockLength = static_cast<int>(rate * 0.1);
        for (auto index = count - fresh; index < count; ++index) {
            const auto x = static_cast<double>(frame->waveform[index]);
            peakHold_ = std::max(peakHold_, std::abs(x));
            // True peak: between the last two samples (Catmull-Rom, with
            // the one before and this one around them).
            for (auto phase = 1; phase < 4; ++phase) {
                const auto t = static_cast<double>(phase) / 4.0;
                const auto p0 = history_[2];
                const auto p1 = history_[1];
                const auto p2 = history_[0];
                const auto p3 = x;
                const auto between = 0.5 * (2.0 * p1 + (p2 - p0) * t + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t
                                            + (3.0 * p1 - p0 - 3.0 * p2 + p3) * t * t * t);
                truePeak_ = std::max(truePeak_, std::abs(between));
            }
            history_[2] = history_[1];
            history_[1] = history_[0];
            history_[0] = x;
            auto y = x;
            for (std::size_t stage = 0; stage < 2; ++stage) {
                const auto& c = stages_[stage];
                auto& s = states_[stage];
                const auto out = c[0] * y + s[0];
                s[0] = c[1] * y - c[3] * out + s[1];
                s[1] = c[2] * y - c[4] * out;
                y = out;
            }
            sum_ += y * y;
            if (++inBlock_ >= blockLength) {
                blocks_[static_cast<std::size_t>(blockIndex_)] = sum_ / static_cast<double>(inBlock_);
                blockIndex_ = (blockIndex_ + 1) % static_cast<int>(blocks_.size());
                filled_ = std::min(filled_ + 1, static_cast<int>(blocks_.size()));
                sum_ = 0.0;
                inBlock_ = 0;
                // Every 100 ms, a 400 ms block for the integrated loudness.
                if (filled_ >= 4 && gated_.size() < 36'000)
                    gated_.push_back(mean(4));
                if (filled_ >= 4)
                    momentaryMax_ = std::max(momentaryMax_, lufs(mean(4)));
                // The short-term loudness every 100 ms: the history drawn,
                // and the range (once three seconds are in).
                shortTermHistory_.push_back(static_cast<float>(lufs(mean(30))));
                if (shortTermHistory_.size() > 6'000)
                    shortTermHistory_.erase(shortTermHistory_.begin(), shortTermHistory_.begin() + 600);
                if (filled_ >= 30) {
                    shortTermMax_ = std::max(shortTermMax_, lufs(mean(30)));
                    if (shortTerms_.size() < 36'000)
                        shortTerms_.push_back(lufs(mean(30)));
                }
            }
        }
    }

    [[nodiscard]] double momentary() const { return lufs(mean(4)); }
    [[nodiscard]] double shortTerm() const { return lufs(mean(30)); }
    [[nodiscard]] double integrated() const
    {
        auto sum = 0.0;
        auto count = 0;
        for (const auto energy : gated_)
            if (lufs(energy) > -70.0) {
                sum += energy;
                ++count;
            }
        if (count == 0)
            return -120.0;
        const auto relative = lufs(sum / count) - 10.0;
        sum = 0.0;
        count = 0;
        for (const auto energy : gated_)
            if (lufs(energy) > -70.0 && lufs(energy) > relative) {
                sum += energy;
                ++count;
            }
        return count > 0 ? lufs(sum / count) : -120.0;
    }
    [[nodiscard]] double truePeakDb() const { return 20.0 * std::log10(std::max(1.0e-9, std::max(truePeak_, peakHold_))); }
    [[nodiscard]] double momentaryMax() const { return momentaryMax_; }
    [[nodiscard]] double shortTermMax() const { return shortTermMax_; }
    // The short-term loudness every 100 ms, oldest first (ten minutes at most).
    [[nodiscard]] const std::vector<float>& history() const { return shortTermHistory_; }
    // The loudness range (EBU Tech 3342): the spread of the short-term
    // loudness, gated at -70 LUFS and 20 LU under its mean, 10th to 95th
    // percentile.
    [[nodiscard]] double loudnessRange() const
    {
        std::vector<double> kept;
        auto energy = 0.0;
        for (const auto value : shortTerms_)
            if (value > -70.0) {
                kept.push_back(value);
                energy += std::pow(10.0, value / 10.0);
            }
        if (kept.size() < 2)
            return 0.0;
        const auto relative = 10.0 * std::log10(energy / static_cast<double>(kept.size())) - 20.0;
        std::erase_if(kept, [relative](const double value) { return value < relative; });
        if (kept.size() < 2)
            return 0.0;
        std::sort(kept.begin(), kept.end());
        const auto at = [&kept](const double share) {
            return kept[static_cast<std::size_t>(std::lround(share * static_cast<double>(kept.size() - 1)))];
        };
        return at(0.95) - at(0.10);
    }
    [[nodiscard]] double samplePeakDb() const { return 20.0 * std::log10(std::max(1.0e-9, peakHold_)); }

private:
    [[nodiscard]] double mean(const int blocks) const
    {
        const auto count = std::min(blocks, filled_);
        if (count == 0)
            return 0.0;
        auto sum = 0.0;
        for (auto index = 1; index <= count; ++index)
            sum += blocks_[static_cast<std::size_t>((blockIndex_ - index + static_cast<int>(blocks_.size())) % static_cast<int>(blocks_.size()))];
        return sum / count;
    }
    // A mono mix of a stereo pair: both channels counted.
    [[nodiscard]] static double lufs(const double energy)
    {
        return energy > 0.0 ? -0.691 + 10.0 * std::log10(energy) + 3.01 : -120.0;
    }
    // BS.1770's two stages (a high shelf, then a high-pass), at this rate.
    void design()
    {
        const auto shelf = [this] {
            const auto f0 = 1'681.974450955533;
            const auto g = 3.999843853973347;
            const auto q = 0.7071752369554196;
            const auto k = std::tan(std::numbers::pi * f0 / rate_);
            const auto vh = std::pow(10.0, g / 20.0);
            const auto vb = std::pow(vh, 0.4996667741545416);
            const auto a0 = 1.0 + k / q + k * k;
            return std::array<double, 5> { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                                           2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        };
        const auto highPass = [this] {
            const auto f0 = 38.13547087602444;
            const auto q = 0.5003270373238773;
            const auto k = std::tan(std::numbers::pi * f0 / rate_);
            const auto a0 = 1.0 + k / q + k * k;
            return std::array<double, 5> { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
        };
        stages_ = { shelf(), highPass() };
        states_ = {};
    }

    std::uint64_t sequence_ {};
    double last_ {};
    double rate_ {};
    std::array<std::array<double, 5>, 2> stages_ {};
    std::array<std::array<double, 2>, 2> states_ {};
    std::array<double, 3> history_ {};
    double sum_ {};
    int inBlock_ {};
    std::array<double, 30> blocks_ {};
    int blockIndex_ {};
    int filled_ {};
    std::vector<double> gated_;
    std::vector<float> shortTermHistory_;
    std::vector<double> shortTerms_;
    double momentaryMax_ { -120.0 };
    double shortTermMax_ { -120.0 };
    double truePeak_ {};
    double peakHold_ {};
};

Loudness& loudnessOf(std::vector<Param>& parameters, const Pane& pane)
{
    static std::map<juce::String, Loudness> meters;
    const auto* first = at(parameters, pane, 0);
    return meters[first != nullptr ? first->binding : juce::String("?")];
}

// The levels scrolling by: what goes in (with the gain, grey), what comes
// out (colour), the gain taken (red, from the top); the ceiling to drag.
bool limiterDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                    const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    const auto gain = get(parameters, pane, pro::limiterGain, 0.0);
    auto* output = at(parameters, pane, pro::limiterOutput);
    const auto ceiling = output != nullptr ? output->value : -1.0;
    constexpr auto topDb = 3.0;
    constexpr auto bottomDb = -42.0;
    const auto yOfDb = [&](const double db) {
        return box.min.y + static_cast<float>((topDb - std::clamp(db, bottomDb, topDb)) / (topDb - bottomDb)) * box.height();
    };
    for (const auto db : { 0.0, -6.0, -12.0, -18.0, -24.0, -30.0, -36.0 }) {
        const auto y = yOfDb(db);
        list->AddLine(ImVec2(box.min.x, y), ImVec2(box.max.x, y), db == 0.0 ? p.gridStrong : p.grid);
        if (large) {
            char label[8];
            std::snprintf(label, sizeof(label), "%.0f", db);
            text(list, ImVec2(box.min.x + dp(3.0f), y - ImGui::GetFontSize() * 0.66f), p.textFaint, label, 0.56f);
        }
    }
    const auto count = live.historyCount;
    if (count > 1 && live.inputHistory != nullptr && live.outputHistory != nullptr) {
        std::vector<ImVec2> in;
        std::vector<ImVec2> out;
        in.reserve(static_cast<std::size_t>(count));
        out.reserve(static_cast<std::size_t>(count));
        const auto step = box.width() / static_cast<float>(count - 1);
        for (auto index = 0; index < count; ++index) {
            const auto x = box.min.x + step * static_cast<float>(index);
            const auto inDb = static_cast<double>(live.inputHistory[index]) + gain;
            const auto outDb = static_cast<double>(live.outputHistory[index]);
            in.emplace_back(x, yOfDb(inDb));
            out.emplace_back(x, yOfDb(outDb));
            auto quietest = inDb;
            if (index > 0)
                quietest = std::min(quietest, static_cast<double>(live.inputHistory[index - 1]) + gain);
            if (index + 1 < count)
                quietest = std::min(quietest, static_cast<double>(live.inputHistory[index + 1]) + gain);
            const auto reduction = std::max(0.0, quietest - outDb);
            if (reduction > 0.05) {
                const auto height = static_cast<float>(std::min(24.0, reduction) / 24.0) * box.height() * 0.4f;
                list->AddRectFilled(ImVec2(x, box.min.y), ImVec2(x + std::max(1.0f, step), box.min.y + height),
                                    theme::alpha(kReduction, 0.75f));
            }
        }
        fillUnder(list, in, box.max.y, theme::alpha(p.textDim, 0.22f));
        fillUnder(list, out, box.max.y, theme::alpha(colour, 0.35f));
        list->AddPolyline(out.data(), static_cast<int>(out.size()), colour, dp(1.2f));
    }
    // The ceiling.
    const auto ceilingY = yOfDb(ceiling);
    ImGui::SetCursorScreenPos(ImVec2(box.min.x, ceilingY - dp(6.0f)));
    ImGui::InvisibleButton("##ceiling", ImVec2(std::max(1.0f, box.width()), dp(12.0f)));
    const auto hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
        changed |= apply(output, topDb - (io.MousePos.y - box.min.y) / std::max(1.0f, box.height()) * (topDb - bottomDb), set);
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
        changed |= apply(output, ceiling + io.MouseWheel * 0.1, set);
        ui::takeWheel();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && output != nullptr)
        changed |= apply(output, output->defaultValue, set);
    dashedLine(list, ImVec2(box.min.x, ceilingY), ImVec2(box.max.x, ceilingY), hovered ? p.text : theme::brighter(colour, 0.2f),
               dp(hovered ? 2.0f : 1.4f));
    char label[48];
    std::snprintf(label, sizeof(label), "%s %.1f dB", L("Plafond", "Ceiling"), ceiling);
    const auto size = textSize(label, 0.68f);
    text(list, ImVec2(box.max.x - size.x - dp(6.0f), ceilingY + dp(3.0f)), p.text, label, 0.68f);
    if (count > 1 && live.inputHistory != nullptr && live.outputHistory != nullptr) {
        const auto reduction = std::max(0.0, std::min(static_cast<double>(live.inputHistory[count - 1]),
                                                      static_cast<double>(live.inputHistory[count - 2]))
                                                 + gain - static_cast<double>(live.outputHistory[count - 1]));
        std::snprintf(label, sizeof(label), "GR %.1f dB", reduction);
        text(list, ImVec2(box.min.x + dp(6.0f), box.min.y + dp(16.0f)), kReduction, label, 0.72f);
    }
    return changed;
}

// The meters beside the display: peak, true peak, momentary, short-term and
// integrated loudness, against the target.
void loudnessMeters(Loudness& meter, const Box& box, const ImU32 colour, const double target, const bool compact)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(box.min, box.max, theme::alpha(kDark, 0.6f), dp(4.0f));
    struct Bar {
        const char* label;
        double value;
        bool loudness;
    };
    const std::array<Bar, 5> bars { { { L("Crête", "Peak"), meter.samplePeakDb(), false },
                                      { "TP", meter.truePeakDb(), false },
                                      { "M", meter.momentary(), true },
                                      { "S", meter.shortTerm(), true },
                                      { "I", meter.integrated(), true } } };
    const auto inner = box.inset(dp(6.0f));
    const auto labelHeight = ImGui::GetFontSize() * 1.6f;
    const auto barTop = inner.min.y + labelHeight;
    const auto barBottom = inner.max.y - labelHeight;
    const auto cell = inner.width() / static_cast<float>(bars.size());
    const auto yOf = [&](const double value, const bool loudness) {
        const auto low = loudness ? -40.0 : -48.0;
        const auto high = loudness ? 0.0 : 0.0;
        return barBottom - static_cast<float>(std::clamp((value - low) / (high - low), 0.0, 1.0)) * (barBottom - barTop);
    };
    for (std::size_t index = 0; index < bars.size(); ++index) {
        const auto& bar = bars[index];
        const auto x0 = inner.min.x + cell * static_cast<float>(index) + dp(3.0f);
        const auto x1 = x0 + cell - dp(6.0f);
        list->AddRectFilled(ImVec2(x0, barTop), ImVec2(x1, barBottom), p.bg2, dp(2.0f));
        const auto y = yOf(bar.value, bar.loudness);
        auto fill = colour;
        if (bar.loudness && bar.value > target + 1.0)
            fill = p.warn;
        else if (!bar.loudness && bar.value > -0.1)
            fill = kReduction;
        list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, barBottom), fill, dp(2.0f));
        if (bar.loudness) {
            const auto targetY = yOf(target, true);
            list->AddLine(ImVec2(x0 - dp(2.0f), targetY), ImVec2(x1 + dp(2.0f), targetY), p.text, dp(1.5f));
        }
        centred(list, ImVec2((x0 + x1) * 0.5f, inner.min.y + labelHeight * 0.45f), p.textDim, bar.label, 0.62f);
        char value[16];
        std::snprintf(value, sizeof(value), bar.value < -99.0 ? "-inf" : "%.1f", bar.value);
        centred(list, ImVec2((x0 + x1) * 0.5f, barBottom + labelHeight * 0.5f), p.text, value, compact ? 0.56f : 0.62f);
    }
}

bool limiterLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                  const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    auto& meter = loudnessOf(parameters, pane);
    meter.feed(live.afterFrame, ImGui::GetTime());
    const auto diameter = 34.0f;
    const auto panelHeight = dp(42.0f) + std::max(knobHeight(diameter), dp(4.0f * 28.0f)) + dp(12.0f);
    const auto metersWidth = std::min(dp(240.0f), size.x * 0.28f);
    const Box outer { position, ImVec2(position.x + size.x - metersWidth - dp(8.0f),
                                       position.y + std::max(dp(170.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= limiterDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("LIMITEUR PRO", "PRO LIMITER"));
    list->PopClipRect();
    const Box meters { ImVec2(outer.max.x + dp(8.0f), outer.min.y), ImVec2(position.x + size.x, outer.max.y - dp(28.0f)) };
    loudnessMeters(meter, meters, colour, get(parameters, pane, pro::limiterTarget, -14.0), false);
    ImGui::SetCursorScreenPos(ImVec2(meters.min.x, meters.max.y + dp(4.0f)));
    if (ImGui::Button(L("Remettre la mesure à zéro", "Reset the meters"), ImVec2(meters.width(), dp(22.0f))))
        meter.reset();

    auto y = outer.max.y + dp(8.0f);
    section(list, ImVec2(position.x + dp(4.0f), y), L("STYLE", "STYLE"));
    y += dp(12.0f);
    changed |= segmented("##style", at(parameters, pane, pro::limiterStyle),
                         { L("Transparent", "Transparent"), L("Percutant", "Punchy"), L("Dynamique", "Dynamic"),
                           L("Polyvalent", "Allround"), L("Agressif", "Aggressive"), L("Moderne", "Modern"), L("Sûr", "Safe"),
                           "Bus" },
                         ImVec2(position.x + dp(4.0f), y), size.x - dp(8.0f), dp(22.0f), colour, set,
                         L("La façon de limiter : transparent pour le mastering, percutant et agressif pour garder ou "
                           "pousser l'attaque, dynamique et moderne pour un niveau qui respire, sûr pour aucune distorsion.",
                           "How it limits: transparent for mastering, punchy and aggressive to keep or push the attack, "
                           "dynamic and modern for a level that breathes, safe for no distortion at all."));
    y += dp(30.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 8> knobs { { { pro::limiterGain, L("Gain", "Gain") },
                                        { pro::limiterOutput, L("Plafond", "Ceiling") },
                                        { pro::limiterLookahead, L("Anticipation", "Lookahead") },
                                        { pro::limiterAttack, L("Attaque", "Attack") },
                                        { pro::limiterRelease, L("Relâchement", "Release") },
                                        { pro::limiterLinkTransients, L("Lien transit.", "Link trans.") },
                                        { pro::limiterLinkRelease, L("Lien relâch.", "Link release") },
                                        { pro::limiterTarget, L("Cible", "Target") } } };
    const auto cell = std::min(dp(96.0f), (size.x - dp(8.0f)) / static_cast<float>(knobs.size()));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index), y), cell,
                            at(parameters, pane, knobs[index].index), knobs[index].label, colour, live, set, diameter);
    // Options beside the knobs when there is room, else under them.
    auto x = position.x + dp(4.0f) + cell * static_cast<float>(knobs.size()) + dp(12.0f);
    auto optionsY = y;
    if (position.x + size.x - x < dp(300.0f)) {
        x = position.x + dp(4.0f);
        optionsY = y + knobHeight(diameter) + dp(6.0f);
    }
    const auto right = position.x + size.x - dp(4.0f);
    const auto rowWidth = right - x;
    const auto rowHeight = dp(22.0f);
    changed |= switchCell("True peak", at(parameters, pane, pro::limiterTruePeak), ImVec2(x, optionsY), ImVec2(dp(84.0f), rowHeight), colour,
                          set, L("Rattrape aussi les crêtes entre les échantillons (ce que la conversion fera entendre).",
                                 "Also catches the peaks between samples (what conversion will make heard)."));
    changed |= switchCell(L("Filtre DC", "DC filter"), at(parameters, pane, pro::limiterDcFilter),
                          ImVec2(x + dp(90.0f), optionsY), ImVec2(dp(84.0f), rowHeight), colour, set);
    changed |= switchCell(L("Gain unité", "Unity gain"), at(parameters, pane, pro::limiterUnity), ImVec2(x + dp(180.0f), optionsY),
                          ImVec2(dp(96.0f), rowHeight), colour, set,
                          L("Écoute au niveau d'entrée : le gain retiré, pour comparer sans être trompé par le volume.",
                            "Listen at the input level: the gain taken back, to compare without being fooled by loudness."));
    auto rowY = optionsY + rowHeight + dp(6.0f);
    text(list, ImVec2(x, rowY + dp(3.0f)), p.textDim, L("Suréch.", "Oversampl."), 0.66f);
    changed |= segmented("##oversampling", at(parameters, pane, pro::limiterOversampling), { "1x", "2x", "4x", "8x" },
                         ImVec2(x + dp(70.0f), rowY), std::min(dp(170.0f), rowWidth - dp(70.0f)), rowHeight, colour, set);
    rowY += rowHeight + dp(6.0f);
    text(list, ImVec2(x, rowY + dp(3.0f)), p.textDim, "Dither", 0.66f);
    changed |= segmented("##dither", at(parameters, pane, pro::limiterDither), { L("Non", "Off"), "24", "20", "18", "16" },
                         ImVec2(x + dp(70.0f), rowY), std::min(dp(200.0f), rowWidth - dp(70.0f)), rowHeight, colour, set);
    rowY += rowHeight + dp(6.0f);
    text(list, ImVec2(x, rowY + dp(3.0f)), p.textDim, L("Bruit", "Shaping"), 0.66f);
    changed |= segmented("##shaping", at(parameters, pane, pro::limiterShaping),
                         { L("Basique", "Basic"), L("Optimisé", "Optimized"), L("Pondéré", "Weighted") }, ImVec2(x + dp(70.0f), rowY),
                         std::min(dp(240.0f), rowWidth - dp(70.0f)), rowHeight, colour, set,
                         L("Où le bruit du dither est repoussé : là où l'oreille l'entend le moins.",
                           "Where the dither's noise is pushed: where the ear hears it least."));
    return changed;
}

// ---- Filtre Pro ------------------------------------------------------------------------------------

struct FilterUnitView {
    bool on {};
    int type {};
    int slope {};
    double cutoff {};
    double resonance {};
};

FilterUnitView unitOf(std::vector<Param>& parameters, const Pane& pane, const int unit)
{
    return { get(parameters, pane, pro::filterIndex(unit, pro::unitOn), 0.0) >= 0.5,
             static_cast<int>(std::lround(get(parameters, pane, pro::filterIndex(unit, pro::unitType), 0.0))),
             static_cast<int>(std::lround(get(parameters, pane, pro::filterIndex(unit, pro::unitSlope), 1.0))),
             get(parameters, pane, pro::filterIndex(unit, pro::unitCutoff), 1'000.0),
             get(parameters, pane, pro::filterIndex(unit, pro::unitResonance), 0.2) };
}

// What the modulation does now, as the interface can tell: the LFOs from the
// clock and the tempo, the follower from the level coming in.
std::array<double, pro::modulationDestinationCount> modulationNow(std::vector<Param>& parameters, const Pane& pane,
                                                                  const Live& live, std::array<double, pro::modulationDestinationCount>* range)
{
    std::array<double, pro::modulationDestinationCount> moved {};
    const auto now = ImGui::GetTime();
    std::array<double, 2> lfo {};
    for (int index = 0; index < 2; ++index) {
        const auto base = index == 0 ? pro::filterLfo1Rate : pro::filterLfo2Rate;
        const auto shape = static_cast<int>(std::lround(get(parameters, pane, base + 1, 0.0)));
        const auto* division = engine::noteDivisionFor(static_cast<float>(get(parameters, pane, base + 2, 0.0)));
        const auto hz = division != nullptr ? std::max(20.0, live.tempo) / 60.0 / division->beats : get(parameters, pane, base, 1.0);
        const auto phase = now * hz;
        const auto cycle = std::floor(phase);
        const auto random = std::sin(cycle * 12.9898 + index * 78.233) * 43'758.5453;
        lfo[static_cast<std::size_t>(index)] = pro::lfoShapeValue(shape, phase, 2.0 * (random - std::floor(random)) - 1.0);
    }
    const auto follower = std::clamp((static_cast<double>(live.inputDb) + get(parameters, pane, pro::filterFollowGain, 0.0) + 60.0) / 60.0,
                                     0.0, 1.0);
    if (range != nullptr)
        range->fill(0.0);
    for (int slot = 0; slot < pro::filterMatrixSlots; ++slot) {
        const auto source = static_cast<int>(std::lround(get(parameters, pane, pro::matrixIndex(slot, pro::matrixSource), 0.0)));
        const auto destination = static_cast<int>(std::lround(get(parameters, pane, pro::matrixIndex(slot, pro::matrixDestination), 0.0)));
        const auto amount = get(parameters, pane, pro::matrixIndex(slot, pro::matrixAmount), 0.0);
        if (source == pro::sourceNone || destination <= pro::destinationNone || destination >= pro::modulationDestinationCount)
            continue;
        const auto signal = source == pro::sourceLfo1 ? lfo[0] : source == pro::sourceLfo2 ? lfo[1] : follower;
        moved[static_cast<std::size_t>(destination)] += signal * amount;
        if (range != nullptr)
            (*range)[static_cast<std::size_t>(destination)] += std::abs(amount);
    }
    return moved;
}

double movedCutoff(const FilterUnitView& unit, const int index, const std::array<double, pro::modulationDestinationCount>& moved)
{
    const auto octaves = (moved[static_cast<std::size_t>(pro::destinationCutoff1 + index * 2)] + moved[pro::destinationAllCutoffs])
                         * pro::modulationOctaves;
    return std::clamp(unit.cutoff * std::pow(2.0, octaves), 20.0, 20'000.0);
}

double movedResonance(const FilterUnitView& unit, const int index, const std::array<double, pro::modulationDestinationCount>& moved)
{
    return std::clamp(unit.resonance + moved[static_cast<std::size_t>(pro::destinationResonance1 + index * 2)], 0.0, 1.0);
}

const char* typeName(const int type, const bool shortName)
{
    static const char* longFr[] { "Passe-bas", "Passe-haut", "Passe-bande", "Réjecteur", "Pic", "Peigne" };
    static const char* longEn[] { "Low-pass", "High-pass", "Band-pass", "Notch", "Peak", "Comb" };
    static const char* shortFr[] { "PB", "PH", "BD", "Rej", "Pic", "Pgn" };
    static const char* shortEn[] { "LP", "HP", "BP", "Ntc", "Pk", "Cmb" };
    const auto index = static_cast<std::size_t>(std::clamp(type, 0, pro::filterTypeCount - 1));
    return shortName ? (ui::french() ? shortFr[index] : shortEn[index]) : (ui::french() ? longFr[index] : longEn[index]);
}

// The four filters over the spectrum: each its curve and its handle
// (cutoff sideways, resonance up and down; a right click for its type), the
// sum of them in white, the reach of its modulation under its handle.
bool filterDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                   const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto& io = ImGui::GetIO();
    auto changed = false;
    constexpr auto topDb = 24.0;
    constexpr auto bottomDb = -48.0;
    const auto yOfDb = [&](const double db) {
        return box.min.y + static_cast<float>((topDb - std::clamp(db, bottomDb, topDb)) / (topDb - bottomDb)) * box.height();
    };
    frequencyGrid(list, box, large);
    for (const auto db : { 12.0, 0.0, -12.0, -24.0, -36.0 }) {
        const auto y = yOfDb(db);
        list->AddLine(ImVec2(box.min.x, y), ImVec2(box.max.x, y), db == 0.0 ? p.gridStrong : p.grid);
        if (large) {
            char label[8];
            std::snprintf(label, sizeof(label), "%+.0f", db);
            text(list, ImVec2(box.min.x + dp(3.0f), y - ImGui::GetFontSize() * 0.66f), p.textFaint, label, 0.56f);
        }
    }
    spectrumBehind(list, box, live.after, colour);

    std::array<double, pro::modulationDestinationCount> range {};
    const auto moved = modulationNow(parameters, pane, live, &range);
    std::array<FilterUnitView, pro::filterUnits> units {};
    for (int unit = 0; unit < pro::filterUnits; ++unit)
        units[static_cast<std::size_t>(unit)] = unitOf(parameters, pane, unit);
    const auto routing = static_cast<int>(std::lround(get(parameters, pane, pro::filterRouting, 0.0)));
    const auto mix = get(parameters, pane, pro::filterMix, 1.0);

    // Each filter's curve, as modulated now.
    const auto steps = std::max(64, static_cast<int>(box.width() / dp(3.0f)));
    std::vector<std::array<double, pro::filterUnits>> responses(static_cast<std::size_t>(steps + 1));
    for (int unit = 0; unit < pro::filterUnits; ++unit) {
        const auto& view = units[static_cast<std::size_t>(unit)];
        if (!view.on)
            continue;
        const auto cutoff = movedCutoff(view, unit, moved);
        const auto resonance = movedResonance(view, unit, moved);
        std::vector<ImVec2> points;
        points.reserve(static_cast<std::size_t>(steps + 1));
        for (auto step = 0; step <= steps; ++step) {
            const auto x = box.min.x + box.width() * static_cast<float>(step) / static_cast<float>(steps);
            const auto db = pro::filterMagnitudeDb(view.type, view.slope, cutoff, resonance, hzOfX(box, x));
            responses[static_cast<std::size_t>(step)][static_cast<std::size_t>(unit)] = db;
            points.emplace_back(x, yOfDb(db));
        }
        list->AddPolyline(points.data(), static_cast<int>(points.size()), theme::alpha(kUnitColours[static_cast<std::size_t>(unit)], 0.8f),
                          dp(1.3f));
    }
    // Their sum through the routing and the mix.
    std::vector<ImVec2> total;
    total.reserve(static_cast<std::size_t>(steps + 1));
    for (auto step = 0; step <= steps; ++step) {
        const auto x = box.min.x + box.width() * static_cast<float>(step) / static_cast<float>(steps);
        const auto& row = responses[static_cast<std::size_t>(step)];
        const auto gainOf = [&](const int unit) { return std::pow(10.0, row[static_cast<std::size_t>(unit)] / 20.0); };
        auto gain = 1.0;
        if (routing == pro::routingParallel) {
            auto sum = 0.0;
            auto count = 0;
            for (int unit = 0; unit < pro::filterUnits; ++unit)
                if (units[static_cast<std::size_t>(unit)].on) {
                    sum += gainOf(unit);
                    ++count;
                }
            gain = count > 0 ? sum / count : 1.0;
        } else if (routing == pro::routingPairs) {
            auto a = 1.0;
            auto b = 1.0;
            for (int unit = 0; unit < 2; ++unit)
                if (units[static_cast<std::size_t>(unit)].on)
                    a *= gainOf(unit);
            for (int unit = 2; unit < 4; ++unit)
                if (units[static_cast<std::size_t>(unit)].on)
                    b *= gainOf(unit);
            gain = 0.5 * (a + b);
        } else {
            for (int unit = 0; unit < pro::filterUnits; ++unit)
                if (units[static_cast<std::size_t>(unit)].on)
                    gain *= gainOf(unit);
        }
        gain = (1.0 - mix) + mix * gain;
        total.emplace_back(x, yOfDb(20.0 * std::log10(std::max(1.0e-6, gain))));
    }
    fillUnder(list, total, yOfDb(0.0), theme::alpha(p.text, 0.06f));
    list->AddPolyline(total.data(), static_cast<int>(total.size()), theme::alpha(p.text, 0.92f), dp(2.0f));

    // Handles: cutoff and resonance.
    auto& state = dragState(parameters, pane);
    const auto zero = yOfDb(0.0);
    const auto lift = box.height() * 0.42f;
    const auto handleOf = [&](const int unit) {
        const auto& view = units[static_cast<std::size_t>(unit)];
        return ImVec2(xOfHz(box, view.cutoff), zero - static_cast<float>(view.resonance) * lift);
    };
    ImGui::SetCursorScreenPos(box.min);
    ImGui::InvisibleButton("##filterDisplay", ImVec2(std::max(1.0f, box.width()), std::max(1.0f, box.height())),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const auto hovered = ImGui::IsItemHovered();
    auto hot = -1;
    if (hovered || ImGui::IsItemActive()) {
        auto best = dp(12.0f);
        for (int unit = 0; unit < pro::filterUnits; ++unit) {
            if (!units[static_cast<std::size_t>(unit)].on)
                continue;
            const auto centre = handleOf(unit);
            const auto distance = std::hypot(centre.x - io.MousePos.x, centre.y - io.MousePos.y);
            if (distance < best) {
                best = distance;
                hot = unit;
            }
        }
    }
    if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state.active = hot;
        if (hot >= 0)
            state.selected = hot;
    }
    if (ImGui::IsItemActive() && state.active >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        changed |= apply(at(parameters, pane, pro::filterIndex(state.active, pro::unitCutoff)), hzOfX(box, io.MousePos.x), set);
        changed |= apply(at(parameters, pane, pro::filterIndex(state.active, pro::unitResonance)), (zero - io.MousePos.y) / lift, set);
    }
    if (ImGui::IsItemDeactivated())
        state.active = -1;
    if (hovered && hot >= 0 && io.MouseWheel != 0.0f) {
        auto* resonance = at(parameters, pane, pro::filterIndex(hot, pro::unitResonance));
        if (resonance != nullptr)
            changed |= apply(resonance, resonance->value + io.MouseWheel * 0.04, set);
        ui::takeWheel();
    }
    // A double click where no filter is: the next one off comes on there.
    if (hovered && hot < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        for (int unit = 0; unit < pro::filterUnits; ++unit)
            if (!units[static_cast<std::size_t>(unit)].on) {
                changed |= apply(at(parameters, pane, pro::filterIndex(unit, pro::unitOn)), 1.0, set);
                changed |= apply(at(parameters, pane, pro::filterIndex(unit, pro::unitCutoff)), hzOfX(box, io.MousePos.x), set);
                state.selected = unit;
                break;
            }
    if (hovered && hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        state.selected = hot;
        ImGui::OpenPopup("##filterMenu");
    }
    if (ImGui::BeginPopup("##filterMenu")) {
        const auto unit = state.selected;
        ImGui::TextDisabled("%s %d", L("Filtre", "Filter"), unit + 1);
        for (int type = 0; type < pro::filterTypeCount; ++type)
            if (ImGui::MenuItem(typeName(type, false), nullptr, units[static_cast<std::size_t>(unit)].type == type))
                changed |= apply(at(parameters, pane, pro::filterIndex(unit, pro::unitType)), type, set);
        ImGui::Separator();
        static const char* slopes[] { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
        for (int slope = 0; slope < pro::filterSlopeCount; ++slope)
            if (ImGui::MenuItem(slopes[slope], nullptr, units[static_cast<std::size_t>(unit)].slope == slope))
                changed |= apply(at(parameters, pane, pro::filterIndex(unit, pro::unitSlope)), slope, set);
        ImGui::Separator();
        if (ImGui::MenuItem(L("Couper ce filtre", "Turn this filter off")))
            changed |= apply(at(parameters, pane, pro::filterIndex(unit, pro::unitOn)), 0.0, set);
        ImGui::EndPopup();
    }
    // Sources dropped on a handle modulate its cutoff.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("FILTERPRO_SOURCE"); payload != nullptr && hot >= 0) {
            const auto source = *static_cast<const int*>(payload->Data);
            for (int slot = 0; slot < pro::filterMatrixSlots; ++slot)
                if (get(parameters, pane, pro::matrixIndex(slot, pro::matrixSource), 0.0) < 0.5) {
                    changed |= apply(at(parameters, pane, pro::matrixIndex(slot, pro::matrixSource)), source, set);
                    changed |= apply(at(parameters, pane, pro::matrixIndex(slot, pro::matrixDestination)),
                                     pro::destinationCutoff1 + hot * 2, set);
                    changed |= apply(at(parameters, pane, pro::matrixIndex(slot, pro::matrixAmount)), 0.25, set);
                    break;
                }
        }
        ImGui::EndDragDropTarget();
    }
    if (hovered && hot >= 0)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    for (int unit = 0; unit < pro::filterUnits; ++unit) {
        const auto& view = units[static_cast<std::size_t>(unit)];
        if (!view.on)
            continue;
        const auto unitColour = kUnitColours[static_cast<std::size_t>(unit)];
        const auto centre = handleOf(unit);
        // Its modulation's reach, and where it is now.
        const auto reach = (range[static_cast<std::size_t>(pro::destinationCutoff1 + unit * 2)] + range[pro::destinationAllCutoffs])
                           * pro::modulationOctaves;
        if (reach > 0.0) {
            const auto low = xOfHz(box, view.cutoff * std::pow(2.0, -reach));
            const auto high = xOfHz(box, view.cutoff * std::pow(2.0, reach));
            list->AddLine(ImVec2(low, centre.y + dp(11.0f)), ImVec2(high, centre.y + dp(11.0f)), theme::alpha(unitColour, 0.7f), dp(3.0f));
            const auto now = xOfHz(box, movedCutoff(view, unit, moved));
            list->AddCircleFilled(ImVec2(now, centre.y + dp(11.0f)), dp(3.5f), p.modulation);
        }
        list->AddLine(ImVec2(centre.x, zero), centre, theme::alpha(unitColour, 0.5f), dp(1.0f));
        const auto isHot = state.active == unit || (state.active < 0 && hot == unit) || state.selected == unit;
        char label[4];
        std::snprintf(label, sizeof(label), "%d", unit + 1);
        handleDot(list, centre, isHot ? theme::brighter(unitColour, 0.2f) : unitColour, isHot, label);
        if (state.active == unit || (state.active < 0 && hot == unit)) {
            const auto hz = view.cutoff < 1'000.0 ? juce::String(juce::roundToInt(view.cutoff)) + " Hz"
                                                  : juce::String(view.cutoff / 1'000.0, 2) + " kHz";
            const auto info = juce::String::fromUTF8(typeName(view.type, false)) + "  " + hz + "  Q "
                              + juce::String(pro::filterQ(view.resonance), 2);
            text(list, ImVec2(std::min(centre.x + dp(10.0f), box.max.x - dp(200.0f)), centre.y - dp(20.0f)), p.text, info.toRawUTF8(), 0.68f);
        }
    }
    if (std::none_of(units.begin(), units.end(), [](const FilterUnitView& view) { return view.on; }))
        centred(list, ImVec2((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f), p.textDim,
                L("Double-clic pour placer un filtre", "Double click to place a filter"), 0.8f);
    return changed;
}

// A modulation source to drag onto a knob or a handle.
void sourceChip(const char* label, const int source, const ImVec2 origin, const ImVec2 size, const ImU32 colour)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushID(source);
    ImGui::InvisibleButton("##source", size);
    const auto hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetTooltip("%s", L("Glissez-le sur une coupure, une résonance ou une poignée pour la moduler.",
                                  "Drag it onto a cutoff, a resonance or a handle to modulate it."));
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
        ImGui::SetDragDropPayload("FILTERPRO_SOURCE", &source, sizeof(source));
        ImGui::TextUnformatted(label);
        ImGui::EndDragDropSource();
    }
    ImGui::PopID();
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), hovered ? theme::brighter(colour, 0.2f) : colour, dp(3.0f));
    centred(list, ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f), kDark, label, 0.68f);
}

// A knob that takes a dropped source: the source modulates it.
bool dropOnto(std::vector<Param>& parameters, const Pane& pane, const int destination, const Setter& set)
{
    auto changed = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("FILTERPRO_SOURCE"); payload != nullptr) {
            const auto source = *static_cast<const int*>(payload->Data);
            for (int slot = 0; slot < pro::filterMatrixSlots; ++slot)
                if (get(parameters, pane, pro::matrixIndex(slot, pro::matrixSource), 0.0) < 0.5) {
                    changed |= apply(at(parameters, pane, pro::matrixIndex(slot, pro::matrixSource)), source, set);
                    changed |= apply(at(parameters, pane, pro::matrixIndex(slot, pro::matrixDestination)), destination, set);
                    changed |= apply(at(parameters, pane, pro::matrixIndex(slot, pro::matrixAmount)), 0.25, set);
                    break;
                }
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

void lfoShapeGlyph(ImDrawList* list, const Box& box, const int shape, const ImU32 colour, const double phase)
{
    std::vector<ImVec2> points;
    for (auto step = 0; step <= 48; ++step) {
        const auto t = static_cast<double>(step) / 48.0;
        const auto random = std::sin(std::floor(t * 4.0) * 12.9898) * 43'758.5453;
        const auto value = pro::lfoShapeValue(shape, t * 2.0, 2.0 * (random - std::floor(random)) - 1.0);
        points.emplace_back(box.min.x + static_cast<float>(t) * box.width(),
                            (box.min.y + box.max.y) * 0.5f - static_cast<float>(value) * box.height() * 0.42f);
    }
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(1.3f));
    const auto x = box.min.x + static_cast<float>(phase - std::floor(phase)) * box.width() * 0.5f;
    list->AddLine(ImVec2(x, box.min.y), ImVec2(x, box.max.y), theme::alpha(colour, 0.5f), dp(1.0f));
}

bool filterLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                 const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto small = 28.0f;
    const auto panelHeight = std::max(dp(84.0f) + (knobHeight(small) + dp(4.0f)) * 2.0f,
                                      dp(26.0f) + dp(50.0f) + knobHeight(small) + dp(19.0f) + dp(24.0f) * 4.0f) + dp(6.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(170.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= filterDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("FILTRE PRO", "PRO FILTER"));
    list->PopClipRect();
    auto& state = dragState(parameters, pane);

    // The four filters, side by side.
    const auto top = outer.max.y + dp(8.0f);
    const auto stripsWidth = std::min(size.x * 0.56f, dp(680.0f));
    const auto strip = stripsWidth / static_cast<float>(pro::filterUnits);
    for (int unit = 0; unit < pro::filterUnits; ++unit) {
        const auto unitColour = kUnitColours[static_cast<std::size_t>(unit)];
        const auto x = position.x + strip * static_cast<float>(unit);
        ImGui::PushID(unit);
        const Box area { ImVec2(x + dp(2.0f), top), ImVec2(x + strip - dp(3.0f), position.y + size.y) };
        list->AddRectFilled(area.min, area.max, state.selected == unit ? theme::mix(p.bg1, unitColour, 0.08f) : p.bg1, dp(4.0f));
        list->AddRectFilled(area.min, ImVec2(area.max.x, area.min.y + dp(3.0f)), unitColour, dp(4.0f), ImDrawFlags_RoundCornersTop);
        char label[16];
        std::snprintf(label, sizeof(label), "%s %d", L("Filtre", "Filter"), unit + 1);
        changed |= switchCell(label, at(parameters, pane, pro::filterIndex(unit, pro::unitOn)), ImVec2(area.min.x + dp(4.0f), area.min.y + dp(6.0f)),
                              ImVec2(area.width() - dp(8.0f), dp(20.0f)), unitColour, set);
        const auto inner2 = area.inset(dp(4.0f));
        std::vector<const char*> types;
        for (int type = 0; type < pro::filterTypeCount; ++type)
            types.push_back(typeName(type, true));
        changed |= segmented("##type", at(parameters, pane, pro::filterIndex(unit, pro::unitType)), types,
                             ImVec2(inner2.min.x, area.min.y + dp(30.0f)), inner2.width(), dp(20.0f), unitColour, set);
        changed |= segmented("##slope", at(parameters, pane, pro::filterIndex(unit, pro::unitSlope)), { "12", "24", "48" },
                             ImVec2(inner2.min.x, area.min.y + dp(54.0f)), inner2.width(), dp(20.0f), unitColour, set,
                             L("La pente : 12, 24 ou 48 dB par octave", "The slope: 12, 24 or 48 dB per octave"));
        const auto cell = inner2.width() * 0.5f;
        const auto knobsTop = area.min.y + dp(80.0f);
        struct Item {
            pro::FilterValue value;
            const char* label;
            int destination;
        };
        const std::array<Item, 4> items { { { pro::unitCutoff, L("Coupure", "Cutoff"), pro::destinationCutoff1 + unit * 2 },
                                            { pro::unitResonance, L("Résonance", "Resonance"), pro::destinationResonance1 + unit * 2 },
                                            { pro::unitDrive, L("Saturation", "Drive"),
                                              unit == 0 ? pro::destinationDrive1 : unit == 1 ? pro::destinationDrive2 : -1 },
                                            { pro::unitSpread, L("Écart stéréo", "Spread"), -1 } } };
        for (std::size_t index = 0; index < items.size(); ++index) {
            const ImVec2 origin(inner2.min.x + cell * static_cast<float>(index % 2),
                                knobsTop + (knobHeight(small) + dp(4.0f)) * static_cast<float>(index / 2));
            changed |= knobCell(origin, cell, at(parameters, pane, pro::filterIndex(unit, items[index].value)), items[index].label,
                                unitColour, live, set, small);
            if (items[index].destination >= 0)
                changed |= dropOnto(parameters, pane, items[index].destination, set);
        }
        ImGui::PopID();
    }

    // The modulation: routing, mix and output; two LFOs and the follower; the
    // matrix.
    const auto left = position.x + stripsWidth + dp(8.0f);
    const auto width = position.x + size.x - left;
    auto y = top;
    text(list, ImVec2(left, y + dp(3.0f)), p.textDim, L("Routage", "Routing"), 0.66f);
    changed |= segmented("##routing", at(parameters, pane, pro::filterRouting),
                         { L("Série", "Serial"), L("Parallèle", "Parallel"), L("Paires", "Pairs") }, ImVec2(left + dp(58.0f), y),
                         std::min(dp(230.0f), width - dp(58.0f)), dp(20.0f), colour, set,
                         L("Série : l'un après l'autre. Parallèle : chacun sur le son d'origine, mélangés. Paires : 1 puis 2, à côté de 3 puis 4.",
                           "Serial: one after another. Parallel: each on the original, mixed. Pairs: 1 then 2, beside 3 then 4."));
    const auto blocks = 3;
    const auto block = width / static_cast<float>(blocks);
    y += dp(26.0f);
    const auto now = ImGui::GetTime();
    for (int index = 0; index < 2; ++index) {
        const auto base = index == 0 ? pro::filterLfo1Rate : pro::filterLfo2Rate;
        const auto x = left + block * static_cast<float>(index);
        ImGui::PushID(100 + index);
        const auto lfoColour = index == 0 ? IM_COL32(186, 140, 255, 255) : IM_COL32(110, 200, 255, 255);
        sourceChip(index == 0 ? "LFO 1" : "LFO 2", index == 0 ? pro::sourceLfo1 : pro::sourceLfo2, ImVec2(x, y), ImVec2(dp(54.0f), dp(18.0f)),
                   lfoColour);
        const auto shape = static_cast<int>(std::lround(get(parameters, pane, base + 1, 0.0)));
        const auto* division = engine::noteDivisionFor(static_cast<float>(get(parameters, pane, base + 2, 0.0)));
        const auto hz = division != nullptr ? std::max(20.0, live.tempo) / 60.0 / division->beats : get(parameters, pane, base, 1.0);
        lfoShapeGlyph(list, Box { ImVec2(x + dp(60.0f), y), ImVec2(x + block - dp(6.0f), y + dp(18.0f)) }, shape, lfoColour, now * hz);
        changed |= segmented("##shape", at(parameters, pane, base + 1), { "~", "^", "/", "\\", "[]", "SH", "rnd" },
                             ImVec2(x, y + dp(22.0f)), block - dp(6.0f), dp(18.0f), lfoColour, set,
                             L("La forme : sinus, triangle, dents de scie, carré, aléatoire tenu, aléatoire lissé",
                               "The shape: sine, triangle, saws, square, sample and hold, smooth random"));
        const auto cell = (block - dp(6.0f)) / 3.0f;
        changed |= knobCell(ImVec2(x, y + dp(44.0f)), cell, at(parameters, pane, base), L("Vitesse", "Rate"), lfoColour, live, set, small);
        changed |= knobCell(ImVec2(x + cell, y + dp(44.0f)), cell, at(parameters, pane, base + 2), L("Synchro", "Sync"), lfoColour, live,
                            set, small);
        changed |= knobCell(ImVec2(x + cell * 2.0f, y + dp(44.0f)), cell, at(parameters, pane, base + 3), L("Stéréo", "Stereo"), lfoColour,
                            live, set, small);
        ImGui::PopID();
    }
    {
        const auto x = left + block * 2.0f;
        ImGui::PushID(200);
        const auto followColour = IM_COL32(255, 210, 90, 255);
        sourceChip(L("Suiveur", "Follower"), pro::sourceFollower, ImVec2(x, y), ImVec2(dp(64.0f), dp(18.0f)), followColour);
        const auto level = std::clamp((static_cast<double>(live.inputDb) + get(parameters, pane, pro::filterFollowGain, 0.0) + 60.0) / 60.0,
                                      0.0, 1.0);
        const Box meter { ImVec2(x + dp(70.0f), y + dp(5.0f)), ImVec2(x + block - dp(6.0f), y + dp(13.0f)) };
        list->AddRectFilled(meter.min, meter.max, p.bg3, dp(2.0f));
        list->AddRectFilled(meter.min, ImVec2(meter.min.x + meter.width() * static_cast<float>(level), meter.max.y), followColour, dp(2.0f));
        const auto cell = (block - dp(6.0f)) / 3.0f;
        changed |= knobCell(ImVec2(x, y + dp(44.0f)), cell, at(parameters, pane, pro::filterFollowAttack), L("Attaque", "Attack"),
                            followColour, live, set, small);
        changed |= knobCell(ImVec2(x + cell, y + dp(44.0f)), cell, at(parameters, pane, pro::filterFollowRelease), L("Relâch.", "Release"),
                            followColour, live, set, small);
        changed |= knobCell(ImVec2(x + cell * 2.0f, y + dp(44.0f)), cell, at(parameters, pane, pro::filterFollowGain), L("Gain", "Gain"),
                            followColour, live, set, small);
        ImGui::PopID();
    }
    y += dp(44.0f) + knobHeight(small) + dp(6.0f);

    // The matrix.
    section(list, ImVec2(left, y), L("MODULATIONS  ·  source, cible, quantité", "MODULATIONS  ·  source, target, amount"));
    y += dp(13.0f);
    std::vector<juce::String> sources { ui::Ls("—", "—"), "LFO 1", "LFO 2", ui::Ls("Suiveur", "Follower") };
    std::vector<juce::String> destinations;
    for (int destination = 0; destination < pro::modulationDestinationCount; ++destination)
        destinations.push_back(studioFormat(Param { "filtpro.m1Destination", {}, {}, 0.0, 13.0, static_cast<double>(destination), 0.0 },
                                            destination));
    const auto rowHeight = dp(21.0f);
    const auto rowWidth = width - dp(150.0f);
    for (int slot = 0; slot < pro::filterMatrixSlots; ++slot) {
        ImGui::PushID(300 + slot);
        const auto rowY = y + (rowHeight + dp(3.0f)) * static_cast<float>(slot);
        changed |= comboCell("##source", at(parameters, pane, pro::matrixIndex(slot, pro::matrixSource)), sources, ImVec2(left, rowY),
                             dp(90.0f), set);
        changed |= comboCell("##destination", at(parameters, pane, pro::matrixIndex(slot, pro::matrixDestination)), destinations,
                             ImVec2(left + dp(94.0f), rowY), std::max(dp(90.0f), rowWidth - dp(94.0f)), set);
        if (auto* amount = at(parameters, pane, pro::matrixIndex(slot, pro::matrixAmount))) {
            ImGui::SetCursorScreenPos(ImVec2(left + std::max(dp(188.0f), rowWidth + dp(4.0f)), rowY));
            ImGui::SetNextItemWidth(dp(140.0f));
            auto value = static_cast<float>(amount->value * 100.0);
            ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
            if (ImGui::SliderFloat("##amount", &value, -100.0f, 100.0f, "%+.0f %%"))
                changed |= apply(amount, value / 100.0, set);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                changed |= apply(amount, 0.0, set);
            ImGui::PopFont();
        }
        ImGui::PopID();
    }
    // Mix and output, on the routing's row.
    const auto fieldWidth = std::min(dp(96.0f), (position.x + size.x - (left + dp(296.0f))) * 0.5f - dp(4.0f));
    if (fieldWidth > dp(50.0f)) {
        if (auto* mix = at(parameters, pane, pro::filterMix)) {
            ImGui::SetCursorScreenPos(ImVec2(position.x + size.x - fieldWidth * 2.0f - dp(6.0f), top - dp(4.0f)));
            ImGui::PushID("mix");
            changed |= valueField("##mix", *mix, fieldWidth, colour, set);
            changed |= dropOnto(parameters, pane, pro::destinationMix, set);
            ImGui::PopID();
        }
        if (auto* output = at(parameters, pane, pro::filterOutput)) {
            ImGui::SetCursorScreenPos(ImVec2(position.x + size.x - fieldWidth, top - dp(4.0f)));
            ImGui::PushID("output");
            changed |= valueField("##output", *output, fieldWidth, colour, set);
            changed |= dropOnto(parameters, pane, pro::destinationOutput, set);
            ImGui::PopID();
        }
    }
    return changed;
}


// ---- Ampli, Baffle et micro, Micro ---------------------------------------------------------------

namespace ampd = engine::amp;

constexpr ImU32 kMicA = IM_COL32(255, 196, 80, 255);
constexpr ImU32 kMicB = IM_COL32(110, 180, 255, 255);
// The amp's channel B, against its own colour for A.
constexpr ImU32 kChannelB = IM_COL32(255, 128, 88, 255);
constexpr double kCurveRate = 96'000.0;

// An effect's values from its pane (its parameters are its catalogue's, in order).
template <std::size_t N>
std::array<float, N> studioValues(std::vector<Param>& parameters, const Pane& pane, const char* typeKey)
{
    std::array<float, N> values {};
    const auto* effect = engine::effectForType(typeKey);
    for (std::size_t i = 0; i < N; ++i) {
        const auto fallback = effect != nullptr && i < effect->parameters.size() ? effect->parameters[i].defaultValue : 0.0f;
        values[i] = static_cast<float>(get(parameters, pane, static_cast<int>(i), fallback));
    }
    return values;
}

int whole(const float value, const int count)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, count - 1);
}

juce::String ampName(const int index)
{
    const auto& model = ampd::ampModel(index);
    return juce::String::fromUTF8(ui::french() ? model.french : model.english);
}
juce::String ampHint(const int index)
{
    const auto& model = ampd::ampModel(index);
    return juce::String::fromUTF8(ui::french() ? model.frenchHint : model.englishHint);
}
juce::String cabName(const int index)
{
    const auto& model = ampd::cabModel(index);
    return juce::String::fromUTF8(ui::french() ? model.french : model.english);
}
juce::String cabHint(const int index)
{
    const auto& model = ampd::cabModel(index);
    return juce::String::fromUTF8(ui::french() ? model.frenchHint : model.englishHint);
}
juce::String micName(const int index)
{
    const auto& model = ampd::micModel(index);
    return juce::String::fromUTF8(ui::french() ? model.french : model.english);
}
juce::String micHint(const int index)
{
    const auto& model = ampd::micModel(index);
    return juce::String::fromUTF8(ui::french() ? model.frenchHint : model.englishHint);
}

std::vector<juce::String> names(const int count, juce::String (*name)(int))
{
    std::vector<juce::String> list;
    for (int index = 0; index < count; ++index)
        list.push_back(name(index));
    return list;
}

// A name to click (a model's): a list to choose from, each with its hint.
bool chooser(const char* id, Param* parameter, const int count, juce::String (*name)(int), juce::String (*hint)(int), const ImVec2 at,
             const ImU32 colour, const Setter& set, const float scale = 0.74f)
{
    if (parameter == nullptr)
        return false;
    auto* list = ImGui::GetWindowDrawList();
    const auto current = whole(static_cast<float>(parameter->value - parameter->minimum), count);
    // Its name and a small arrow, drawn (not every font has the glyph).
    const auto label = name(current);
    const auto size = textSize(label.toRawUTF8(), scale);
    const auto arrow = size.y * 0.5f;
    const ImVec2 box(size.x + arrow + dp(12.0f), size.y + dp(4.0f));
    ImGui::PushID(id);
    ImGui::SetCursorScreenPos(at);
    const auto clicked = ImGui::InvisibleButton("##chooser", box);
    const auto hovered = ImGui::IsItemHovered();
    list->AddRectFilled(at, ImVec2(at.x + box.x, at.y + box.y), theme::alpha(colour, hovered ? 0.35f : 0.18f), dp(3.0f));
    text(list, ImVec2(at.x + dp(4.0f), at.y + dp(2.0f)), theme::palette().text, label.toRawUTF8(), scale);
    const ImVec2 tip(at.x + size.x + dp(8.0f) + arrow * 0.5f, at.y + box.y * 0.5f + arrow * 0.3f);
    list->AddTriangleFilled(ImVec2(tip.x - arrow * 0.5f, tip.y - arrow * 0.55f), ImVec2(tip.x + arrow * 0.5f, tip.y - arrow * 0.55f), tip,
                            theme::palette().textDim);
    if (hovered)
        ImGui::SetTooltip("%s", (hint(current) + "\n" + ui::Ls("Clic : changer", "Click: change")).toRawUTF8());
    if (clicked)
        ImGui::OpenPopup("##choices");
    auto changed = false;
    if (ImGui::BeginPopup("##choices")) {
        for (int index = 0; index < count; ++index) {
            if (ImGui::Selectable(name(index).toRawUTF8(), index == current))
                changed |= apply(parameter, parameter->minimum + index, set);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", hint(index).toRawUTF8());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

// A response over the box, `range` dB up and down from its middle.
std::vector<ImVec2> curveOver(const Box& box, const double range, const std::function<double(double)>& db)
{
    std::vector<ImVec2> points;
    const auto step = dp(2.0f);
    for (auto x = box.min.x; x <= box.max.x; x += step) {
        const auto value = std::clamp(db(hzOfX(box, x)), -range * 1.2, range * 1.2);
        points.emplace_back(x, (box.min.y + box.max.y) * 0.5f - static_cast<float>(value / range) * box.height() * 0.5f);
    }
    return points;
}

void levelGrid(ImDrawList* list, const Box& box, const double range, const bool labels)
{
    const auto& p = theme::palette();
    const auto middle = (box.min.y + box.max.y) * 0.5f;
    for (const auto db : { -12.0, 0.0, 12.0 }) {
        if (std::abs(db) > range)
            continue;
        const auto y = middle - static_cast<float>(db / range) * box.height() * 0.5f;
        list->AddLine(ImVec2(box.min.x, y), ImVec2(box.max.x, y), db == 0.0 ? p.gridStrong : p.grid);
        if (labels) {
            char label[12];
            std::snprintf(label, sizeof(label), "%+.0f", db);
            text(list, ImVec2(box.min.x + dp(3.0f), y - ImGui::GetFontSize() * 0.66f), p.textFaint, label, 0.58f);
        }
    }
}

// The distance (cm) as a place along a line, 1 cm to `far`, logarithmic.
float distancePlace(const double centimetres, const double far)
{
    return static_cast<float>(std::log(std::clamp(centimetres, 1.0, far)) / std::log(far));
}
double distanceAt(const float place, const double far)
{
    return std::pow(far, std::clamp(static_cast<double>(place), 0.0, 1.0));
}

// ---- Ampli ---

bool ampDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                const Setter& set, const bool large)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    // The channel chosen: its model and its knobs are what is drawn.
    const auto all = studioValues<ampd::ampParameterCount>(parameters, pane, "amp-sim");
    const auto channel = whole(all[ampd::ampChannel], 2);
    const auto values = ampd::ampChannelValues(all, channel);
    const auto modelIndex = whole(values[ampd::ampModelValue], ampd::ampModelCount);
    const auto& model = ampd::ampModel(modelIndex);
    const auto plot = Box { ImVec2(box.min.x, box.min.y + dp(large ? 24.0f : 18.0f)), box.max };
    frequencyGrid(list, plot, large);
    constexpr auto range = 24.0;
    levelGrid(list, plot, range, large);
    // The tone: what the knobs make of the spectrum around the valves.
    const auto points = curveOver(plot, range, [&](const double hz) { return ampd::ampToneDb(values, hz, kCurveRate); });
    fillUnder(list, points, (plot.min.y + plot.max.y) * 0.5f, theme::alpha(colour, 0.16f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(large ? 2.2f : 1.6f));
    // The valves' curve: what the gain does to the sound's shape.
    const auto side = std::min(plot.height() * 0.62f, dp(large ? 150.0f : 70.0f));
    const Box inset { ImVec2(plot.max.x - side - dp(8.0f), plot.max.y - side - dp(8.0f)), ImVec2(plot.max.x - dp(8.0f), plot.max.y - dp(8.0f)) };
    list->AddRectFilled(inset.min, inset.max, theme::alpha(kDark, 0.85f), dp(3.0f));
    list->AddRect(inset.min, inset.max, theme::alpha(p.line, 0.8f), dp(3.0f));
    dashedLine(list, ImVec2(inset.min.x, inset.max.y), ImVec2(inset.max.x, inset.min.y), theme::alpha(p.textFaint, 0.6f));
    std::vector<ImVec2> transfer;
    auto peak = 0.01;
    // Over a guitar's swing (to -6 dBFS).
    for (int i = -40; i <= 40; ++i)
        peak = std::max(peak, std::abs(ampd::preampCurve(model, values[ampd::ampGain], i / 80.0)));
    for (int i = -40; i <= 40; ++i) {
        const auto y = ampd::preampCurve(model, values[ampd::ampGain], i / 80.0) / peak;
        transfer.emplace_back(inset.min.x + (i + 40) / 80.0f * inset.width(),
                              (inset.min.y + inset.max.y) * 0.5f - static_cast<float>(y) * inset.height() * 0.45f);
    }
    list->AddPolyline(transfer.data(), static_cast<int>(transfer.size()), IM_COL32(255, 120, 90, 255), dp(1.6f));
    text(list, ImVec2(inset.min.x + dp(3.0f), inset.min.y + dp(2.0f)), p.textFaint, L("lampes", "valves"), 0.56f);
    // Its level, in and out, when it plays.
    if (live.running && live.inputDb > -100.0f) {
        char label[48];
        std::snprintf(label, sizeof(label), "%s %.0f  %s %.0f dB", L("entrée", "in"), live.inputDb, L("sortie", "out"), live.outputDb);
        text(list, ImVec2(plot.min.x + dp(6.0f), plot.max.y - ImGui::GetFontSize() * (large ? 1.6f : 0.9f)), p.textDim, label, 0.6f);
    }
    // The channel's model, to choose, and the channel beside it.
    const auto chooserX = box.max.x - textSize((ampName(modelIndex) + "  ").toRawUTF8(), large ? 0.82f : 0.7f).x - dp(16.0f);
    auto changed = chooser("##ampModel", at(parameters, pane, ampd::ampChannelIndex(ampd::ampModelValue, channel)), ampd::ampModelCount,
                           ampName, ampHint, ImVec2(chooserX, box.min.y + dp(2.0f)), colour, set, large ? 0.82f : 0.7f);
    const auto abWidth = dp(large ? 56.0f : 42.0f);
    changed |= segmented("##ampChannel", at(parameters, pane, ampd::ampChannel), { "A", "B" },
                         ImVec2(chooserX - abWidth - dp(8.0f), box.min.y + dp(2.0f)), abWidth, dp(large ? 20.0f : 16.0f),
                         channel == 1 ? kChannelB : colour, set,
                         L("Le canal : deux amplis réglés chacun à sa façon (un clair, un saturé). On passe de l'un à l'autre sans clic ; "
                           "la pédale, le gate et la sortie sont communs aux deux.",
                           "The channel: two amps each set its own way (a clean, a lead). Switching does not click; the pedal, the gate "
                           "and the output are both channels'."));
    if (large)
        text(list, ImVec2(box.min.x + dp(6.0f), box.min.y + dp(22.0f)), p.textDim, ampHint(modelIndex).toRawUTF8(), 0.66f);
    return changed;
}

bool ampLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
              const Live& live, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto big = 46.0f;
    const auto small = 34.0f;
    const auto panelHeight = dp(30.0f) + knobHeight(big) + dp(26.0f) + knobHeight(small) + dp(30.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(170.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= ampDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("AMPLI  ·  tonalité et courbe des lampes", "AMP  ·  tone and valve curve"));
    list->PopClipRect();

    // The faceplate.
    auto y = outer.max.y + dp(8.0f);
    const Box plate { ImVec2(position.x, y), ImVec2(position.x + size.x, position.y + size.y) };
    list->AddRectFilled(plate.min, plate.max, theme::mix(colour, p.bg1, 0.86f), dp(6.0f));
    list->AddRect(plate.min, plate.max, theme::alpha(colour, 0.5f), dp(6.0f));
    y += dp(6.0f);
    // Channel A or B: the faceplate shows the one chosen, in its colour.
    const auto channel = whole(static_cast<float>(get(parameters, pane, ampd::ampChannel, 0.0)), 2);
    const auto mine = [&](const int index) { return at(parameters, pane, ampd::ampChannelIndex(index, channel)); };
    const auto tint = channel == 1 ? kChannelB : colour;
    const auto abWidth = dp(76.0f);
    changed |= segmented("##channel", at(parameters, pane, ampd::ampChannel), { L("Canal A", "Ch. A"), L("Canal B", "Ch. B") },
                         ImVec2(position.x + dp(8.0f), y), abWidth * 2.0f, dp(22.0f), tint, set,
                         L("Deux canaux, deux amplis : un modèle et des potards chacun. On passe de l'un à l'autre en 20 ms, sans clic "
                           "(automatisable : couplet clair, refrain saturé).",
                           "Two channels, two amps: a model and knobs each. Switching takes 20 ms, without a click (automate it: a clean "
                           "verse, a lead chorus)."));
    changed |= segmented("##models", mine(ampd::ampModelValue),
                         { "Clean US", "Crunch UK", "Lead UK", L("Moderne", "Modern"), "Boutique", "Chime", L("Basse vint.", "Vint. bass"),
                           L("Basse mod.", "Mod. bass") },
                         ImVec2(position.x + dp(16.0f) + abWidth * 2.0f, y), size.x - dp(24.0f) - abWidth * 2.0f, dp(22.0f), tint, set,
                         L("Le modèle du canal : ses lampes, son tone stack, son étage de puissance.",
                           "The channel's model: its valves, its tone stack, its power stage."));
    y += dp(30.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 7> knobs { { { ampd::ampGain, L("GAIN", "GAIN") },
                                        { ampd::ampBass, L("BASSES", "BASS") },
                                        { ampd::ampMiddle, L("MÉDIUMS", "MIDDLE") },
                                        { ampd::ampTreble, L("AIGUS", "TREBLE") },
                                        { ampd::ampPresence, L("PRÉSENCE", "PRESENCE") },
                                        { ampd::ampDepth, L("PROFONDEUR", "DEPTH") },
                                        { ampd::ampMaster, "MASTER" } } };
    const auto brightWidth = dp(76.0f);
    auto cell = std::min(dp(110.0f), (size.x - dp(16.0f) - brightWidth) / static_cast<float>(knobs.size()));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(index), y), cell, mine(knobs[index].index),
                            knobs[index].label, tint, live, set, big);
    changed |= switchCell("Bright", mine(ampd::ampBright),
                          ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(knobs.size()) + dp(6.0f), y + dp(14.0f)),
                          ImVec2(brightWidth - dp(12.0f), dp(24.0f)), tint, set,
                          L("Relève les aigus avant les lampes, surtout à faible gain.", "Lifts the highs before the valves, most at low gain."));
    y += knobHeight(big) + dp(8.0f);
    section(list, ImVec2(position.x + dp(8.0f), y), L("PÉDALE", "PEDAL"));
    const auto pedalWidth = dp(250.0f);
    changed |= segmented("##pedal", at(parameters, pane, ampd::ampPedal),
                         { L("Aucune", "None"), "Boost", "Overdrive", "Fuzz" }, ImVec2(position.x + dp(56.0f), y - dp(4.0f)),
                         pedalWidth - dp(56.0f), dp(20.0f), IM_COL32(120, 210, 120, 255), set,
                         L("Une pédale avant l'ampli : boost propre, overdrive (bosse dans les médiums), fuzz.",
                           "A pedal before the amp: clean boost, overdrive (a mid hump), fuzz."));
    section(list, ImVec2(position.x + pedalWidth + dp(20.0f), y), L("RÉGLAGES", "SETTINGS"));
    changed |= segmented("##oversampling", at(parameters, pane, ampd::ampOversampling), { "1x", "2x", "4x" },
                         ImVec2(position.x + size.x - dp(150.0f), y - dp(4.0f)), dp(142.0f), dp(20.0f), colour, set,
                         L("Suréchantillonnage : moins de repliement dans les aigus saturés, plus de calcul.",
                           "Oversampling: less aliasing in saturated highs, more computing."));
    y += dp(20.0f);
    const std::array<Item, 3> pedal { { { ampd::ampPedalDrive, "Drive" },
                                        { ampd::ampPedalTone, L("Tonalité", "Tone") },
                                        { ampd::ampPedalLevel, L("Niveau", "Level") } } };
    cell = pedalWidth / 3.0f;
    for (std::size_t index = 0; index < pedal.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(8.0f) + cell * static_cast<float>(index), y), cell,
                            at(parameters, pane, pedal[index].index), pedal[index].label, IM_COL32(120, 210, 120, 255), live, set, small);
    const std::array<Item, 5> rest { { { ampd::ampGate, "Noise gate" },
                                       { ampd::ampSag, "Sag" },
                                       { ampd::ampTight, L("Serrage", "Tight") },
                                       { ampd::ampOutput, L("Sortie", "Output") },
                                       { ampd::ampMix, L("Mélange", "Mix") } } };
    cell = std::min(dp(96.0f), (size.x - pedalWidth - dp(24.0f)) / static_cast<float>(rest.size()));
    for (std::size_t index = 0; index < rest.size(); ++index)
        changed |= knobCell(ImVec2(position.x + pedalWidth + dp(16.0f) + cell * static_cast<float>(index), y), cell,
                            at(parameters, pane, rest[index].index), rest[index].label, colour, live, set, small);
    return changed;
}

// ---- Baffle et micro ---

// The speaker seen from the front: microphone A on its left half, B on its
// right, each where it points (the centre or the edge); drag them.
bool speakerFace(std::vector<Param>& parameters, const Pane& pane, const Box& box, const std::array<float, ampd::cabParameterCount>& values,
                 const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto& cab = ampd::cabModel(whole(values[ampd::cabModelValue], ampd::cabModelCount));
    const ImVec2 centre((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f);
    const auto radius = std::min(box.width(), box.height()) * 0.44f;
    list->AddRectFilled(box.min, box.max, IM_COL32(30, 28, 26, 255), dp(4.0f));
    // The grille cloth, then the speaker.
    for (auto x = box.min.x; x < box.max.x; x += dp(4.0f))
        list->AddLine(ImVec2(x, box.min.y), ImVec2(x, box.max.y), IM_COL32(44, 40, 36, 255));
    list->AddCircleFilled(centre, radius, IM_COL32(38, 36, 34, 255), 64);
    for (int ring = 1; ring <= 5; ++ring)
        list->AddCircle(centre, radius * (0.25f + ring * 0.15f), IM_COL32(60, 56, 52, 255), 64, dp(1.0f));
    list->AddCircleFilled(centre, radius * 0.22f, IM_COL32(70, 66, 60, 255), 48);
    list->AddCircle(centre, radius, IM_COL32(90, 84, 76, 255), 64, dp(2.0f));
    char label[32];
    std::snprintf(label, sizeof(label), "%d x %.0f\"", cab.speakers, cab.inches);
    text(list, ImVec2(box.min.x + dp(4.0f), box.max.y - ImGui::GetFontSize() * 0.75f), p.textFaint, label, 0.58f);
    const auto bOn = values[ampd::cabMicBOn] >= 0.5f;
    const auto reach = radius * 0.95f;
    const ImVec2 a(centre.x - values[ampd::cabPositionA] * reach, centre.y - radius * 0.12f);
    const ImVec2 b(centre.x + values[ampd::cabPositionB] * reach, centre.y + radius * 0.12f);
    // Drag: the nearest microphone, along its half.
    ImGui::SetCursorScreenPos(box.min);
    ImGui::InvisibleButton("##face", ImVec2(std::max(1.0f, box.width()), std::max(1.0f, box.height())));
    auto* storage = ImGui::GetStateStorage();
    const auto heldId = ImGui::GetID("##faceHeld");
    const auto mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemActivated()) {
        const auto toA = std::hypot(mouse.x - a.x, mouse.y - a.y);
        const auto toB = bOn ? std::hypot(mouse.x - b.x, mouse.y - b.y) : 1.0e9f;
        storage->SetInt(heldId, toA <= toB ? 1 : 2);
    }
    auto changed = false;
    const auto held = ImGui::IsItemActive() ? storage->GetInt(heldId, 0) : 0;
    if (held == 1)
        changed |= apply(at(parameters, pane, ampd::cabPositionA), std::clamp((centre.x - mouse.x) / reach, 0.0f, 1.0f), set);
    else if (held == 2)
        changed |= apply(at(parameters, pane, ampd::cabPositionB), std::clamp((mouse.x - centre.x) / reach, 0.0f, 1.0f), set);
    if (ImGui::IsItemHovered() && held == 0)
        ImGui::SetTooltip("%s", L("Glissez un micro : vers le centre (brillant) ou vers le bord (sombre).",
                                  "Drag a microphone: towards the centre (bright) or the edge (dark)."));
    if (bOn)
        handleDot(list, b, kMicB, held == 2, "B");
    handleDot(list, a, kMicA, held == 1, "A");
    return changed;
}

// The microphones from the side: how far from the grille (left, sideways),
// at what angle (drag up and down).
bool micSide(std::vector<Param>& parameters, const Pane& pane, const Box& box, const std::array<float, ampd::cabParameterCount>& values,
             const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    constexpr auto far = 100.0;
    list->AddRectFilled(box.min, box.max, theme::alpha(kDark, 0.6f), dp(4.0f));
    const auto grille = box.min.x + dp(10.0f);
    list->AddRectFilled(ImVec2(box.min.x, box.min.y), ImVec2(grille, box.max.y), IM_COL32(44, 40, 36, 255), dp(3.0f));
    const auto line = [&](const float place) { return grille + dp(8.0f) + place * (box.width() - dp(30.0f)); };
    for (const auto cm : { 1.0, 3.0, 10.0, 30.0, 100.0 }) {
        const auto x = line(distancePlace(cm, far));
        list->AddLine(ImVec2(x, box.min.y + dp(14.0f)), ImVec2(x, box.max.y), p.grid);
        char label[12];
        std::snprintf(label, sizeof(label), "%.0f cm", cm);
        text(list, ImVec2(x + dp(2.0f), box.min.y + dp(2.0f)), p.textFaint, label, 0.55f);
    }
    const auto bOn = values[ampd::cabMicBOn] >= 0.5f;
    const auto rowA = box.min.y + box.height() * 0.38f;
    const auto rowB = box.min.y + box.height() * 0.76f;
    const auto glyph = [&](const float distance, const float angle, const float row, const ImU32 colour, const char* name, const bool hot) {
        const ImVec2 capsule(line(distancePlace(distance, far)), row);
        const auto radians = angle * std::numbers::pi_v<float> / 180.0f;
        const ImVec2 tail(capsule.x + std::cos(radians) * dp(34.0f), capsule.y + std::sin(radians) * dp(34.0f));
        list->AddLine(capsule, tail, colour, dp(hot ? 6.0f : 5.0f));
        handleDot(list, capsule, colour, hot, name);
        char label[24];
        std::snprintf(label, sizeof(label), "%.0f cm  %.0f\xc2\xb0", distance, angle);
        text(list, ImVec2(capsule.x + dp(10.0f), row - ImGui::GetFontSize() * 1.1f), colour, label, 0.6f);
        return capsule;
    };
    ImGui::SetCursorScreenPos(box.min);
    ImGui::InvisibleButton("##side", ImVec2(std::max(1.0f, box.width()), std::max(1.0f, box.height())));
    auto* storage = ImGui::GetStateStorage();
    const auto heldId = ImGui::GetID("##sideHeld");
    const auto mouse = ImGui::GetIO().MousePos;
    const ImVec2 a(line(distancePlace(values[ampd::cabDistanceA], far)), rowA);
    const ImVec2 b(line(distancePlace(values[ampd::cabDistanceB], far)), rowB);
    if (ImGui::IsItemActivated()) {
        const auto toA = std::hypot(mouse.x - a.x, mouse.y - a.y);
        const auto toB = bOn ? std::hypot(mouse.x - b.x, mouse.y - b.y) : 1.0e9f;
        storage->SetInt(heldId, toA <= toB ? 1 : 2);
    }
    auto changed = false;
    const auto held = ImGui::IsItemActive() ? storage->GetInt(heldId, 0) : 0;
    if (held != 0) {
        const auto place = (mouse.x - grille - dp(8.0f)) / std::max(1.0f, box.width() - dp(30.0f));
        const auto row = held == 1 ? rowA : rowB;
        const auto angle = std::clamp((mouse.y - row) / (box.height() * 0.3f) * 90.0f, 0.0f, 90.0f);
        changed |= apply(at(parameters, pane, held == 1 ? ampd::cabDistanceA : ampd::cabDistanceB), distanceAt(place, far), set);
        changed |= apply(at(parameters, pane, held == 1 ? ampd::cabAngleA : ampd::cabAngleB), angle, set);
    }
    if (ImGui::IsItemHovered() && held == 0)
        ImGui::SetTooltip("%s", L("Glissez un micro : de côté pour sa distance, vers le bas pour l'incliner (hors axe).",
                                  "Drag a microphone: sideways for its distance, down to tilt it (off axis)."));
    if (bOn)
        glyph(values[ampd::cabDistanceB], values[ampd::cabAngleB], rowB, kMicB, "B", held == 2);
    glyph(values[ampd::cabDistanceA], values[ampd::cabAngleA], rowA, kMicA, "A", held == 1);
    return changed;
}

// A loaded response's curve read at a frequency.
double cardFileDb(const std::vector<float>& curve, const double hz)
{
    if (curve.empty())
        return 0.0;
    const auto t = std::log(std::clamp(hz, kLowHz, kHighHz) / kLowHz) / std::log(kHighHz / kLowHz) * static_cast<double>(curve.size() - 1);
    const auto index = std::min(static_cast<std::size_t>(t), curve.size() - 1);
    const auto next = std::min(index + 1, curve.size() - 1);
    const auto fraction = t - static_cast<double>(index);
    return curve[index] + (curve[next] - curve[index]) * fraction;
}

// The response's line: its name and a cross, or a button to load one.
void cardFileRow(const ImVec2 at, const bool large, const ImU32 colour)
{
    if (cardFile == nullptr)
        return;
    ImGui::SetCursorScreenPos(at);
    ImGui::PushFont(nullptr, ui::currentFontBase() * (large ? 0.86f : 0.74f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(dp(5.0f), dp(1.0f)));
    if (cardFile->name.isEmpty()) {
        if (ImGui::Button(L("IRâ¦##loadIr", "IRâ¦##loadIr")) && cardFile->load)
            cardFile->load();
        ui::tooltip(L("Charger une réponse impulsionnelle (.wav) : un baffle enregistré avec son micro, joué à la place des modèles",
                      "Load an impulse response (.wav): a cabinet recorded with its microphone, played instead of the models"));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::alpha(colour, 0.35f));
        if (ImGui::Button((juce::String("IR : ") + cardFile->name + "##changeIr").toRawUTF8()) && cardFile->load)
            cardFile->load();
        ImGui::PopStyleColor();
        ui::tooltip(L("Réponse impulsionnelle chargée : un clic en charge une autre", "Impulse response loaded: a click loads another"));
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ImGui::Button("Ã##clearIr") && cardFile->clear)
            cardFile->clear();
        ui::tooltip(L("Revenir aux baffles et micros modélisés", "Back to the modelled cabinets and microphones"));
    }
    ImGui::PopStyleVar();
    ImGui::PopFont();
}

bool cabDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                const Setter& set, const bool large)
{
    (void) live;
    const auto loaded = cardFile != nullptr && cardFile->name.isNotEmpty() && cardFile->curveDb != nullptr;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto values = studioValues<ampd::cabParameterCount>(parameters, pane, "cab-sim");
    const auto top = dp(large ? 24.0f : 18.0f);
    auto changed = false;
    auto curveLeft = 0.0f;
    ImGui::PushID("##cab");
    if (large) {
        // A column: the speaker from the front, the microphones from the side under it.
        const auto column = std::min(box.width() * 0.34f, dp(380.0f));
        const auto faceSide = std::min(column, (box.height() - top) * 0.58f);
        const Box face { ImVec2(box.min.x + dp(6.0f) + (column - faceSide) * 0.5f, box.min.y + top),
                         ImVec2(box.min.x + dp(6.0f) + (column + faceSide) * 0.5f, box.min.y + top + faceSide) };
        changed |= speakerFace(parameters, pane, face, values, set);
        const Box side { ImVec2(box.min.x + dp(6.0f), face.max.y + dp(6.0f)), ImVec2(box.min.x + dp(6.0f) + column, box.max.y - dp(6.0f)) };
        changed |= micSide(parameters, pane, side, values, set);
        curveLeft = box.min.x + column + dp(16.0f);
    } else {
        const auto faceSide = std::min(box.height() - top - dp(6.0f), box.width() * 0.34f);
        const Box face { ImVec2(box.min.x + dp(6.0f), box.min.y + top), ImVec2(box.min.x + dp(6.0f) + faceSide, box.min.y + top + faceSide) };
        changed |= speakerFace(parameters, pane, face, values, set);
        curveLeft = face.max.x + dp(8.0f);
    }
    ImGui::PopID();
    values = studioValues<ampd::cabParameterCount>(parameters, pane, "cab-sim");
    const Box plot { ImVec2(curveLeft, box.min.y + top), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    frequencyGrid(list, plot, large);
    constexpr auto range = 24.0;
    levelGrid(list, plot, range, large);
    const auto bOn = !loaded && values[ampd::cabMicBOn] >= 0.5f;
    if (loaded) {
        // The response's own curve, with the cuts.
        const auto& curve = *cardFile->curveDb;
        const auto points = curveOver(plot, range, [&](const double hz) { return cardFileDb(curve, hz); });
        fillUnder(list, points, (plot.min.y + plot.max.y) * 0.5f, theme::alpha(colour, 0.15f));
        list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(large ? 2.2f : 1.6f));
        cardFileRow(ImVec2(box.max.x - dp(large ? 230.0f : 150.0f), box.min.y + dp(2.0f)), large, colour);
        return changed;
    }
    if (bOn) {
        const auto pointsA = curveOver(plot, range, [&](const double hz) { return ampd::cabResponseDb(values, 0, hz, kCurveRate); });
        const auto pointsB = curveOver(plot, range, [&](const double hz) { return ampd::cabResponseDb(values, 1, hz, kCurveRate); });
        list->AddPolyline(pointsA.data(), static_cast<int>(pointsA.size()), theme::alpha(kMicA, 0.7f), dp(1.2f));
        list->AddPolyline(pointsB.data(), static_cast<int>(pointsB.size()), theme::alpha(kMicB, 0.7f), dp(1.2f));
    }
    const auto both = curveOver(plot, range, [&](const double hz) { return ampd::cabResponseDb(values, 2, hz, kCurveRate); });
    fillUnder(list, both, (plot.min.y + plot.max.y) * 0.5f, theme::alpha(colour, 0.15f));
    list->AddPolyline(both.data(), static_cast<int>(both.size()), bOn ? p.text : kMicA, dp(large ? 2.2f : 1.6f));
    // The cabinet and the microphones, to choose.
    const auto cabIndex = whole(values[ampd::cabModelValue], ampd::cabModelCount);
    const auto scale = large ? 0.8f : 0.68f;
    auto x = box.max.x - textSize((cabName(cabIndex) + "  ").toRawUTF8(), scale).x - dp(16.0f);
    changed |= chooser("##cabModel", at(parameters, pane, ampd::cabModelValue), ampd::cabModelCount, cabName, cabHint,
                       ImVec2(x, box.min.y + dp(2.0f)), colour, set, scale);
    if (large) {
        auto micX = box.min.x + dp(170.0f);
        changed |= chooser("##micA", at(parameters, pane, ampd::cabMicA), ampd::cabMicCount, micName, micHint,
                           ImVec2(micX, box.min.y + dp(2.0f)), kMicA, set, 0.74f);
        micX += textSize((micName(whole(values[ampd::cabMicA], ampd::cabMicCount)) + "    ").toRawUTF8(), 0.74f).x + dp(18.0f);
        if (bOn) {
            changed |= chooser("##micB", at(parameters, pane, ampd::cabMicB), ampd::cabMicCount, micName, micHint,
                               ImVec2(micX, box.min.y + dp(2.0f)), kMicB, set, 0.74f);
            micX += textSize((micName(whole(values[ampd::cabMicB], ampd::cabMicCount)) + "    ").toRawUTF8(), 0.74f).x + dp(18.0f);
        }
        cardFileRow(ImVec2(micX, box.min.y + dp(1.0f)), true, colour);
    } else {
        cardFileRow(ImVec2(box.min.x + dp(4.0f), box.max.y - dp(20.0f)), false, colour);
    }
    return changed;
}

bool cabLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
              const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 34.0f;
    constexpr std::size_t knobCount = 15;
    const auto columns = knobColumns(size.x - dp(8.0f), knobCount, dp(72.0f));
    const auto rows = (knobCount + columns - 1) / columns;
    const auto panelHeight = dp(34.0f) + knobHeight(diameter) * static_cast<float>(rows) + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= cabDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("BAFFLE ET MICRO", "CABINET AND MIC"));
    list->PopClipRect();

    auto y = outer.max.y + dp(10.0f);
    const auto rowHeight = dp(22.0f);
    auto x = position.x + dp(4.0f);
    const auto option = [&](const char* label, const int index, const float width, const char* tip, const ImU32 tint) {
        changed |= switchCell(label, at(parameters, pane, index), ImVec2(x, y), ImVec2(width, rowHeight), tint, set, tip);
        x += width + dp(6.0f);
    };
    option(L("Micro B", "Mic B"), ampd::cabMicBOn, dp(84.0f), L("Un deuxième micro sur le haut-parleur.", "A second microphone on the speaker."),
           kMicB);
    option(L("Phase B inversée", "Flip B"), ampd::cabPolarityB, dp(120.0f),
           L("Inverse la polarité du micro B : creuse ou remplit le grave du mélange.",
             "Flips mic B's polarity: hollows or fills the blend's lows."),
           kMicB);
    option(L("Aligner", "Align"), ampd::cabAlign, dp(78.0f),
           L("Aligne les deux micros dans le temps. Coupé, leurs distances différentes creusent des peignes, comme en vrai.",
             "Lines the two microphones up in time. Off, their different distances comb, as they really do."),
           colour);
    option(L("A gauche, B à droite", "A left, B right"), ampd::cabStereo, dp(150.0f),
           L("Un micro par côté au lieu de leur mélange.", "One microphone each side instead of their blend."), colour);
    y += rowHeight + dp(10.0f);
    struct Item {
        int index;
        const char* label;
        ImU32 tint;
    };
    const std::array<Item, knobCount> knobs { { { ampd::cabPositionA, L("Position A", "Position A"), kMicA },
                                         { ampd::cabDistanceA, L("Distance A", "Distance A"), kMicA },
                                         { ampd::cabAngleA, L("Angle A", "Angle A"), kMicA },
                                         { ampd::cabPositionB, L("Position B", "Position B"), kMicB },
                                         { ampd::cabDistanceB, L("Distance B", "Distance B"), kMicB },
                                         { ampd::cabAngleB, L("Angle B", "Angle B"), kMicB },
                                         { ampd::cabBlend, L("Mélange A/B", "A/B blend"), colour },
                                         { ampd::cabRoom, L("Pièce", "Room"), colour },
                                         { ampd::cabRoomSize, L("Taille pièce", "Room size"), colour },
                                         { ampd::cabRoomTone, L("Ton pièce", "Room tone"), colour },
                                         { ampd::cabResonance, L("Résonance", "Resonance"), colour },
                                         { ampd::cabLowCut, L("Coupe-bas", "Low cut"), colour },
                                         { ampd::cabHighCut, L("Coupe-haut", "High cut"), colour },
                                         { ampd::cabOutput, L("Sortie", "Output"), colour },
                                         { ampd::cabMix, L("Mélange", "Mix"), colour } } };
    const auto cell = std::min(dp(92.0f), (size.x - dp(8.0f)) / static_cast<float>(columns));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index % columns),
                                   y + knobHeight(diameter) * static_cast<float>(index / columns)),
                            cell, at(parameters, pane, knobs[index].index), knobs[index].label, knobs[index].tint, live, set, diameter);
    return changed;
}

// ---- Micro ---

// The microphone from above, pointing up: its pattern, and the source
// where it is (angle and distance); drag the source.
bool polarView(std::vector<Param>& parameters, const Pane& pane, const Box& box, const std::array<float, ampd::micParameterCount>& values,
               const ImU32 colour, const Setter& set)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    constexpr auto far = 200.0;
    const ImVec2 centre((box.min.x + box.max.x) * 0.5f, (box.min.y + box.max.y) * 0.5f + dp(4.0f));
    const auto radius = std::min(box.width(), box.height()) * 0.44f;
    list->AddRectFilled(box.min, box.max, theme::alpha(kDark, 0.6f), dp(4.0f));
    for (const auto cm : { 3.0, 10.0, 30.0, 100.0 }) {
        const auto r = radius * distancePlace(cm, far);
        list->AddCircle(centre, r, p.grid, 48, dp(1.0f));
        char label[12];
        std::snprintf(label, sizeof(label), "%.0f", cm);
        text(list, ImVec2(centre.x + dp(2.0f), centre.y - r - ImGui::GetFontSize() * 0.6f), p.textFaint, label, 0.52f);
    }
    // The pattern: what it picks up all round.
    const auto pattern = ampd::micPatternOf(values);
    std::vector<ImVec2> shape;
    for (int step = 0; step <= 72; ++step) {
        const auto degrees = step * 5.0;
        const auto folded = degrees <= 180.0 ? degrees : 360.0 - degrees;
        const auto r = radius * static_cast<float>(ampd::patternGain(pattern, folded));
        const auto radians = static_cast<float>(degrees * std::numbers::pi / 180.0);
        shape.emplace_back(centre.x + std::sin(radians) * r, centre.y - std::cos(radians) * r);
    }
    {
        // A fan from the centre: the pattern is seen whole from there.
        const auto flags = list->Flags;
        list->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (std::size_t i = 0; i + 1 < shape.size(); ++i)
            list->AddTriangleFilled(centre, shape[i], shape[i + 1], theme::alpha(colour, 0.14f));
        list->Flags = flags;
    }
    list->AddPolyline(shape.data(), static_cast<int>(shape.size()), colour, dp(1.6f));
    // The microphone, pointing up.
    list->AddRectFilled(ImVec2(centre.x - dp(4.0f), centre.y - dp(8.0f)), ImVec2(centre.x + dp(4.0f), centre.y + dp(14.0f)), p.textDim,
                        dp(3.0f));
    // The source.
    const auto angle = static_cast<float>(values[ampd::micAngle]) * std::numbers::pi_v<float> / 180.0f;
    const auto r = radius * distancePlace(values[ampd::micDistance], far);
    const ImVec2 source(centre.x + std::sin(angle) * r, centre.y - std::cos(angle) * r);
    ImGui::SetCursorScreenPos(box.min);
    ImGui::InvisibleButton("##polar", ImVec2(std::max(1.0f, box.width()), std::max(1.0f, box.height())));
    const auto active = ImGui::IsItemActive();
    auto changed = false;
    if (active) {
        const auto mouse = ImGui::GetIO().MousePos;
        const auto dx = std::abs(mouse.x - centre.x);
        const auto dy = mouse.y - centre.y;
        const auto degrees = std::atan2(dx, -dy) * 180.0f / std::numbers::pi_v<float>;
        changed |= apply(at(parameters, pane, ampd::micAngle), std::clamp(degrees, 0.0f, 180.0f), set);
        changed |= apply(at(parameters, pane, ampd::micDistance), distanceAt(std::hypot(dx, dy) / radius, far), set);
    }
    if (ImGui::IsItemHovered() && !active)
        ImGui::SetTooltip("%s", L("Glissez la source : sa distance au micro (cm) et son angle (devant en haut).",
                                  "Drag the source: its distance from the microphone (cm) and its angle (front at the top)."));
    handleDot(list, source, IM_COL32(255, 210, 120, 255), active, "\xe2\x99\xaa");
    char label[32];
    std::snprintf(label, sizeof(label), "%.0f cm  %.0f\xc2\xb0", values[ampd::micDistance], values[ampd::micAngle]);
    text(list, ImVec2(box.min.x + dp(4.0f), box.max.y - ImGui::GetFontSize() * 0.75f), p.textDim, label, 0.58f);
    return changed;
}

bool micDisplay(std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour, const Live& live,
                const Setter& set, const bool large)
{
    (void) live;
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto values = studioValues<ampd::micParameterCount>(parameters, pane, "mic-sim");
    const auto top = dp(large ? 24.0f : 18.0f);
    const auto side = std::min(box.height() - top - dp(6.0f), box.width() * (large ? 0.3f : 0.36f));
    const Box polar { ImVec2(box.min.x + dp(6.0f), box.min.y + top), ImVec2(box.min.x + dp(6.0f) + side, box.min.y + top + side) };
    ImGui::PushID("##mic");
    auto changed = polarView(parameters, pane, polar, values, colour, set);
    ImGui::PopID();
    values = studioValues<ampd::micParameterCount>(parameters, pane, "mic-sim");
    const Box plot { ImVec2(polar.max.x + dp(8.0f), box.min.y + top), ImVec2(box.max.x - dp(4.0f), box.max.y - dp(4.0f)) };
    frequencyGrid(list, plot, large);
    constexpr auto range = 24.0;
    levelGrid(list, plot, range, large);
    const auto points = curveOver(plot, range, [&](const double hz) { return ampd::micResponseDb(values, hz, kCurveRate); });
    fillUnder(list, points, (plot.min.y + plot.max.y) * 0.5f, theme::alpha(colour, 0.15f));
    list->AddPolyline(points.data(), static_cast<int>(points.size()), colour, dp(large ? 2.2f : 1.6f));
    const auto modelIndex = whole(values[ampd::micModelValue], ampd::micModelCount);
    const auto scale = large ? 0.8f : 0.68f;
    changed |= chooser("##micModel", at(parameters, pane, ampd::micModelValue), ampd::micModelCount, micName, micHint,
                       ImVec2(box.max.x - textSize((micName(modelIndex) + "  ").toRawUTF8(), scale).x - dp(16.0f), box.min.y + dp(2.0f)),
                       colour, set, scale);
    if (large)
        text(list, ImVec2(plot.min.x + dp(4.0f), plot.min.y + dp(2.0f)), p.textDim, micHint(modelIndex).toRawUTF8(), 0.64f);
    return changed;
}

bool micLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
              const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto diameter = 34.0f;
    constexpr std::size_t knobCount = 11;
    const auto columns = knobColumns(size.x - dp(8.0f), knobCount, dp(72.0f));
    const auto rows = (knobCount + columns - 1) / columns;
    const auto panelHeight = dp(34.0f) + knobHeight(diameter) * static_cast<float>(rows) + dp(16.0f);
    const Box outer { position, ImVec2(position.x + size.x, position.y + std::max(dp(190.0f), size.y - panelHeight - dp(8.0f))) };
    const auto inner = frame(list, outer);
    list->PushClipRect(inner.min, inner.max, true);
    changed |= micDisplay(parameters, pane, inner, colour, live, set, true);
    title(list, inner, L("MICRO  ·  directivité et réponse", "MICROPHONE  ·  pattern and response"));
    list->PopClipRect();

    auto y = outer.max.y + dp(10.0f);
    section(list, ImVec2(position.x + dp(4.0f), y + dp(4.0f)), L("DIRECTIVITÉ", "PATTERN"));
    changed |= segmented("##pattern", at(parameters, pane, ampd::micPattern),
                         { L("Du micro", "Its own"), "Omni", L("Cardio", "Cardioid"), "Hyper", L("Figure 8", "Figure 8") },
                         ImVec2(position.x + dp(84.0f), y), size.x * 0.48f - dp(84.0f), dp(22.0f), colour, set,
                         L("La directivité : omni n'a pas d'effet de proximité ; cardio rejette l'arrière ; la figure 8 entend devant et "
                           "derrière.",
                           "The pattern: omni has no proximity effect; cardioid rejects the back; figure 8 hears front and back."));
    const auto preampX = position.x + size.x * 0.5f;
    section(list, ImVec2(preampX, y + dp(4.0f)), L("PRÉAMPLI", "PREAMP"));
    changed |= segmented("##preamp", at(parameters, pane, ampd::micPreamp), { "Transistor", L("Lampe", "Valve"), L("Transfo", "Transformer") },
                         ImVec2(preampX + dp(70.0f), y), size.x * 0.5f - dp(74.0f), dp(22.0f), colour, set,
                         L("Ce que le préampli fait quand on le pousse (Drive) : transistor net, lampe chaude, transfo qui sature le grave.",
                           "What the preamp does when pushed (Drive): clean transistor, warm valve, a transformer saturating the lows."));
    y += dp(32.0f);
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, knobCount> knobs { { { ampd::micDistance, L("Distance", "Distance") },
                                        { ampd::micAngle, L("Angle", "Angle") },
                                        { ampd::micDrive, "Drive" },
                                        { ampd::micLowCut, L("Coupe-bas", "Low cut") },
                                        { ampd::micHighCut, L("Coupe-haut", "High cut") },
                                        { ampd::micRoom, L("Pièce", "Room") },
                                        { ampd::micRoomSize, L("Taille pièce", "Room size") },
                                        { ampd::micRoomTone, L("Ton pièce", "Room tone") },
                                        { ampd::micNoise, L("Bruit", "Noise") },
                                        { ampd::micOutput, L("Sortie", "Output") },
                                        { ampd::micMix, L("Mélange", "Mix") } } };
    const auto cell = std::min(dp(96.0f), (size.x - dp(8.0f)) / static_cast<float>(columns));
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(position.x + dp(4.0f) + cell * static_cast<float>(index % columns),
                                   y + knobHeight(diameter) * static_cast<float>(index / columns)),
                            cell, at(parameters, pane, knobs[index].index), knobs[index].label, colour, live, set, diameter);
    return changed;
}

// ---- Pression, Fournaise, Écho Bande, Multibande Pro, Velours, Patine, Sonomètre ---

#include "MixEffectsPanes.inc"

// ---- Tranche, Finition ---

#include "SuiteEffectsPanes.inc"

// ---- Halo ---

#include "HaloPanes.inc"

// ---- The effects added on 2026-10-14 ---

// The window most of them share: the display large on top (a title in its
// corner), its controls under it in sections (controlGroups) — each
// section's values as pane indices.
struct AddedSection {
    const char* title;
    std::vector<int> indices;
};
bool addedLarge(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                const Live& live, const Setter& set, const char* heading, const std::vector<AddedSection>& sections,
                const std::function<bool(const Box&)>& display, const float smallestDisplay = 190.0f)
{
    auto* list = ImGui::GetWindowDrawList();
    std::vector<ControlGroup> groups;
    for (const auto& item : sections) {
        ControlGroup group { juce::String::fromUTF8(item.title), {} };
        for (const auto index : item.indices)
            if (index >= 0 && static_cast<std::size_t>(index) < pane.parameters.size() && pane.parameters[static_cast<std::size_t>(index)] >= 0)
                group.indices.push_back(pane.parameters[static_cast<std::size_t>(index)]);
        if (!group.indices.empty())
            groups.push_back(std::move(group));
    }
    const auto controls = controlGroups(parameters, groups, ImVec2(position.x, 0.0f), size.x, colour, live, set, false, nullptr);
    const auto displayHeight = std::max(dp(smallestDisplay), size.y - controls - dp(10.0f));
    const Box outer { position, ImVec2(position.x + size.x, position.y + displayHeight) };
    const auto inner = frame(list, outer);
    auto changed = false;
    list->PushClipRect(inner.min, inner.max, true);
    if (display)
        changed |= display(inner);
    title(list, inner, heading);
    list->PopClipRect();
    controlGroups(parameters, groups, ImVec2(position.x, outer.max.y + dp(8.0f)), size.x, colour, live, set, true, &changed);
    return changed;
}

// The values of a pane as an array (missing ones at their defaults).
template <std::size_t N>
std::array<float, N> addedValues(std::vector<Param>& parameters, const Pane& pane, const char* typeKey)
{
    return studioValues<N>(parameters, pane, typeKey);
}

#include "VoicePanes.inc"
#include "PitchPanes.inc"
#include "SpacePanes.inc"
#include "RhythmPanes.inc"
#include "MotionPanes.inc"
#include "ResonancePanes.inc"
#include "UtilityPanes.inc"
#include "MidiPanes.inc"

bool addedDisplay(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane, const Box& box, const ImU32 colour,
                  const Live& live, const Setter& set, const bool large)
{
    if (kind == PaneKind::vocoder || kind == PaneKind::deEsser || kind == PaneKind::tuner)
        return voiceDisplay(kind, parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::harmonie || kind == PaneKind::justesse)
        return pitchDisplay(kind, parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::empreinte || kind == PaneKind::givre)
        return spacefxDisplay(kind, parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::grains || kind == PaneKind::saccade || kind == PaneKind::boucleur)
        return rhythmDisplay(kind, parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::pompe || kind == PaneKind::decaleur || kind == PaneKind::anneau || kind == PaneKind::rotor)
        return motionDisplay(kind, parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::resonateurs || kind == PaneKind::corps || kind == PaneKind::peigne)
        return resonanceDisplay(kind, parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::utilitaire || kind == PaneKind::stereoscope)
        return utilityDisplay(kind, parameters, pane, box, colour, live, set, large);
    if (kind == PaneKind::arpege || kind == PaneKind::accords || kind == PaneKind::gamme || kind == PaneKind::echoNotes || kind == PaneKind::alea || kind == PaneKind::duree || kind == PaneKind::sequenceur || kind == PaneKind::roulement)
        return midifxDisplay(kind, parameters, pane, box, colour, live, set, large);
    return false;
}

bool addedLargePane(const PaneKind kind, const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size,
                    const ImU32 colour, const Live& live, const Setter& set)
{
    if (kind == PaneKind::vocoder || kind == PaneKind::deEsser || kind == PaneKind::tuner)
        return voiceLarge(kind, pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::harmonie || kind == PaneKind::justesse)
        return pitchLarge(kind, pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::empreinte || kind == PaneKind::givre)
        return spacefxLarge(kind, pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::grains || kind == PaneKind::saccade || kind == PaneKind::boucleur)
        return rhythmLarge(kind, pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::pompe || kind == PaneKind::decaleur || kind == PaneKind::anneau || kind == PaneKind::rotor)
        return motionLarge(kind, pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::resonateurs || kind == PaneKind::corps || kind == PaneKind::peigne)
        return resonanceLarge(kind, pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::utilitaire || kind == PaneKind::stereoscope)
        return utilityLarge(kind, pane, parameters, position, size, colour, live, set);
    if (kind == PaneKind::arpege || kind == PaneKind::accords || kind == PaneKind::gamme || kind == PaneKind::echoNotes || kind == PaneKind::alea || kind == PaneKind::duree || kind == PaneKind::sequenceur || kind == PaneKind::roulement)
        return midifxLarge(kind, pane, parameters, position, size, colour, live, set);
    return false;
}

std::vector<int> addedCardFields(const PaneKind kind, std::vector<Param>& parameters, const Pane& pane)
{
    if (kind == PaneKind::vocoder || kind == PaneKind::deEsser || kind == PaneKind::tuner)
        return voiceCardFields(kind, parameters, pane);
    if (kind == PaneKind::harmonie || kind == PaneKind::justesse)
        return pitchCardFields(kind, parameters, pane);
    if (kind == PaneKind::empreinte || kind == PaneKind::givre)
        return spacefxCardFields(kind, parameters, pane);
    if (kind == PaneKind::grains || kind == PaneKind::saccade || kind == PaneKind::boucleur)
        return rhythmCardFields(kind, parameters, pane);
    if (kind == PaneKind::pompe || kind == PaneKind::decaleur || kind == PaneKind::anneau || kind == PaneKind::rotor)
        return motionCardFields(kind, parameters, pane);
    if (kind == PaneKind::resonateurs || kind == PaneKind::corps || kind == PaneKind::peigne)
        return resonanceCardFields(kind, parameters, pane);
    if (kind == PaneKind::utilitaire || kind == PaneKind::stereoscope)
        return utilityCardFields(kind, parameters, pane);
    if (kind == PaneKind::arpege || kind == PaneKind::accords || kind == PaneKind::gamme || kind == PaneKind::echoNotes || kind == PaneKind::alea || kind == PaneKind::duree || kind == PaneKind::sequenceur || kind == PaneKind::roulement)
        return midifxCardFields(kind, parameters, pane);
    return {};
}

juce::String addedFormat(const juce::String& binding, const double value)
{
    if (auto shown = voiceFormat(binding, value); shown.isNotEmpty())
        return shown;
    if (auto shown = pitchFormat(binding, value); shown.isNotEmpty())
        return shown;
    if (auto shown = spacefxFormat(binding, value); shown.isNotEmpty())
        return shown;
    if (auto shown = rhythmFormat(binding, value); shown.isNotEmpty())
        return shown;
    if (auto shown = motionFormat(binding, value); shown.isNotEmpty())
        return shown;
    if (auto shown = resonanceFormat(binding, value); shown.isNotEmpty())
        return shown;
    if (auto shown = utilityFormat(binding, value); shown.isNotEmpty())
        return shown;
    if (auto shown = midifxFormat(binding, value); shown.isNotEmpty())
        return shown;
    return {};
}

bool addedStepped(const juce::String& binding)
{
    return midifxStepped(binding) || voiceStepped(binding) || pitchStepped(binding) || spacefxStepped(binding) || rhythmStepped(binding) || motionStepped(binding) || resonanceStepped(binding) || utilityStepped(binding);
}

// ---- The controls of the other devices, in their windows -------------------------------------------

enum class ControlKind { knob, toggle, buttons, list };

bool endsWithAny(const juce::String& binding, std::initializer_list<const char*> suffixes)
{
    for (const auto* suffix : suffixes)
        if (binding.endsWith(suffix))
            return true;
    return false;
}

ControlKind controlKindOf(const Param& parameter)
{
    const auto whole = std::abs(parameter.minimum - std::round(parameter.minimum)) < 1.0e-6
                       && std::abs(parameter.maximum - std::round(parameter.maximum)) < 1.0e-6;
    const auto named = studioStepped(parameter)
                       || endsWithAny(parameter.binding, { ".mode", ".shape", ".waveform", ".sync", ".type", "Type", "Sync", "Shape", "Mode" });
    if (!whole || !named)
        return ControlKind::knob;
    const auto span = parameter.maximum - parameter.minimum;
    if (span <= 1.0 + 1.0e-6 && !parameter.binding.endsWith("sync") && !parameter.binding.endsWith("Sync"))
        return ControlKind::toggle;
    return span <= 5.0 ? ControlKind::buttons : ControlKind::list;
}

// A choice's names: what each of its values reads as.
std::vector<juce::String> choiceNames(const Param& parameter)
{
    std::vector<juce::String> list;
    for (auto value = parameter.minimum; value <= parameter.maximum + 1.0e-6; value += 1.0)
        list.push_back(formatValue(parameter, value));
    return list;
}

float choiceWidth(const Param& parameter, const ControlKind kind)
{
    if (kind == ControlKind::list)
        return dp(170.0f);
    if (kind == ControlKind::toggle)
        return textSize(parameter.label.toRawUTF8(), 0.8f).x + dp(30.0f);
    auto width = dp(12.0f);
    for (const auto& name : choiceNames(parameter))
        width += std::max(dp(58.0f), textSize(name.toRawUTF8(), 0.66f).x + dp(18.0f));
    return width;
}

struct ControlSection {
    const char* title;
    std::vector<int> indices;
};

float nativeSectionHeight(std::vector<Param>& parameters, const ControlSection& section, const float width, const float diameter)
{
    if (section.indices.empty())
        return 0.0f;
    auto knobs = 0;
    auto choiceRow = 0.0f;
    auto choiceRows = 0;
    for (const auto index : section.indices) {
        const auto& parameter = parameters[static_cast<std::size_t>(index)];
        const auto kind = controlKindOf(parameter);
        if (kind == ControlKind::knob) {
            ++knobs;
            continue;
        }
        const auto needed = choiceWidth(parameter, kind) + dp(12.0f);
        if (choiceRows == 0 || choiceRow + needed > width) {
            ++choiceRows;
            choiceRow = 0.0f;
        }
        choiceRow += needed;
    }
    const auto perRow = std::max(1, static_cast<int>(width / dp(diameter + 52.0f)));
    const auto knobRows = (knobs + perRow - 1) / perRow;
    return dp(18.0f) + static_cast<float>(choiceRows) * (dp(26.0f) + ImGui::GetFontSize() * 0.9f)
           + static_cast<float>(knobRows) * (knobHeight(diameter) + dp(8.0f)) + dp(6.0f);
}

bool nativeSection(std::vector<Param>& parameters, const ControlSection& part, const ImVec2 origin, const float width,
                   const float diameter, const ImU32 colour, const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    section(list, ImVec2(origin.x, origin.y), part.title);
    auto y = origin.y + dp(18.0f);
    // The choices first, a row of them (more if they do not fit): a switch,
    // buttons named by their values, a list for many.
    auto x = origin.x;
    auto rowUsed = false;
    const auto labelHeight = ImGui::GetFontSize() * 0.9f;
    for (const auto index : part.indices) {
        auto& parameter = parameters[static_cast<std::size_t>(index)];
        const auto kind = controlKindOf(parameter);
        if (kind == ControlKind::knob)
            continue;
        const auto controlWidth = choiceWidth(parameter, kind);
        if (rowUsed && x + controlWidth > origin.x + width) {
            x = origin.x;
            y += dp(26.0f) + labelHeight;
        }
        text(list, ImVec2(x, y), theme::palette().textDim, parameter.label.toRawUTF8(), 0.64f);
        const ImVec2 at(x, y + labelHeight);
        ImGui::PushID(index);
        if (kind == ControlKind::toggle) {
            changed |= switchCell(parameter.label.toRawUTF8(), &parameter, at, ImVec2(controlWidth, dp(22.0f)), colour, set);
        } else if (kind == ControlKind::buttons) {
            const auto names = choiceNames(parameter);
            std::vector<std::string> storage;
            storage.reserve(names.size());
            for (const auto& name : names)
                storage.push_back(name.toStdString());
            std::vector<const char*> pointers;
            for (const auto& name : storage)
                pointers.push_back(name.c_str());
            changed |= segmented("##choice", &parameter, pointers, at, controlWidth, dp(22.0f), colour, set);
        } else {
            changed |= comboCell("##list", &parameter, choiceNames(parameter), at, controlWidth, set);
        }
        ImGui::PopID();
        x += controlWidth + dp(12.0f);
        rowUsed = true;
    }
    if (rowUsed)
        y += dp(26.0f) + labelHeight;
    // The knobs, in rows.
    std::vector<int> knobs;
    for (const auto index : part.indices)
        if (controlKindOf(parameters[static_cast<std::size_t>(index)]) == ControlKind::knob)
            knobs.push_back(index);
    const auto perRow = std::max(1, static_cast<int>(width / dp(diameter + 52.0f)));
    const auto cell = std::min(dp(diameter + 72.0f), width / static_cast<float>(std::min<int>(perRow, std::max<int>(1, static_cast<int>(knobs.size())))));
    for (std::size_t at = 0; at < knobs.size(); ++at) {
        const auto row = static_cast<int>(at) / perRow;
        const auto column = static_cast<int>(at) % perRow;
        auto& parameter = parameters[static_cast<std::size_t>(knobs[at])];
        // The label without the device's name before it ("Chorus · Rate").
        const auto label = parameter.label.fromLastOccurrenceOf(juce::String::fromUTF8("\xc2\xb7 "), false, false).toStdString();
        changed |= knobCell(ImVec2(origin.x + cell * static_cast<float>(column), y + static_cast<float>(row) * (knobHeight(diameter) + dp(8.0f))),
                            cell, &parameter, label.c_str(), colour, live, set, diameter);
    }
    return changed;
}

} // namespace

float nativeControls(std::vector<Param>& parameters, const std::vector<int>& main, const std::vector<int>& others, const char* mainTitle,
                     const ImVec2 origin, const float width, const ImU32 colour, const Live& live, const Setter& set, const bool draw,
                     bool* changed)
{
    const auto& p = theme::palette();
    const std::array<ControlSection, 2> sections { { { mainTitle, main }, { L("AUTRES RÉGLAGES", "OTHER SETTINGS"), others } } };
    const auto knobCount = main.size() + others.size();
    const auto diameter = knobCount <= 8 ? 50.0f : knobCount <= 14 ? 42.0f : 36.0f;
    const auto inner = width - dp(24.0f);
    // What a section needs in width on one row: its choices, its knobs.
    const auto natural = [&](const ControlSection& part) {
        auto choices = 0.0f;
        auto knobs = 0;
        for (const auto index : part.indices) {
            const auto& parameter = parameters[static_cast<std::size_t>(index)];
            const auto kind = controlKindOf(parameter);
            if (kind == ControlKind::knob)
                ++knobs;
            else
                choices += choiceWidth(parameter, kind) + dp(12.0f);
        }
        return std::max(choices, static_cast<float>(knobs) * dp(diameter + 60.0f));
    };
    // Side by side when both fit on a row; else one under the other.
    const auto both = !main.empty() && !others.empty();
    const auto mainWidth = natural(sections[0]);
    const auto sideBySide = both && mainWidth + natural(sections[1]) + dp(28.0f) <= inner;
    const auto firstWidth = sideBySide ? std::max(mainWidth, inner * 0.5f) : inner;
    const auto secondWidth = sideBySide ? inner - firstWidth - dp(28.0f) : inner;
    auto height = dp(12.0f);
    if (sideBySide)
        height += std::max(nativeSectionHeight(parameters, sections[0], firstWidth, diameter),
                           nativeSectionHeight(parameters, sections[1], secondWidth, diameter))
                  + dp(8.0f);
    else
        for (const auto& part : sections)
            if (!part.indices.empty())
                height += nativeSectionHeight(parameters, part, inner, diameter) + dp(8.0f);
    if (!draw)
        return height;
    auto* list = ImGui::GetWindowDrawList();
    // A faceplate in the device's colour.
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), theme::mix(colour, p.bg1, 0.88f), dp(6.0f));
    list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), theme::alpha(colour, 0.45f), dp(6.0f));
    auto moved = false;
    if (sideBySide) {
        const auto top = origin.y + dp(10.0f);
        moved |= nativeSection(parameters, sections[0], ImVec2(origin.x + dp(12.0f), top), firstWidth, diameter, colour, live, set);
        const auto split = origin.x + dp(12.0f) + firstWidth + dp(14.0f);
        list->AddLine(ImVec2(split, top), ImVec2(split, origin.y + height - dp(10.0f)), theme::alpha(colour, 0.3f));
        moved |= nativeSection(parameters, sections[1], ImVec2(split + dp(14.0f), top), secondWidth, diameter, colour, live, set);
    } else {
        auto y = origin.y + dp(10.0f);
        for (const auto& part : sections) {
            if (part.indices.empty())
                continue;
            moved |= nativeSection(parameters, part, ImVec2(origin.x + dp(12.0f), y), inner, diameter, colour, live, set);
            y += nativeSectionHeight(parameters, part, inner, diameter) + dp(8.0f);
        }
    }
    if (changed != nullptr)
        *changed = moved;
    return height;
}

namespace {

// A setting's name on its own: after its group's ("Sortie · niveau"), its
// first letter up.
juce::String ownLabel(const Param& parameter)
{
    const auto label = parameter.label.fromLastOccurrenceOf(juce::String::fromUTF8("\xc2\xb7 "), false, false);
    return label.substring(0, 1).toUpperCase() + label.substring(1);
}

} // namespace

ImVec2 compactControlSize(const Param& parameter, const float diameter)
{
    const auto label = ownLabel(parameter);
    switch (controlKindOf(parameter)) {
    case ControlKind::toggle: return ImVec2(std::clamp(textSize(label.toRawUTF8(), 0.8f).x + dp(26.0f), dp(56.0f), dp(120.0f)), dp(22.0f));
    case ControlKind::buttons:
    case ControlKind::list: {
        auto widest = textSize(label.toRawUTF8(), 0.64f).x;
        for (const auto& name : choiceNames(parameter))
            widest = std::max(widest, textSize(name.toRawUTF8(), 0.8f).x + dp(28.0f));
        return ImVec2(std::clamp(widest, dp(72.0f), dp(130.0f)), ImGui::GetFontSize() * 0.8f + dp(24.0f));
    }
    default:
        return ImVec2(std::clamp(textSize(label.toRawUTF8(), 0.86f).x + dp(8.0f), dp(diameter + 20.0f), dp(90.0f)), knobHeight(diameter));
    }
}

bool compactControl(Param& parameter, const ImVec2 origin, const float width, const ImU32 colour, const Live& live, const Setter& set,
                    const float diameter)
{
    const auto label = ownLabel(parameter).toStdString();
    switch (controlKindOf(parameter)) {
    case ControlKind::toggle: return switchCell(label.c_str(), &parameter, origin, ImVec2(width, dp(22.0f)), colour, set);
    case ControlKind::buttons:
    case ControlKind::list: {
        text(ImGui::GetWindowDrawList(), origin, theme::palette().textDim, label.c_str(), 0.64f);
        ImGui::PushID(parameter.binding.toRawUTF8());
        const auto changed = comboCell("##list", &parameter, choiceNames(parameter), ImVec2(origin.x, origin.y + ImGui::GetFontSize() * 0.8f),
                                       width, set);
        ImGui::PopID();
        return changed;
    }
    default: return knobCell(origin, width, &parameter, label.c_str(), colour, live, set, diameter);
    }
}

float controlGroups(std::vector<Param>& parameters, const std::vector<ControlGroup>& groups, const ImVec2 origin, const float width,
                    const ImU32 colour, const Live& live, const Setter& set, const bool draw, bool* changed)
{
    const auto& p = theme::palette();
    std::vector<std::string> titles;
    std::vector<ControlSection> sections;
    for (const auto& group : groups)
        if (!group.indices.empty())
            titles.push_back(group.title.toStdString());
    for (std::size_t at = 0, title = 0; at < groups.size(); ++at)
        if (!groups[at].indices.empty())
            sections.push_back({ titles[title++].c_str(), groups[at].indices });
    if (sections.empty())
        return 0.0f;
    auto knobCount = std::size_t {};
    for (const auto& part : sections)
        knobCount += part.indices.size();
    const auto diameter = knobCount <= 8 ? 50.0f : knobCount <= 16 ? 42.0f : 36.0f;
    const auto inner = width - dp(24.0f);
    const auto gap = dp(28.0f);
    // What a section needs on one row: its choices, its knobs.
    const auto natural = [&](const ControlSection& part) {
        auto choices = 0.0f;
        auto knobs = 0;
        for (const auto index : part.indices) {
            const auto& parameter = parameters[static_cast<std::size_t>(index)];
            const auto kind = controlKindOf(parameter);
            if (kind == ControlKind::knob)
                ++knobs;
            else
                choices += choiceWidth(parameter, kind) + dp(12.0f);
        }
        return std::clamp(std::max(choices, static_cast<float>(knobs) * dp(diameter + 60.0f)), dp(150.0f), inner);
    };
    // The sections in rows, each as wide as it needs; a row's room shared out.
    std::vector<std::vector<std::pair<std::size_t, float>>> rows(1);
    auto used = 0.0f;
    for (std::size_t at = 0; at < sections.size(); ++at) {
        const auto need = natural(sections[at]);
        if (!rows.back().empty() && used + gap + need > inner) {
            rows.emplace_back();
            used = 0.0f;
        }
        used += (rows.back().empty() ? 0.0f : gap) + need;
        rows.back().push_back({ at, need });
    }
    for (auto& row : rows) {
        auto total = 0.0f;
        for (const auto& [at, need] : row)
            total += need;
        const auto room = inner - gap * static_cast<float>(row.size() - 1);
        for (auto& [at, need] : row)
            need = need * room / std::max(1.0f, total);
    }
    std::vector<float> heights;
    auto height = dp(12.0f);
    for (const auto& row : rows) {
        auto tallest = 0.0f;
        for (const auto& [at, need] : row)
            tallest = std::max(tallest, nativeSectionHeight(parameters, sections[at], need, diameter));
        heights.push_back(tallest);
        height += tallest + dp(8.0f);
    }
    if (!draw)
        return height;
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), theme::mix(colour, p.bg1, 0.88f), dp(6.0f));
    list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), theme::alpha(colour, 0.45f), dp(6.0f));
    auto moved = false;
    auto y = origin.y + dp(10.0f);
    for (std::size_t line = 0; line < rows.size(); ++line) {
        auto x = origin.x + dp(12.0f);
        for (std::size_t at = 0; at < rows[line].size(); ++at) {
            const auto& [index, need] = rows[line][at];
            if (at > 0) {
                list->AddLine(ImVec2(x - gap * 0.5f, y), ImVec2(x - gap * 0.5f, y + heights[line]), theme::alpha(colour, 0.3f));
            }
            ImGui::PushID(static_cast<int>(index));
            moved |= nativeSection(parameters, sections[index], ImVec2(x, y), need, diameter, colour, live, set);
            ImGui::PopID();
            x += need + gap;
        }
        y += heights[line] + dp(8.0f);
    }
    if (changed != nullptr)
        *changed = moved;
    return height;
}

// ---- Entry points --------------------------------------------------------------------------------

void setCardFile(const CardFile* file)
{
    cardFile = file;
}

bool studioPane(const Pane& pane, std::vector<Param>& parameters, const ImVec2 min, const ImVec2 max, const ImU32 colour,
                const Live& live, const Setter& set)
{
    const Box box { min, max };
    switch (pane.kind) {
    case PaneKind::reverbPro: return reverbDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::limiterPro: {
        auto& meter = loudnessOf(parameters, pane);
        meter.feed(live.afterFrame, ImGui::GetTime());
        const auto changed = limiterDisplay(parameters, pane, box, colour, live, set, false);
        char label[32];
        const auto shortTerm = meter.shortTerm();
        std::snprintf(label, sizeof(label), shortTerm < -99.0 ? "S -- LUFS" : "S %.1f LUFS", shortTerm);
        const auto size = textSize(label, 0.68f);
        text(ImGui::GetWindowDrawList(), ImVec2(max.x - size.x - dp(6.0f), min.y + dp(4.0f)), theme::palette().text, label, 0.68f);
        return changed;
    }
    case PaneKind::filterPro: return filterDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::ampSim: return ampDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::cabSim: return cabDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::micSim: return micDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::pression: return pressionDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::fournaise: return fournaiseDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::echoBande: return echoDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::multibandPro: return multibandDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::velours: return veloursDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::patine: return patineDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::sonometre: return sonometreDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::tranche: return trancheDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::finition: return finitionDisplay(parameters, pane, box, colour, live, set, false);
    case PaneKind::halo: return haloDisplay(parameters, pane, box, colour, live, set, false);
    default: return addedDisplay(pane.kind, parameters, pane, box, colour, live, set, false);
    }
}

bool studioLargePane(const Pane& pane, std::vector<Param>& parameters, const ImVec2 position, const ImVec2 size, const ImU32 colour,
                     const Live& live, const Setter& set)
{
    ImGui::PushID(static_cast<int>(pane.kind));
    auto changed = false;
    switch (pane.kind) {
    case PaneKind::reverbPro: changed = reverbLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::limiterPro: changed = limiterLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::filterPro: changed = filterLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::ampSim: changed = ampLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::cabSim: changed = cabLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::micSim: changed = micLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::pression: changed = pressionLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::fournaise: changed = fournaiseLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::echoBande: changed = echoLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::multibandPro: changed = multibandLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::velours: changed = veloursLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::patine: changed = patineLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::sonometre: changed = sonometreLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::tranche: changed = trancheLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::finition: changed = finitionLarge(pane, parameters, position, size, colour, live, set); break;
    case PaneKind::halo: changed = haloLarge(pane, parameters, position, size, colour, live, set); break;
    default: changed = addedLargePane(pane.kind, pane, parameters, position, size, colour, live, set); break;
    }
    ImGui::PopID();
    return changed;
}

float compressorStudioPanelHeight()
{
    return dp(36.0f) + knobHeight(34.0f) + dp(8.0f);
}

bool compressorStudioPanel(const Pane& pane, std::vector<Param>& parameters, const ImVec2 origin, const float width, const ImU32 colour,
                           const Live& live, const Setter& set)
{
    auto* list = ImGui::GetWindowDrawList();
    auto changed = false;
    const auto rowHeight = dp(22.0f);
    // Styles, channels, options on one row.
    const auto styleWidth = width * 0.56f;
    section(list, ImVec2(origin.x, origin.y - dp(1.0f)), L("STYLE", "STYLE"));
    changed |= segmented("##style", at(parameters, pane, pro::compressorStyle),
                         { "Clean", "Classic", "Opto", L("Voix", "Vocal"), "Mastering", "Bus", "Punch", L("Pompage", "Pumping") },
                         ImVec2(origin.x + dp(40.0f), origin.y - dp(4.0f)), styleWidth - dp(40.0f), rowHeight, colour, set,
                         L("Le caractère : Clean (net), Classic (rétroaction), Opto (relâchement qui dépend du signal), Voix, "
                           "Mastering (très doux), Bus (cohésion), Punch (laisse passer l'attaque), Pompage (relâchement marqué).",
                           "The character: Clean, Classic (feedback), Opto (program-dependent release), Vocal, Mastering "
                           "(very gentle), Bus (glue), Punch (lets the attack through), Pumping (pronounced release)."));
    auto x = origin.x + styleWidth + dp(8.0f);
    changed |= segmented("##channels", at(parameters, pane, pro::compressorChannels), { L("Stéréo", "Stereo"), "Mid", "Side" },
                         ImVec2(x, origin.y - dp(4.0f)), dp(150.0f), rowHeight, colour, set,
                         L("Stéréo, ou seulement le centre (mid) ou les côtés (side).", "Stereo, or only the centre (mid) or the sides (side)."));
    x += dp(158.0f);
    const auto optionWidth = std::max(dp(70.0f), (origin.x + width - x) / 3.0f - dp(4.0f));
    changed |= switchCell(L("Relâch. auto", "Auto release"), at(parameters, pane, pro::compressorAutoRelease), ImVec2(x, origin.y - dp(4.0f)),
                          ImVec2(optionWidth, rowHeight), colour, set,
                          L("Le relâchement s'adapte : court sur les transitoires, long sur ce qui dure.",
                            "The release adapts: short on transients, long on what lasts."));
    changed |= switchCell(L("Gain auto", "Auto gain"), at(parameters, pane, pro::compressorAutoGain),
                          ImVec2(x + optionWidth + dp(4.0f), origin.y - dp(4.0f)), ImVec2(optionWidth, rowHeight), colour, set,
                          L("Compense ce que la compression retire.", "Makes up for what the compression takes."));
    changed |= switchCell(L("Écoute", "Audition"), at(parameters, pane, pro::compressorAudition),
                          ImVec2(x + (optionWidth + dp(4.0f)) * 2.0f, origin.y - dp(4.0f)), ImVec2(optionWidth, rowHeight), colour, set,
                          L("Écouter ce que le détecteur entend (après ses filtres).", "Listen to what the detector hears (after its filters)."));
    // Every knob.
    struct Item {
        int index;
        const char* label;
    };
    const std::array<Item, 13> knobs { { { pro::compressorThreshold, L("Seuil", "Threshold") },
                                         { pro::compressorRatio, L("Ratio", "Ratio") },
                                         { pro::compressorKnee, L("Genou", "Knee") },
                                         { pro::compressorRange, L("Plage", "Range") },
                                         { pro::compressorAttack, L("Attaque", "Attack") },
                                         { pro::compressorRelease, L("Relâchement", "Release") },
                                         { pro::compressorHold, L("Maintien", "Hold") },
                                         { pro::compressorLookahead, L("Anticipation", "Lookahead") },
                                         { pro::compressorMakeup, L("Compensation", "Makeup") },
                                         { pro::compressorMix, L("Mélange", "Mix") },
                                         { pro::compressorLink, L("Lien stéréo", "Stereo link") },
                                         { pro::compressorSidechainHigh, L("Détect. PH", "Detect. HP") },
                                         { pro::compressorSidechainLow, L("Détect. PB", "Detect. LP") } } };
    const auto cell = std::min(dp(96.0f), width / static_cast<float>(knobs.size()));
    const auto y = origin.y + rowHeight + dp(6.0f);
    for (std::size_t index = 0; index < knobs.size(); ++index)
        changed |= knobCell(ImVec2(origin.x + cell * static_cast<float>(index), y), cell, at(parameters, pane, knobs[index].index),
                            knobs[index].label, colour, live, set, 34.0f);
    return changed;
}

std::vector<int> studioCardFields(const Pane& pane, std::vector<Param>& parameters)
{
    switch (pane.kind) {
    case PaneKind::reverbPro: return { pro::reverbSpace, pro::reverbDecay, pro::reverbPredelay, pro::reverbMix };
    case PaneKind::limiterPro: return { pro::limiterGain, pro::limiterOutput, pro::limiterRelease, pro::limiterStyle };
    case PaneKind::filterPro:
        return { pro::filterIndex(0, pro::unitCutoff), pro::filterIndex(0, pro::unitResonance), pro::filterMix };
    case PaneKind::ampSim: {
        const auto channel = whole(static_cast<float>(get(parameters, pane, engine::amp::ampChannel, 0.0)), 2);
        std::vector<int> fields;
        for (const auto index : { engine::amp::ampGain, engine::amp::ampBass, engine::amp::ampMiddle, engine::amp::ampTreble,
                                  engine::amp::ampMaster })
            fields.push_back(engine::amp::ampChannelIndex(index, channel));
        return fields;
    }
    case PaneKind::cabSim:
        return { engine::amp::cabPositionA, engine::amp::cabDistanceA, engine::amp::cabRoom, engine::amp::cabOutput };
    case PaneKind::micSim: return { engine::amp::micDistance, engine::amp::micAngle, engine::amp::micDrive, engine::amp::micMix };
    case PaneKind::pression:
        return { engine::mix::pressionDepth, engine::mix::pressionTime, engine::mix::pressionUpward, engine::mix::pressionDownward };
    case PaneKind::fournaise:
        return { engine::mix::fournaiseDrive, engine::mix::fournaiseTone, engine::mix::fournaiseMix, engine::mix::fournaiseOutput };
    case PaneKind::echoBande: return { engine::mix::echoTime, engine::mix::echoIntensity, engine::mix::echoLevel, engine::mix::echoMix };
    case PaneKind::multibandPro:
        return { engine::mix::multibandThreshold1, engine::mix::multibandThreshold2, engine::mix::multibandThreshold3,
                 engine::mix::multibandThreshold4 };
    case PaneKind::velours:
        return { engine::mix::veloursDepth, engine::mix::veloursSharpness, engine::mix::veloursSelectivity, engine::mix::veloursMix };
    case PaneKind::patine: return { engine::mix::patineMagnitude, engine::mix::patineTone, engine::mix::patineMix, engine::mix::patineOutput };
    case PaneKind::sonometre: return {};
    case PaneKind::tranche:
        return { engine::suite::trancheCompThreshold, engine::suite::trancheExcite, engine::suite::trancheWidth, engine::suite::trancheOutput };
    case PaneKind::finition:
        return { engine::suite::finitionGain, engine::suite::finitionCeiling, engine::suite::finitionDynamics, engine::suite::finitionWidth };
    case PaneKind::halo: return { engine::halo::haloMix, engine::halo::haloDecay, engine::halo::haloPredelay, engine::halo::haloSize };
    default: return addedCardFields(pane.kind, parameters, pane);
    }
}

namespace {

// The part of a binding after the node's: "comppro.style".
bool is(const Param& parameter, const char* suffix)
{
    return parameter.binding.endsWith(suffix);
}

juce::String pick(const double value, const std::vector<const char*>& names)
{
    const auto index = std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(names.size()) - 1);
    return juce::String::fromUTF8(names[static_cast<std::size_t>(index)]);
}

juce::String onOff(const double value)
{
    return value >= 0.5 ? ui::Ls("Actif", "On") : ui::Ls("Coupé", "Off");
}

} // namespace

juce::String studioFormat(const Param& parameter, const double value)
{
    const auto& binding = parameter.binding;
    if (auto shown = addedFormat(binding, value); shown.isNotEmpty())
        return shown;
    const auto knob = [](const double v) { return juce::String(v, 1); };
    const auto off = [](const char* french, const char* english) { return ui::Ls(french, english); };
    const auto degrees = [](const double v) { return juce::String(juce::roundToInt(v)) + juce::String::fromUTF8("\xc2\xb0"); };
    const auto centimetres = [](const double v) { return juce::String(v, v < 10.0 ? 1 : 0) + " cm"; };
    if (binding.contains(".amp.") || binding.startsWith("amp.")) {
        const auto suffix = binding.fromLastOccurrenceOf("amp.", false, false);
        if (suffix == "model" || suffix == "modelB")
            return ampName(whole(static_cast<float>(value), engine::amp::ampModelCount));
        if (suffix == "channel")
            return value >= 0.5 ? "B" : "A";
        if (suffix == "pedal")
            return pick(value, { L("Aucune", "None"), "Boost", "Overdrive", "Fuzz" });
        if (suffix == "oversampling")
            return pick(value, { "1x", "2x", "4x" });
        if (suffix == "bright" || suffix == "brightB")
            return onOff(value);
        // Channel A's knobs and channel B's ("gainB").
        const auto knobOf = suffix.endsWith("B") ? suffix.dropLastCharacters(1) : suffix;
        if (juce::StringArray { "gain", "bass", "middle", "treble", "presence", "master", "depth" }.contains(knobOf))
            return knob(value);
        if (suffix == "sag" || suffix == "mix" || suffix == "pedalDrive" || suffix == "pedalTone")
            return percent(value);
        if (suffix == "tight")
            return value <= 20.5 ? off("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix == "gate")
            return value <= -89.5 ? off("Désactivé", "Off") : juce::String(value, 0) + " dB";
        return {};
    }
    if (binding.contains(".cab.") || binding.startsWith("cab.")) {
        const auto suffix = binding.fromLastOccurrenceOf("cab.", false, false);
        if (suffix == "model")
            return cabName(whole(static_cast<float>(value), engine::amp::cabModelCount));
        if (suffix == "micA" || suffix == "micB")
            return micName(whole(static_cast<float>(value), engine::amp::cabMicCount));
        if (suffix == "micBOn" || suffix == "polarityB" || suffix == "align" || suffix == "stereo")
            return onOff(value);
        if (suffix == "positionA" || suffix == "positionB")
            return value <= 0.02 ? off("Centre", "Centre") : value >= 0.98 ? off("Bord", "Edge") : percent(value);
        if (suffix == "distanceA" || suffix == "distanceB")
            return centimetres(value);
        if (suffix == "angleA" || suffix == "angleB")
            return degrees(value);
        if (suffix == "blend")
            return "A " + juce::String(juce::roundToInt((1.0 - value) * 100.0)) + " / B " + juce::String(juce::roundToInt(value * 100.0));
        if (suffix == "room" || suffix == "resonance" || suffix == "mix")
            return percent(value);
        if (suffix == "lowCut")
            return value <= 20.5 ? off("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix == "highCut")
            return value >= 19'900.0 ? off("Désactivé", "Off") : juce::String(value / 1'000.0, 1) + " kHz";
        return {};
    }
    if (binding.contains("micsim.")) {
        const auto suffix = binding.fromLastOccurrenceOf("micsim.", false, false);
        if (suffix == "model")
            return micName(whole(static_cast<float>(value), engine::amp::micModelCount));
        if (suffix == "pattern")
            return pick(value, { L("Du micro", "Its own"), "Omni", L("Cardioïde", "Cardioid"), L("Hypercardioïde", "Hypercardioid"),
                                 L("Figure 8", "Figure 8") });
        if (suffix == "preamp")
            return pick(value, { "Transistor", L("Lampe", "Valve"), L("Transformateur", "Transformer") });
        if (suffix == "distance")
            return centimetres(value);
        if (suffix == "angle")
            return degrees(value);
        if (suffix == "room" || suffix == "mix" || suffix == "roomSize")
            return percent(value);
        if (suffix == "roomTone")
            return juce::String(value / 1'000.0, 1) + " kHz";
        if (suffix == "noise")
            return value <= -89.5 ? off("Du micro", "Its own") : juce::String(value, 0) + " dB";
        if (suffix == "lowCut")
            return value <= 20.5 ? off("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix == "highCut")
            return value >= 19'900.0 ? off("Désactivé", "Off") : juce::String(value / 1'000.0, 1) + " kHz";
        return {};
    }
    if (binding.contains("pression.")) {
        const auto suffix = binding.fromLastOccurrenceOf("pression.", false, false);
        if (suffix == "depth" || suffix == "time" || suffix == "upward" || suffix == "downward")
            return percent(value);
        if (suffix == "solo")
            return pick(value, { L("Tout", "All"), L("Graves", "Low"), L("Médiums", "Mid"), L("Aigus", "High") });
        if (suffix.endsWith("Ratio"))
            return value >= 99.5 ? juce::String::fromUTF8("\xe2\x88\x9e:1") : juce::String(value, value < 10.0 ? 2 : 1) + ":1";
        if (suffix.endsWith("Threshold"))
            return juce::String(value, 1) + " dB";
        if (suffix.startsWith("cross"))
            return value >= 1'000.0 ? juce::String(value / 1'000.0, 2) + " kHz" : juce::String(juce::roundToInt(value)) + " Hz";
        return {};
    }
    if (binding.contains("fournaise.")) {
        const auto suffix = binding.fromLastOccurrenceOf("fournaise.", false, false);
        if (suffix == "style")
            return fournaiseName(whole(static_cast<float>(value), engine::mix::fournaiseStyleCount));
        if (suffix == "punish" || suffix == "auto" || suffix == "thump" || suffix == "steep")
            return onOff(value);
        if (suffix == "oversampling")
            return pick(value, { "1x", "2x", "4x" });
        if (suffix == "drive")
            return knob(value);
        if (suffix == "tone")
            return std::abs(value) < 0.005 ? ui::Ls("Neutre", "Flat")
                                           : (value > 0.0 ? ui::Ls("Clair ", "Bright ") : ui::Ls("Sombre ", "Dark ")) + percent(std::abs(value));
        if (suffix == "mix")
            return percent(value);
        if (suffix == "lowCut")
            return value <= 20.5 ? off("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix == "highCut")
            return value >= 19'900.0 ? off("Désactivé", "Off") : juce::String(value / 1'000.0, 1) + " kHz";
        return {};
    }
    if (binding.contains("echoband.")) {
        const auto suffix = binding.fromLastOccurrenceOf("echoband.", false, false);
        if (suffix == "mode")
            return echoModeName(whole(static_cast<float>(value), engine::mix::echoModeCount));
        if (suffix == "sync") {
            const auto* division = engine::noteDivisionFor(static_cast<float>(value));
            return division == nullptr ? ui::Ls("Temps libre", "Free time") : juce::String(std::string(division->name));
        }
        if (suffix == "intensity" || suffix == "echo" || suffix == "reverb" || suffix == "wow" || suffix == "mix" || suffix == "age"
            || suffix == "width")
            return percent(value);
        if (suffix == "time")
            return juce::String(juce::roundToInt(value)) + " ms";
        return {};
    }
    if (binding.contains("multiband.")) {
        const auto suffix = binding.fromLastOccurrenceOf("multiband.", false, false);
        if (suffix == "amount" || suffix == "link")
            return percent(value);
        if (suffix.startsWith("cross"))
            return value >= 1'000.0 ? juce::String(value / 1'000.0, 2) + " kHz" : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix.endsWith("Mode"))
            return juce::String::fromUTF8(multibandModeName(whole(static_cast<float>(value), engine::mix::multibandModeCount), false));
        if (suffix.endsWith("Ratio"))
            return juce::String(value, value < 10.0 ? 2 : 1) + ":1";
        if (suffix.endsWith("On") || suffix.endsWith("Solo"))
            return onOff(value);
        return {};
    }
    if (binding.contains("velours.")) {
        const auto suffix = binding.fromLastOccurrenceOf("velours.", false, false);
        if (suffix == "sharpness" || suffix == "selectivity" || suffix == "mix" || suffix == "link")
            return percent(value);
        if (suffix == "mode")
            return pick(value, { L("Doux", "Soft"), L("Dur", "Hard") });
        if (suffix == "delta")
            return onOff(value);
        if (suffix.startsWith("focus"))
            return value >= 1'000.0 ? juce::String(value / 1'000.0, 1) + " kHz" : juce::String(juce::roundToInt(value)) + " Hz";
        return {};
    }
    if (binding.contains("patine.")) {
        const auto suffix = binding.fromLastOccurrenceOf("patine.", false, false);
        if (suffix == "noiseType")
            return pick(value, { L("Disque", "Record"), L("Bande", "Tape"), L("Secteur", "Mains") });
        if (suffix == "distortType")
            return pick(value, { L("Lampe", "Valve"), L("Bande", "Tape"), "Transistor" });
        if (suffix == "noiseFollow")
            return onOff(value);
        if (suffix == "tone")
            return std::abs(value) < 0.005 ? ui::Ls("Neutre", "Flat")
                                           : (value > 0.0 ? ui::Ls("Clair ", "Bright ") : ui::Ls("Sombre ", "Dark ")) + percent(std::abs(value));
        if (suffix == "output")
            return {};
        return percent(value);
    }
    if (binding.contains("halo.")) {
        const auto suffix = binding.fromLastOccurrenceOf("halo.", false, false);
        if (suffix == "mode")
            return haloModeName(whole(static_cast<float>(value), engine::halo::haloModeCount));
        if (suffix == "colour")
            return pick(value, { L("Années 70", "1970s"), L("Années 80", "1980s"), L("Maintenant", "Now") });
        if (suffix == "sync") {
            const auto* division = engine::noteDivisionFor(static_cast<float>(value));
            return division == nullptr ? ui::Ls("Libre", "Free") : juce::String(std::string(division->name));
        }
        if (suffix == "mix" || suffix == "size" || suffix == "attack" || suffix == "earlyDiffusion" || suffix == "lateDiffusion"
            || suffix == "modDepth" || suffix == "width")
            return percent(value);
        if (suffix == "bassMultiplier")
            return juce::String::fromUTF8("\xc3\x97") + juce::String(value, 2);
        if (suffix == "decay")
            return juce::String(value, value < 10.0 ? 2 : 1) + " s";
        if (suffix == "lowCut")
            return value <= 10.5 ? off("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix == "highCut" || suffix == "damping")
            return value >= 19'900.0 ? off("Désactivé", "Off") : juce::String(value / 1'000.0, 1) + " kHz";
        return {};
    }
    if (binding.contains("tranche.")) {
        const auto suffix = binding.fromLastOccurrenceOf("tranche.", false, false);
        if (suffix.endsWith("On"))
            return onOff(value);
        if (suffix == "exciterStyle")
            return pick(value, { L("Lampe", "Valve"), L("Bande", "Tape"), "Transistor" });
        if (suffix == "attack" || suffix == "sustain")
            return (value > 0.0 ? "+" : "") + juce::String(juce::roundToInt(value * 100.0)) + " %";
        if (suffix == "excite" || suffix == "width" || suffix == "mix")
            return percent(value);
        if (suffix == "compRatio")
            return juce::String(value, value < 10.0 ? 1 : 0) + ":1";
        if (suffix == "lowCut")
            return value <= 10.5 ? off("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        if (suffix == "highCut")
            return value >= 19'900.0 ? off("Désactivé", "Off") : juce::String(value / 1'000.0, 1) + " kHz";
        return {};
    }
    if (binding.contains("finition.")) {
        const auto suffix = binding.fromLastOccurrenceOf("finition.", false, false);
        if (suffix.endsWith("On"))
            return onOff(value);
        if (suffix == "target")
            return targetName(whole(static_cast<float>(value), engine::mix::loudnessTargetCount));
        if (suffix == "dynamics" || suffix == "excite" || suffix.endsWith("Drive") || suffix.endsWith("Width") || suffix == "width")
            return percent(value);
        if (suffix == "ratio")
            return juce::String(value, 1) + ":1";
        if (suffix == "lowCut")
            return value <= 10.5 ? off("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        return {};
    }
    if (binding.contains("sonometre.")) {
        const auto suffix = binding.fromLastOccurrenceOf("sonometre.", false, false);
        if (suffix == "target")
            return targetName(whole(static_cast<float>(value), engine::mix::loudnessTargetCount));
        if (suffix == "history")
            return value >= 60.0 ? juce::String(value / 60.0, 1) + " min" : juce::String(juce::roundToInt(value)) + " s";
        return {};
    }
    if (binding.contains("comppro.")) {
        if (is(parameter, "comppro.style"))
            return pick(value, { "Clean", "Classic", "Opto", L("Voix", "Vocal"), "Mastering", "Bus", "Punch", L("Pompage", "Pumping") });
        if (is(parameter, "comppro.channels"))
            return pick(value, { L("Stéréo", "Stereo"), "Mid", "Side" });
        if (is(parameter, "comppro.autoRelease") || is(parameter, "comppro.autoGain") || is(parameter, "comppro.audition"))
            return onOff(value);
        if (is(parameter, "comppro.range"))
            return value >= 59.95 ? ui::Ls("Illimitée", "Unlimited") : juce::String(value, 1) + " dB";
        if (is(parameter, "comppro.link"))
            return juce::String(juce::roundToInt(value * 100.0)) + " %";
        if (is(parameter, "comppro.sidechainLow"))
            return value >= 19'500.0 ? ui::Ls("Désactivé", "Off") : juce::String(value / 1'000.0, 1) + " kHz";
        return {};
    }
    if (binding.contains("revpro.")) {
        if (is(parameter, "revpro.space") || is(parameter, "revpro.distance") || is(parameter, "revpro.character")
            || is(parameter, "revpro.diffusion") || is(parameter, "revpro.width") || is(parameter, "revpro.mix"))
            return percent(value);
        if (is(parameter, "revpro.brightness"))
            return (value >= 0.0 ? "+" : "") + percent(value);
        if (is(parameter, "revpro.lowDecay") || is(parameter, "revpro.midDecay") || is(parameter, "revpro.highDecay"))
            return "x" + juce::String(value, 2);
        if (is(parameter, "revpro.decay"))
            return juce::String(value, value < 10.0 ? 2 : 1) + " s";
        if (is(parameter, "revpro.freeze"))
            return onOff(value);
        if (is(parameter, "revpro.lowCut"))
            return value <= 10.5 ? ui::Ls("Désactivé", "Off") : juce::String(juce::roundToInt(value)) + " Hz";
        if (is(parameter, "revpro.highCut"))
            return value >= 19'900.0 ? ui::Ls("Désactivé", "Off") : juce::String(value / 1'000.0, 1) + " kHz";
        if (is(parameter, "revpro.ducking"))
            return value <= 0.05 ? ui::Ls("Désactivé", "Off") : juce::String(value, 1) + " dB";
        return {};
    }
    if (binding.contains("limpro.")) {
        if (is(parameter, "limpro.style"))
            return pick(value, { L("Transparent", "Transparent"), L("Percutant", "Punchy"), L("Dynamique", "Dynamic"),
                                 L("Polyvalent", "Allround"), L("Agressif", "Aggressive"), L("Moderne", "Modern"), L("Sûr", "Safe"), "Bus" });
        if (is(parameter, "limpro.oversampling"))
            return pick(value, { "1x", "2x", "4x", "8x" });
        if (is(parameter, "limpro.dither"))
            return pick(value, { L("Sans", "Off"), "24 bits", "20 bits", "18 bits", "16 bits" });
        if (is(parameter, "limpro.shaping"))
            return pick(value, { L("Basique", "Basic"), L("Optimisée", "Optimized"), L("Pondérée", "Weighted") });
        if (is(parameter, "limpro.truePeak") || is(parameter, "limpro.dcFilter") || is(parameter, "limpro.unity"))
            return onOff(value);
        if (is(parameter, "limpro.linkTransients") || is(parameter, "limpro.linkRelease"))
            return percent(value);
        if (is(parameter, "limpro.target"))
            return juce::String(value, 1) + " LUFS";
        return {};
    }
    if (binding.contains("filtpro.")) {
        const auto suffix = binding.fromLastOccurrenceOf("filtpro.", false, false);
        if (suffix.endsWith("On"))
            return onOff(value);
        if (suffix.endsWith("Type"))
            return juce::String::fromUTF8(typeName(static_cast<int>(std::lround(value)), false));
        if (suffix.endsWith("Slope"))
            return pick(value, { "12 dB/oct", "24 dB/oct", "48 dB/oct" });
        if (suffix.endsWith("Resonance") || suffix == "mix")
            return percent(value);
        if (suffix.endsWith("Spread"))
            return (value >= 0.0 ? "+" : "") + juce::String(value, 2) + " oct";
        if (suffix == "routing")
            return pick(value, { L("Série", "Serial"), L("Parallèle", "Parallel"), L("Paires", "Pairs") });
        if (suffix.endsWith("Shape"))
            return pick(value, { L("Sinus", "Sine"), L("Triangle", "Triangle"), L("Scie montante", "Saw up"), L("Scie descendante", "Saw down"),
                                 L("Carré", "Square"), L("Aléatoire tenu", "Sample & hold"), L("Aléatoire lissé", "Smooth random") });
        if (suffix.endsWith("Sync")) {
            if (const auto* division = engine::noteDivisionFor(static_cast<float>(value)))
                return juce::String(std::string(division->name));
            return ui::Ls("Libre", "Free");
        }
        if (suffix.endsWith("Stereo"))
            return juce::String(juce::roundToInt(value * 360.0)) + juce::String::fromUTF8("\xc2\xb0");
        if (suffix.endsWith("Source"))
            return pick(value, { "\xe2\x80\x94", "LFO 1", "LFO 2", L("Suiveur", "Follower") });
        if (suffix.endsWith("Destination"))
            return pick(value, { "\xe2\x80\x94", L("F1 coupure", "F1 cutoff"), L("F1 résonance", "F1 resonance"), L("F2 coupure", "F2 cutoff"),
                                 L("F2 résonance", "F2 resonance"), L("F3 coupure", "F3 cutoff"), L("F3 résonance", "F3 resonance"),
                                 L("F4 coupure", "F4 cutoff"), L("F4 résonance", "F4 resonance"), L("Toutes les coupures", "All cutoffs"),
                                 L("F1 saturation", "F1 drive"), L("F2 saturation", "F2 drive"), L("Mélange", "Mix"), L("Sortie", "Output") });
        if (suffix.endsWith("Amount"))
            return (value >= 0.0 ? "+" : "") + percent(value);
        return {};
    }
    return {};
}

bool studioStepped(const Param& parameter)
{
    static const char* stepped[] { "comppro.style", "comppro.channels", "comppro.autoRelease", "comppro.autoGain", "comppro.audition",
                                   "revpro.freeze", "revpro.sync", "limpro.style", "limpro.truePeak", "limpro.oversampling",
                                   "limpro.dither", "limpro.shaping", "limpro.dcFilter", "limpro.unity", "filtpro.routing",
                                   "amp.model", "amp.bright", "amp.pedal", "amp.oversampling", "cab.model", "cab.micA", "cab.micB",
                                   "cab.micBOn", "cab.polarityB", "cab.align", "cab.stereo", "micsim.model", "micsim.pattern",
                                   "micsim.preamp", "amp.channel", "amp.modelB", "amp.brightB", "pression.solo",
                                   "fournaise.style", "fournaise.punish", "fournaise.auto", "fournaise.thump", "fournaise.steep",
                                   "fournaise.oversampling", "echoband.mode", "echoband.sync", "velours.mode", "velours.delta",
                                   "patine.noiseType", "patine.distortType", "patine.noiseFollow", "sonometre.target",
                                   "tranche.gateOn", "tranche.eqOn", "tranche.compOn", "tranche.transientOn", "tranche.exciterOn",
                                   "tranche.exciterStyle", "finition.eqOn", "finition.dynamicsOn", "finition.exciterOn",
                                   "finition.imagerOn", "finition.maximizerOn", "finition.target", "halo.mode", "halo.colour",
                                   "halo.sync" };
    for (const auto* suffix : stepped)
        if (parameter.binding.endsWith(suffix))
            return true;
    if (addedStepped(parameter.binding))
        return true;
    if (parameter.binding.contains("multiband.band")
        && (parameter.binding.endsWith("Mode") || parameter.binding.endsWith("On") || parameter.binding.endsWith("Solo")))
        return true;
    if (!parameter.binding.contains("filtpro."))
        return false;
    const auto suffix = parameter.binding.fromLastOccurrenceOf("filtpro.", false, false);
    return suffix.endsWith("On") || suffix.endsWith("Type") || suffix.endsWith("Slope") || suffix.endsWith("Shape")
           || suffix.endsWith("Sync") || suffix.endsWith("Source") || suffix.endsWith("Destination");
}

float addedPaneFactor(const PaneKind kind)
{
    switch (kind) {
    case PaneKind::vocoder: return 2.3f;
    case PaneKind::deEsser: return 2.3f;
    case PaneKind::tuner: return 2.3f;
    case PaneKind::harmonie: return 2.3f;
    case PaneKind::justesse: return 2.3f;
    case PaneKind::empreinte: return 2.3f;
    case PaneKind::givre: return 2.3f;
    case PaneKind::grains: return 2.3f;
    case PaneKind::saccade: return 2.3f;
    case PaneKind::boucleur: return 2.3f;
    case PaneKind::pompe: return 2.3f;
    case PaneKind::decaleur: return 2.3f;
    case PaneKind::anneau: return 2.3f;
    case PaneKind::rotor: return 2.3f;
    case PaneKind::resonateurs: return 2.3f;
    case PaneKind::corps: return 2.3f;
    case PaneKind::peigne: return 2.3f;
    case PaneKind::utilitaire: return 2.3f;
    case PaneKind::stereoscope: return 2.3f;
    case PaneKind::arpege: return 2.3f;
    case PaneKind::accords: return 2.3f;
    case PaneKind::gamme: return 2.3f;
    case PaneKind::echoNotes: return 2.3f;
    case PaneKind::alea: return 2.3f;
    case PaneKind::duree: return 2.3f;
    case PaneKind::sequenceur: return 2.3f;
    case PaneKind::roulement: return 2.3f;
    default: return 2.0f;
    }
}

} // namespace cydaw::v2::visual
