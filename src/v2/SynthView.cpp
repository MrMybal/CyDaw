#include "SynthView.h"

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
constexpr float paneKnob = 26.0f;

// A section's colour (stored as 0xAARRGGBB) as ImGui draws it.
ImU32 fromArgb(const std::uint32_t argb)
{
    return IM_COL32((argb >> 16) & 0xffu, (argb >> 8) & 0xffu, argb & 0xffu, (argb >> 24) & 0xffu);
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

SynthView::SynthView(Studio& studio, UiState& state) : studio_(studio), state_(state), presets_(studio, state) {}

bool SynthView::handles(const juce::String& typeKey)
{
    return synth::modelForType(typeKey.toStdString()) != synth::Model::none;
}

float SynthView::paneWidth()
{
    return dp(360.0f);
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
    return true;
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
    case synth::Kind::choice: return ImVec2(dp(item.width > 0.0f ? item.width : 104.0f), ImGui::GetFrameHeight());
    case synth::Kind::toggle: {
        const auto* label = textOf(item.french, item.english);
        const auto text = ImGui::CalcTextSize(label != nullptr ? label : "").x;
        return ImVec2(std::max(dp(62.0f), text + dp(18.0f)), ImGui::GetFrameHeight());
    }
    default: return ImVec2(dp(diameter + 14.0f), dp(diameter) + ui::currentFontBase() * 0.78f + dp(3.0f));
    }
}

void SynthView::control(const Control& item, const float diameter)
{
    const auto index = indexOf(item.key);
    if (index < 0)
        return;
    const auto enabled = met(item);
    if (!enabled && item.hide)
        return;
    if (!enabled)
        ImGui::BeginDisabled();
    const auto* label = textOf(item.french, item.english);
    switch (synth::parameter(card_.model, index).kind) {
    case synth::Kind::choice: choice(index, controlSize(item, diameter).x); break;
    case synth::Kind::toggle: toggle(index, label != nullptr ? label : "", controlSize(item, diameter).x); break;
    default: knob(index, label, diameter); break;
    }
    if (!enabled)
        ImGui::EndDisabled();
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
    style.colour = colourOf(index);
    const auto origin = ImGui::GetCursorScreenPos();
    const auto changed = ui::knob("##knob", &normalised, 0.0f, 1.0f, defaultNormalised, style);
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
    // Its name, its value while touched.
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
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", (ui::french() ? parameter.french : parameter.english).c_str());
    ImGui::PopID();
}

void SynthView::toggle(const int index, const char* label, const float width)
{
    const auto& parameter = synth::parameter(card_.model, index);
    ImGui::PushID(index);
    auto on = value(index) >= 0.5f;
    if (ui::toggle(label, &on, colourOf(index), ImVec2(width, 0.0f), (ui::french() ? parameter.french : parameter.english).c_str()))
        set(index, on ? 1.0f : 0.0f);
    ImGui::PopID();
}

float SynthView::flow(const std::vector<Control>& controls, const ImVec2 origin, const float width, const float diameter, const bool draw)
{
    const auto gap = dp(6.0f);
    auto x = origin.x;
    auto y = origin.y;
    auto lineHeight = 0.0f;
    for (const auto& item : controls) {
        const auto size = controlSize(item, diameter);
        if (size.x <= 0.0f)
            continue;
        if (x > origin.x && (item.newLine || x + size.x > origin.x + width)) {
            x = origin.x;
            y += lineHeight + gap;
            lineHeight = 0.0f;
        }
        if (draw) {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            control(item, diameter);
        }
        x += size.x + gap;
        lineHeight = std::max(lineHeight, size.y);
    }
    return y + lineHeight - origin.y;
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

void SynthView::panel(const synth_layout::Panel& item, const float width, const float height)
{
    const auto& info = synth::info(card_.model);
    const auto colour = item.section >= 0 && item.section < static_cast<int>(info.sections.size())
                            ? fromArgb(info.sections[static_cast<std::size_t>(item.section)].colour)
                            : synth_layout::layoutFor(card_.model).colour;
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

namespace {

// One of Colosse's filters at `hz` (its kinds as its engine has them).
std::complex<double> colosseStage(const int type, const double cutoff, const double resonance, const double hz)
{
    using complex = std::complex<double>;
    const complex s(0.0, hz / std::max(1.0, cutoff));
    const auto damping = std::max(0.02, 2.0 - 1.98 * resonance);
    switch (type) {
    case 0: {
        const auto k = 4.2 * resonance;
        const auto pole = 1.0 + s;
        return (1.0 + 0.6 * k) / (pole * pole * pole * pole + k);
    }
    case 1: return 1.0 / (s * s + damping * s + 1.0);
    case 2: return damping * s / (s * s + damping * s + 1.0);
    case 3: return s * s / (s * s + damping * s + 1.0);
    case 4: {
        const auto feedback = 0.97 * resonance;
        const auto delay = 1.0 / std::max(40.0, cutoff);
        const auto z = std::exp(complex(0.0, -2.0 * kPi * hz * delay));
        return (1.0 - 0.6 * feedback) / (1.0 - feedback * z);
    }
    case 5: {
        static constexpr std::array<std::array<double, 3>, 5> vowels { { { 800.0, 1'150.0, 2'900.0 },
                                                                        { 400.0, 1'900.0, 2'700.0 },
                                                                        { 280.0, 2'200.0, 2'950.0 },
                                                                        { 450.0, 800.0, 2'830.0 },
                                                                        { 325.0, 700.0, 2'530.0 } } };
        static constexpr std::array<double, 3> gains { 1.0, 0.7, 0.35 };
        const auto t = std::clamp(std::log(std::max(20.0, cutoff) / 20.0) / std::log(1'000.0), 0.0, 1.0) * 4.0;
        const auto lower = std::min(static_cast<int>(t), 3);
        const auto mix = t - static_cast<double>(lower);
        const auto width = std::max(0.04, 0.5 - 0.44 * resonance);
        complex sum;
        for (std::size_t at = 0; at < 3; ++at) {
            const auto centre = vowels[static_cast<std::size_t>(lower)][at]
                                + (vowels[static_cast<std::size_t>(lower + 1)][at] - vowels[static_cast<std::size_t>(lower)][at]) * mix;
            const complex f(0.0, hz / centre);
            sum += gains[at] * width * f / (f * f + width * f + 1.0);
        }
        return sum * 1.6;
    }
    default: {
        const auto screaming = std::max(0.02, 2.0 - 1.98 * std::min(0.98, 0.55 + 0.45 * resonance));
        return (0.5 + resonance) * s / (s * s + screaming * s + 1.0);
    }
    }
}

} // namespace

double SynthView::filterDb(const double hz) const
{
    using complex = std::complex<double>;
    complex response;
    if (indexOf("f.type") >= 0) {
        // One filter of the family's kinds.
        response = colosseStage(static_cast<int>(std::lround(valueOf("f.type"))), static_cast<double>(valueOf("f.cutoff")),
                                std::clamp(static_cast<double>(valueOf("f.resonance")), 0.0, 1.0), hz);
        return 20.0 * std::log10(std::max(1.0e-9, std::abs(response)));
    }
    if (indexOf("f1.type") >= 0) {
        // Two filters, in series or side by side, blended.
        const auto stage = [this, hz](const char* type, const char* cutoff, const char* resonance) {
            return colosseStage(static_cast<int>(std::lround(valueOf(type))), static_cast<double>(valueOf(cutoff)),
                                std::clamp(static_cast<double>(valueOf(resonance)), 0.0, 1.0), hz);
        };
        const auto one = stage("f1.type", "f1.cutoff", "f1.resonance");
        const auto two = stage("f2.type", "f2.cutoff", "f2.resonance");
        const auto parallel = valueOf("filter.routing") >= 0.5f;
        if (indexOf("filter.blend") >= 0) {
            // The second dosed by its blend.
            const auto blend = std::clamp(static_cast<double>(valueOf("filter.blend")), 0.0, 1.0);
            response = parallel ? one + (two - one) * blend : one + (one * two - one) * blend;
        } else {
            // Side by side, each as much as the engines send it; in series, both.
            const auto levelA = static_cast<double>(valueOf("a.level"));
            const auto levelB = static_cast<double>(valueOf("b.level"));
            const auto sent = (levelA * valueOf("a.send") + levelB * valueOf("b.send")) / std::max(1.0e-6, levelA + levelB);
            response = parallel ? one + (two - one) * sent : one * two;
        }
        return 20.0 * std::log10(std::max(1.0e-9, std::abs(response)));
    }
    const auto cutoff = std::max(1.0, static_cast<double>(valueOf("filter.cutoff")));
    const auto resonance = std::clamp(static_cast<double>(valueOf("filter.resonance")), 0.0, 1.0);
    const auto model = indexOf("filter.model") >= 0 ? static_cast<int>(std::lround(valueOf("filter.model"))) : 2;
    const auto mode = indexOf("filter.mode") >= 0 ? static_cast<int>(std::lround(valueOf("filter.mode"))) : 0;
    const complex s(0.0, hz / cutoff);
    if (model == 0 || model == 1) {
        // Four poles in a loop: low-pass, the lows given back as it resonates.
        const auto k = (model == 0 ? 4.2 : 4.6) * resonance;
        const auto makeUp = 1.0 + (model == 0 ? 0.6 : 0.35) * k;
        const auto pole = 1.0 + s;
        response = makeUp / (pole * pole * pole * pole + k);
    } else {
        // Two poles, state-variable: low, band or high.
        const auto damping = std::max(0.02, 2.0 - 2.0 * resonance * 0.99);
        const auto denominator = s * s + damping * s + 1.0;
        response = mode == 1 ? s / denominator : mode == 2 ? s * s / denominator : 1.0 / denominator;
    }
    if (const auto hp = indexOf("hpf") >= 0 ? static_cast<double>(valueOf("hpf")) : 0.0; hp > 20.5) {
        const complex h(0.0, hz / hp);
        response *= h / (1.0 + h);
    }
    return 20.0 * std::log10(std::max(1.0e-9, std::abs(response)));
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
    const auto hovered = ImGui::IsItemHovered();
    const auto& io = ImGui::GetIO();
    auto nearest = 0;
    if (handleCount == 2) {
        const auto distance = [&io](const ImVec2 at) { return std::hypot(io.MousePos.x - at.x, io.MousePos.y - at.y); };
        nearest = distance(points[1]) < distance(points[0]) ? 1 : 0;
    }
    if (ImGui::IsItemActivated()) {
        dragging_ = id;
        draggingPoint_ = nearest;
    }
    if (!ImGui::IsItemActive() && dragging_ == id)
        dragging_.clear();
    const auto chosen = dragging_ == id ? std::clamp(draggingPoint_, 0, handleCount - 1) : nearest;
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
    if (!interactive)
        return;
    // The points: the attack's top, the decay's end (with the sustain), the release's end.
    const ImVec2 points[] = { ImVec2(x1, yOf(1.0)), ImVec2(x2, yOf(level)), ImVec2(x4, yOf(0.0)) };
    const auto id = std::string("##envelope.") + (prefix != nullptr ? prefix : "");
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton(id.c_str(), size);
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
        dragging_ = id;
        draggingPoint_ = nearest;
    }
    if (!ImGui::IsItemActive() && dragging_ == id)
        dragging_.clear();
    const auto mine = dragging_ == id;
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
        const auto y = shape >= 4 ? held[static_cast<int>(phase * 4.0) % 8] : lfoWave(shape, phase);
        line.emplace_back(left + static_cast<float>(at), middle - static_cast<float>(y) * half);
    }
    list->AddPolyline(line.data(), static_cast<int>(line.size()), colour, dp(1.6f));
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
    if (!bind(track, graph, nodeId))
        return;
    (void) colour;
    const auto& layout = synth_layout::layoutFor(card_.model);
    const auto& info = synth::info(card_.model);
    ImGui::PushID(static_cast<int>(nodeId) + 41'000);
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginGroup();
    const auto open = juce::String(ui::french() ? "Ouvrir " : "Open ") + juce::String::fromUTF8((ui::french() ? info.french : info.english).c_str());
    if (ui::primaryButton(open.toRawUTF8(), ImVec2(dp(120.0f), 0.0f)))
        openEditor(track, nodeId);
    if (const auto index = indexOf(layout.paneChoice); index >= 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", synth::displayValue(card_.model, index, value(index), ui::french()).c_str());
    }
    const auto knobsHeight = dp(paneKnob) + ui::currentFontBase() * 0.78f + dp(3.0f);
    const auto pictureOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 pictureSize(size.x, std::max(dp(36.0f), size.y - (pictureOrigin.y - origin.y) - knobsHeight - dp(6.0f)));
    drawPicture(layout.panePicture, layout.paneKey, nullptr, pictureOrigin, pictureSize, false);
    ImGui::SetCursorScreenPos(ImVec2(pictureOrigin.x, pictureOrigin.y + pictureSize.y + dp(4.0f)));
    for (std::size_t at = 0; at < layout.pane.size(); ++at) {
        if (at > 0)
            ImGui::SameLine(0.0f, dp(2.0f));
        control(layout.pane[at], paneKnob);
    }
    ImGui::EndGroup();
    ImGui::PopID();
}

void SynthView::openEditor(const TrackRef& track, const std::uint64_t nodeId, const bool detached)
{
    editor_ = Target { track, nodeId };
    focusEditor_ = true;
    if (detached)
        ui::setDetached(editorWindowId, true);
}

void SynthView::drawEditor()
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
    const auto& layout = synth_layout::layoutFor(card_.model);
    const auto& info = synth::info(card_.model);
    ui::beforeDetachable(editorWindowId);
    ImGui::SetNextWindowSize(ImVec2(dp(1'180.0f), dp(780.0f)), ImGuiCond_FirstUseEver);
    if (focusEditor_) {
        ImGui::SetNextWindowFocus();
        focusEditor_ = false;
    }
    auto open = true;
    const auto title = juce::String::fromUTF8((ui::french() ? info.french : info.english).c_str()) + juce::String::fromUTF8(" \xe2\x80\x94 ")
                       + studio_.trackName(target.track) + editorWindowId;
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, theme::mix(layout.colour, p.bg2 | 0xff000000u, 0.65f));
    const auto visible = ImGui::Begin(title.toRawUTF8(), &open, ImGuiWindowFlags_NoCollapse);
    ImGui::PopStyleColor();
    if (visible) {
        // Out of the studio's window, as a plug-in's (another screen), and back.
        {
            const auto headerY = ImGui::GetCursorPosY();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - dp(24.0f));
            (void) ui::detachButton(editorWindowId, ImVec2(dp(24.0f), dp(22.0f)));
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
    if (!open)
        editor_.reset();
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
