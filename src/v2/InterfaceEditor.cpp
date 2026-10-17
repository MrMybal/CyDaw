#include "InterfaceEditor.h"
#include "InstrumentPanels.h"
#include "CustomInterface.h"
#include "DeviceVisuals.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <numbers>
#include <set>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

using Kind = app::ModuleControlKind;

// The controls the palette offers, their size in grid cells, and how many
// published settings each binds (-1: an EQ curve's triples).
struct KindInfo {
    Kind kind;
    const char* fr;
    const char* en;
    int columns;
    int rows;
    int bindings;
};
constexpr KindInfo kinds[] = {
    { Kind::knob, "Potard", "Knob", 3, 4, 1 },
    { Kind::fader, "Fader", "Fader", 2, 6, 1 },
    { Kind::toggle, "Interrupteur", "Switch", 3, 2, 1 },
    { Kind::selector, "Sélecteur", "Selector", 4, 2, 1 },
    { Kind::xyPad, "Pad XY", "XY pad", 6, 6, 2 },
    { Kind::valueDisplay, "Afficheur de valeur", "Value display", 4, 2, 1 },
    { Kind::text, "Texte", "Text", 6, 1, 0 },
    { Kind::group, "Cadre", "Frame", 8, 6, 0 },
    { Kind::filterResponse, "Courbe de filtre", "Filter curve", 8, 5, 2 },
    { Kind::dynamicsResponse, "Courbe de dynamique", "Dynamics curve", 6, 5, 2 },
    { Kind::eqCurve, "Courbe d'égaliseur", "EQ curve", 10, 5, -1 },
    { Kind::envelope, "Enveloppe ADSR", "ADSR envelope", 8, 5, 4 },
    { Kind::waveform, "Forme d'onde", "Waveform", 8, 4, 0 },
    { Kind::spectrum, "Spectre", "Spectrum", 8, 4, 0 },
    { Kind::levelMeter, "Vumètre", "Level meter", 1, 6, 0 },
    { Kind::wavetable, "Oscillateur / table", "Oscillator / wavetable", 8, 5, 1 },
    { Kind::custom, "Composant C++ ImGui", "C++ ImGui component", 8, 5, 0 },
};

const KindInfo* infoOf(const Kind kind)
{
    for (const auto& info : kinds)
        if (info.kind == kind)
            return &info;
    return nullptr;
}

bool isPort(const Kind kind)
{
    return kind == Kind::inputPort || kind == Kind::outputPort;
}

constexpr float kDefaultWidth = 720.0f;
constexpr float kDefaultHeight = 420.0f;

} // namespace

InterfaceEditor::InterfaceEditor(DeviceHost& studio, UiState& state) : studio_(studio), state_(state) {}

void InterfaceEditor::report(const juce::String& message)
{
    error_ = message;
    errorTime_ = ImGui::GetTime();
}

void InterfaceEditor::pull(GraphSlot& slot, const bool force)
{
    if (!force && drag_ != Drag::none)
        return;
    if (force || slot_ != &slot || revision_ != slot.revision()) {
        interface_ = slot.graph().editorInterface();
        analysis_ = &slot.graph().editorAnalysis();
        if(deviceLayout_) {
            std::swap(interface_.controls,interface_.deviceControls);
            std::swap(interface_.windowWidth,interface_.deviceWidth);
            std::swap(interface_.windowHeight,interface_.deviceHeight);
            std::swap(interface_.windowDesign,interface_.deviceDesign);
        }
        if (slot_ != &slot)
            selected_ = 0;
        slot_ = &slot;
        revision_ = slot.revision();
        if (selected_ != 0 && find(selected_) == nullptr)
            selected_ = 0;
    }
}

bool InterfaceEditor::push(GraphSlot& slot, const bool commit)
{
    auto canonical=interface_;
    if(deviceLayout_) {
        canonical.deviceDesigned=true;
        std::swap(canonical.controls,canonical.deviceControls);
        std::swap(canonical.windowWidth,canonical.deviceWidth);
        std::swap(canonical.windowHeight,canonical.deviceHeight);
        std::swap(canonical.windowDesign,canonical.deviceDesign);
    } else canonical.designed=true;
    if (const auto result = slot.graph().editorSetInterface(canonical, commit); result.failed()) {
        report(result.getErrorMessage());
        pull(slot, true);
        return false;
    }
    if (commit) {
        slot.touch();
        revision_ = slot.revision();
    }
    return true;
}

InterfaceEditor::Control* InterfaceEditor::find(const std::uint64_t id)
{
    for (auto& control : interface_.controls)
        if (control.id == id)
            return &control;
    return nullptr;
}

const app::ModulePublishedParameter* InterfaceEditor::parameter(const juce::String& key) const
{
    for (const auto& item : interface_.parameters)
        if (item.key == key)
            return &item;
    return nullptr;
}

namespace {
// Frequencies and times turn on a logarithmic scale, as every knob of CyDAW.
bool logarithmic(const app::ModulePublishedParameter& item)
{
    return item.minimum > 0.0 && (item.unit == "Hz" || item.unit == "ms" || item.unit == "s");
}
} // namespace

double InterfaceEditor::value01(const juce::String& key) const
{
    const auto* item = parameter(key);
    if (item == nullptr || item->maximum <= item->minimum)
        return 0.0;
    if (logarithmic(*item))
        return std::clamp(std::log(std::max(item->value, item->minimum) / item->minimum) / std::log(item->maximum / item->minimum), 0.0,
                          1.0);
    return std::clamp((item->value - item->minimum) / (item->maximum - item->minimum), 0.0, 1.0);
}

void InterfaceEditor::setValue01(GraphSlot& slot, const juce::String& key, const double position)
{
    for (auto& item : interface_.parameters)
        if (item.key == key) {
            const auto at = std::clamp(position, 0.0, 1.0);
            item.value = logarithmic(item) ? item.minimum * std::pow(item.maximum / item.minimum, at)
                                           : item.minimum + at * (item.maximum - item.minimum);
            (void) slot.graph().editorSetParameter(key, item.value);
        }
}

ImVec2 InterfaceEditor::windowSize() const
{
    return { interface_.windowWidth != 0 ? static_cast<float>(interface_.windowWidth) : kDefaultWidth,
             interface_.windowHeight != 0 ? static_cast<float>(interface_.windowHeight) : kDefaultHeight };
}

float InterfaceEditor::snap(const float value, const int cells) const
{
    if (!interface_.snapToGrid || cells <= 0)
        return value;
    return std::round(value * static_cast<float>(cells)) / static_cast<float>(cells);
}

std::uint64_t InterfaceEditor::hit(const ImVec2 screen) const
{
    const auto size = ImVec2(canvasMax_.x - canvasMin_.x, canvasMax_.y - canvasMin_.y);
    for (auto index = interface_.controls.size(); index-- > 0;) {
        const auto& control = interface_.controls[index];
        if (isPort(control.kind) || !visible(control))
            continue;
        const ImVec2 min(canvasMin_.x + control.x * size.x, canvasMin_.y + control.y * size.y);
        const ImVec2 max(min.x + control.width * size.x, min.y + control.height * size.y);
        if (screen.x >= min.x && screen.x <= max.x && screen.y >= min.y && screen.y <= max.y)
            return control.id;
    }
    return 0;
}

std::uint64_t InterfaceEditor::add(GraphSlot& slot, const Kind kind, const ImVec2 at, const juce::String& binding)
{
    const auto* info = infoOf(kind);
    if (info == nullptr)
        return 0;
    Control control;
    control.id = interface_.nextControlId++;
    control.kind = kind;
    const auto columns = static_cast<float>(interface_.gridColumns);
    const auto rows = static_cast<float>(interface_.gridRows);
    control.width = std::min(1.0f, static_cast<float>(info->columns) / columns);
    control.height = std::min(1.0f, static_cast<float>(info->rows) / rows);
    control.x = std::clamp(snap(at.x, interface_.gridColumns), 0.0f, 1.0f - control.width);
    control.y = std::clamp(snap(at.y, interface_.gridRows), 0.0f, 1.0f - control.height);

    // Bindings among the published settings: the one asked for, else one no
    // control shows yet, else any; the curves look for their own words.
    std::set<juce::String> used;
    for (const auto& other : interface_.controls)
        used.insert(other.parameterBinding);
    const auto lower = [](const juce::String& text) { return text.toLowerCase(); };
    const auto pick = [&](const std::initializer_list<const char*> words, const std::set<juce::String>& except) -> juce::String {
        for (const auto& item : interface_.parameters) {
            if (except.contains(item.key))
                continue;
            if (words.size() == 0)
                return item.key;
            for (const auto* word : words)
                if (lower(item.key).contains(word))
                    return item.key;
        }
        return {};
    };
    std::set<juce::String> none;
    if (info->bindings == 1) {
        control.parameterBinding = binding.isNotEmpty() ? binding : pick({}, used);
        if (control.parameterBinding.isEmpty())
            control.parameterBinding = pick({}, none);
    } else if (info->bindings == 2) {
        const auto first = kind == Kind::filterResponse     ? pick({ "cutoff", "frequency" }, none)
                           : kind == Kind::dynamicsResponse ? pick({ "threshold" }, none)
                                                            : juce::String();
        control.parameterBinding = binding.isNotEmpty() ? binding : first.isNotEmpty() ? first : pick({}, used);
        if (control.parameterBinding.isEmpty())
            control.parameterBinding = pick({}, none);
        std::set<juce::String> taken { control.parameterBinding };
        const auto second = kind == Kind::filterResponse     ? pick({ "resonance", ".q" }, taken)
                            : kind == Kind::dynamicsResponse ? pick({ "ratio" }, taken)
                                                             : juce::String();
        control.secondaryParameterBinding = second.isNotEmpty() ? second : pick({}, taken);
    } else if (info->bindings == 4) {
        control.parameterBinding = pick({ "attack" }, none);
        control.secondaryParameterBinding = pick({ "decay" }, none);
        control.tertiaryParameterBinding = pick({ "sustain" }, none);
        control.quaternaryParameterBinding = pick({ "release" }, none);
    } else if (info->bindings < 0) {
        // Each band: its frequency, gain and Q.
        for (const auto& item : interface_.parameters) {
            juce::String base;
            juce::String gain;
            juce::String q;
            if (item.key.endsWith("eqBand.frequency")) {
                base = item.key.dropLastCharacters(9);
                gain = base + "gain";
                q = base + "q";
            } else if (item.key.contains("eqpro.b") && item.key.endsWith("Frequency")) {
                base = item.key.dropLastCharacters(9);
                gain = base + "Gain";
                q = base + "Q";
            } else {
                continue;
            }
            if (parameter(gain) != nullptr && parameter(q) != nullptr) {
                control.extraParameterBindings.push_back(item.key);
                control.extraParameterBindings.push_back(gain);
                control.extraParameterBindings.push_back(q);
            }
        }
    }
    if (const auto* item = parameter(control.parameterBinding); item != nullptr && info->bindings == 1)
        control.label = item->label;
    else
        control.label = ui::Ls(info->fr, info->en);
    if (kind == Kind::text)
        control.label = slot.graph().editorGraphName().isNotEmpty() ? slot.graph().editorGraphName() : ui::Ls("Texte", "Text");
    if (kind == Kind::group) control.label = {};
    if (page_.isNotEmpty()) { control.design=new juce::DynamicObject;control.design.getDynamicObject()->setProperty("page",page_); }
    if (kind == Kind::custom) {
        control.design = control.design.isObject()?control.design:juce::var(new juce::DynamicObject);
        control.design.getDynamicObject()->setProperty("customSource",CustomInterface::example());
    }

    const auto needs = [&]() -> juce::String {
        if (info->bindings >= 1 && control.parameterBinding.isEmpty())
            return ui::Ls("Il faut un réglage publié : ajoutez des cartes au graphe.",
                          "It needs a published setting: add cards to the graph.");
        if (info->bindings == 2 && (control.secondaryParameterBinding.isEmpty()
                                    || control.secondaryParameterBinding == control.parameterBinding))
            return ui::Ls("Il faut deux réglages publiés différents.", "It needs two different published settings.");
        if (info->bindings == 4) {
            const std::set<juce::String> four { control.parameterBinding, control.secondaryParameterBinding,
                                                control.tertiaryParameterBinding, control.quaternaryParameterBinding };
            if (four.size() != 4 || four.contains({}))
                return ui::Ls("Il faut une enveloppe dans le graphe (attaque, déclin, maintien, relâchement).",
                              "It needs an envelope in the graph (attack, decay, sustain, release).");
        }
        if (info->bindings < 0 && control.extraParameterBindings.empty())
            return ui::Ls("Il faut un égaliseur dans le graphe (bande EQ ou Égaliseur Pro).",
                          "It needs an equaliser in the graph (EQ band or Parametric EQ).");
        return {};
    }();
    if (needs.isNotEmpty()) {
        --interface_.nextControlId;
        report(needs);
        return 0;
    }
    interface_.controls.push_back(control);
    if (!push(slot, true))
        return 0;
    selected_ = control.id; selection_={selected_};
    return control.id;
}

void InterfaceEditor::autoLayout(GraphSlot& slot)
{
    std::erase_if(interface_.controls, [](const Control& control) { return !isPort(control.kind); });
    std::vector<const app::ModulePublishedParameter*> shown;
    for (const auto& item : interface_.parameters)
        if (item.showInDevice || item.publishToHost)
            shown.push_back(&item);
    const auto columns = static_cast<float>(interface_.gridColumns);
    const auto rows = static_cast<float>(interface_.gridRows);
    Control title;
    title.id = interface_.nextControlId++;
    title.kind = Kind::text;
    title.label = slot.graph().editorGraphName();
    title.x = 1.0f / columns;
    title.y = 0.0f;
    title.width = std::min(1.0f - title.x, 12.0f / columns);
    title.height = 2.0f / rows;
    interface_.controls.push_back(title);
    const auto perRow = std::max(1, std::min(static_cast<int>(shown.size()), static_cast<int>(interface_.gridColumns) / 3));
    const auto rowCount = (static_cast<int>(shown.size()) + perRow - 1) / std::max(1, perRow);
    const auto knobRows = std::max(2.0f, std::min(4.0f, (rows - 3.0f) / static_cast<float>(std::max(1, rowCount))));
    for (std::size_t index = 0; index < shown.size(); ++index) {
        const auto row = static_cast<int>(index) / perRow;
        const auto column = static_cast<int>(index) % perRow;
        Control knob;
        knob.id = interface_.nextControlId++;
        knob.kind = Kind::knob;
        knob.parameterBinding = shown[index]->key;
        knob.label = shown[index]->label;
        knob.width = 3.0f / columns;
        knob.height = std::floor(knobRows) / rows;
        knob.x = std::min(1.0f - knob.width, (1.0f + static_cast<float>(column) * 3.0f) / columns);
        knob.y = std::min(1.0f - knob.height, (2.5f + static_cast<float>(row) * std::floor(knobRows)) / rows);
        knob.y = snap(knob.y, interface_.gridRows);
        if (knob.y + knob.height > 1.0f)
            break;
        interface_.controls.push_back(knob);
    }
    selected_ = 0;
    (void) push(slot, true);
}

void InterfaceEditor::drawControl(ImDrawList* list, const Control& control, const ImVec2 min, const ImVec2 max) const
{
    auto p = theme::palette();
    const auto color=[&](const char* key,ImU32 fallback) {
        const auto value=control.design.getProperty(key,interface_.windowDesign.getProperty(key,{})).toString();if(value.isEmpty())return fallback;
        const auto c=juce::Colour::fromString(value);
        return ImGui::ColorConvertFloat4ToU32({c.getFloatRed(),c.getFloatGreen(),c.getFloatBlue(),c.getFloatAlpha()});
    };
    p.accent=color("accent",p.accent);p.bg1=color("background",p.bg1);p.text=color("text",p.text);p.textDim=color("text",p.textDim);
    const auto rounding=static_cast<float>(control.design.getProperty("rounding",4.0));
    list->AddRectFilled(min,max,p.bg1,rounding);
    const auto w = max.x - min.x;
    const auto h = max.y - min.y;
    const auto font = ImGui::GetFontSize();
    const auto label = control.label.isNotEmpty() ? control.label
                       : parameter(control.parameterBinding) != nullptr ? parameter(control.parameterBinding)->label
                                                                         : juce::String();
    const auto caption = [&](const float y) {
        if (label.isEmpty())
            return;
        const auto size = ImGui::CalcTextSize(label.toRawUTF8());
        ui::clippedText(list, ImVec2(min.x + std::max(2.0f, (w - size.x) * 0.5f), y), w - 4.0f, p.textDim, label.toRawUTF8());
    };
    const auto valueText = [&](const juce::String& key) {
        const auto* item = parameter(key);
        if (item == nullptr)
            return juce::String();
        const auto magnitude = std::abs(item->value);
        const auto digits = magnitude >= 100.0 ? 0 : magnitude >= 10.0 ? 1 : 2;
        return juce::String(item->value, digits) + (item->unit.isNotEmpty() ? " " + item->unit : juce::String());
    };
    if(static_cast<bool>(control.design.getProperty("showValue",false)) && control.parameterBinding.isNotEmpty())
        list->AddText(ImVec2(min.x+4,min.y+2),p.text,valueText(control.parameterBinding).toRawUTF8());
    const auto fontScale=std::clamp(static_cast<float>(control.design.getProperty("fontScale",1.0)),.5f,3.0f);
    ImGui::PushFont(nullptr,ui::currentFontBase()*fontScale);
    switch (control.kind) {
    case Kind::knob: {
        const auto room = std::max(4.0f, h - font * 1.3f);
        const auto radius = std::max(4.0f, std::min(w, room) * 0.38f);
        const ImVec2 centre((min.x + max.x) * 0.5f, min.y + room * 0.5f + 1.0f);
        const auto start = std::numbers::pi_v<float> * 0.75f;
        const auto end = std::numbers::pi_v<float> * 2.25f;
        const auto at = start + static_cast<float>(value01(control.parameterBinding)) * (end - start);
        list->PathArcTo(centre, radius, start, end, 32);
        list->PathStroke(p.bg4, radius * 0.22f, ImDrawFlags_None);
        list->PathArcTo(centre, radius, start, at, 32);
        list->PathStroke(p.accent, radius * 0.22f, ImDrawFlags_None);
        list->AddCircleFilled(centre, radius * 0.62f, p.bg3, 24);
        list->AddLine(centre, ImVec2(centre.x + std::cos(at) * radius * 0.6f, centre.y + std::sin(at) * radius * 0.6f), p.text,
                      std::max(1.5f, radius * 0.1f));
        caption(max.y - font * 1.2f);
        break;
    }
    case Kind::fader: {
        const auto top = min.y + 4.0f;
        const auto bottom = max.y - font * 1.4f;
        const auto x = (min.x + max.x) * 0.5f;
        list->AddRectFilled(ImVec2(x - 2.0f, top), ImVec2(x + 2.0f, bottom), p.bg4, 2.0f);
        const auto y = bottom - static_cast<float>(value01(control.parameterBinding)) * (bottom - top);
        list->AddRectFilled(ImVec2(x - 2.0f, y), ImVec2(x + 2.0f, bottom), p.accent, 2.0f);
        list->AddRectFilled(ImVec2(x - std::min(w * 0.35f, 14.0f), y - 4.0f), ImVec2(x + std::min(w * 0.35f, 14.0f), y + 4.0f), p.text,
                            2.0f);
        caption(max.y - font * 1.2f);
        break;
    }
    case Kind::toggle: {
        const auto on = value01(control.parameterBinding) > 0.5;
        list->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, max.y - 2.0f), on ? p.accentDim : p.bg3, 4.0f);
        list->AddRect(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, max.y - 2.0f), on ? p.accent : p.lineStrong, 4.0f, 1.0f);
        ui::centredText(list, min, max, on ? p.text : p.textDim, label.toRawUTF8());
        break;
    }
    case Kind::selector: {
        list->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, max.y - 2.0f), p.bg3, 4.0f);
        const auto* item = parameter(control.parameterBinding);
        const auto text = item != nullptr ? app::moduleSelectorValueLabel(*item, item->value) : juce::String();
        ui::centredText(list, min, max, p.text,
                        (juce::String::fromUTF8("\xe2\x80\xb9 ") + text + juce::String::fromUTF8(" \xe2\x80\xba")).toRawUTF8());
        break;
    }
    case Kind::xyPad: {
        const ImVec2 a(min.x + 3.0f, min.y + 3.0f);
        const ImVec2 b(max.x - 3.0f, max.y - font * 1.3f);
        list->AddRectFilled(a, b, p.bg1, 4.0f);
        list->AddRect(a, b, p.lineStrong, 4.0f, 1.0f);
        const ImVec2 dot(a.x + static_cast<float>(value01(control.parameterBinding)) * (b.x - a.x),
                         b.y - static_cast<float>(value01(control.secondaryParameterBinding)) * (b.y - a.y));
        list->AddLine(ImVec2(dot.x, a.y), ImVec2(dot.x, b.y), theme::alpha(p.accent, 0.35f));
        list->AddLine(ImVec2(a.x, dot.y), ImVec2(b.x, dot.y), theme::alpha(p.accent, 0.35f));
        list->AddCircleFilled(dot, 5.0f, p.accent, 16);
        caption(max.y - font * 1.2f);
        break;
    }
    case Kind::valueDisplay: {
        list->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, max.y - 2.0f), p.bg1, 4.0f);
        ui::centredText(list, ImVec2(min.x, min.y + h * 0.3f), max, p.accent, valueText(control.parameterBinding).toRawUTF8());
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.8f);
        ui::clippedText(list, ImVec2(min.x + 6.0f, min.y + 4.0f), w - 8.0f, p.textDim, label.toRawUTF8());
        ImGui::PopFont();
        break;
    }
    case Kind::text: {
        ImGui::PushFont(theme::boldFont(), std::clamp(h * 0.55f, ui::currentFontBase() * 0.8f, dp(28.0f)));
        ui::clippedText(list, ImVec2(min.x + 4.0f, min.y + (h - ImGui::GetFontSize()) * 0.5f), w - 6.0f, p.text, label.toRawUTF8());
        ImGui::PopFont();
        break;
    }
    case Kind::group: {
        list->AddRect(ImVec2(min.x + 1.0f, min.y + font * 0.5f), ImVec2(max.x - 1.0f, max.y - 1.0f), p.lineStrong, 6.0f, 1.2f);
        if (label.isNotEmpty()) {
            const auto size = ImGui::CalcTextSize(label.toRawUTF8());
            list->AddRectFilled(ImVec2(min.x + 8.0f, min.y), ImVec2(min.x + 14.0f + size.x, min.y + font), p.bg1);
            list->AddText(ImVec2(min.x + 11.0f, min.y), p.textDim, label.toRawUTF8());
        }
        break;
    }
    case Kind::filterResponse:
    case Kind::dynamicsResponse:
    case Kind::envelope:
    case Kind::eqCurve:
    case Kind::waveform:
    case Kind::spectrum:
    case Kind::wavetable: {
        const ImVec2 a(min.x + 3.0f, min.y + 3.0f);
        const ImVec2 b(max.x - 3.0f, max.y - (label.isNotEmpty() ? font * 1.3f : 3.0f));
        list->AddRectFilled(a, b, p.bg1, 4.0f);
        list->AddRect(a, b, p.line, 4.0f, 1.0f);
        std::vector<ImVec2> points;
        const auto toY = [&a, &b](const float position) { return b.y - std::clamp(position, 0.0f, 1.0f) * (b.y - a.y); };
        constexpr auto count = 64;
        if (control.kind == Kind::filterResponse) {
            // A low-pass at the cutoff, its peak from the resonance.
            const auto* cutoff = parameter(control.parameterBinding);
            const auto frequency = cutoff != nullptr && cutoff->unit == "Hz" ? std::clamp(cutoff->value, 20.0, 20'000.0)
                                                                             : 20.0 * std::pow(1'000.0, value01(control.parameterBinding));
            const auto q = 0.5 + value01(control.secondaryParameterBinding) * 8.0;
            for (auto index = 0; index <= count; ++index) {
                const auto x = static_cast<double>(index) / count;
                const auto ratio = 20.0 * std::pow(1'000.0, x) / frequency;
                const auto gain = 1.0 / std::sqrt(std::pow(1.0 - ratio * ratio, 2.0) + std::pow(ratio / q, 2.0));
                const auto db = std::clamp(20.0 * std::log10(std::max(gain, 1.0e-6)), -36.0, 18.0);
                points.push_back({ a.x + static_cast<float>(x) * (b.x - a.x), toY(static_cast<float>((db + 36.0) / 54.0)) });
            }
        } else if (control.kind == Kind::dynamicsResponse) {
            const auto* threshold = parameter(control.parameterBinding);
            const auto* ratio = parameter(control.secondaryParameterBinding);
            const auto t = threshold != nullptr ? std::clamp(threshold->value, -60.0, 0.0) : -18.0;
            const auto r = ratio != nullptr ? std::max(1.0, ratio->value) : 4.0;
            for (auto index = 0; index <= count; ++index) {
                const auto in = -60.0 + 60.0 * index / count;
                const auto out = in < t ? in : t + (in - t) / r;
                points.push_back({ a.x + static_cast<float>(index) / count * (b.x - a.x), toY(static_cast<float>((out + 60.0) / 60.0)) });
            }
            list->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), theme::alpha(p.textDim, 0.3f));
        } else if (control.kind == Kind::envelope) {
            const auto attack = 0.05f + 0.25f * static_cast<float>(value01(control.parameterBinding));
            const auto decay = 0.05f + 0.25f * static_cast<float>(value01(control.secondaryParameterBinding));
            const auto sustain = static_cast<float>(value01(control.tertiaryParameterBinding));
            const auto release = 0.05f + 0.25f * static_cast<float>(value01(control.quaternaryParameterBinding));
            const auto x = [&a, &b](const float t) { return a.x + t * (b.x - a.x); };
            points = { { x(0.0f), toY(0.0f) }, { x(attack), toY(0.95f) }, { x(attack + decay), toY(sustain * 0.95f) },
                       { x(1.0f - release), toY(sustain * 0.95f) }, { x(1.0f), toY(0.0f) } };
        } else if (control.kind == Kind::eqCurve) {
            points = { { a.x, toY(0.5f) }, { b.x, toY(0.5f) } };
            for (std::size_t band = 0; band + 2 < control.extraParameterBindings.size(); band += 3) {
                const ImVec2 dot(a.x + static_cast<float>(value01(control.extraParameterBindings[band])) * (b.x - a.x),
                                 toY(static_cast<float>(value01(control.extraParameterBindings[band + 1]))));
                list->AddCircleFilled(dot, 4.0f, p.accent, 12);
            }
        } else if (control.kind == Kind::spectrum) {
            std::array<float,64> bins {};
            if(analysis_) cydaw::engine::computeLogSpectrum(*analysis_,20,20000,bins.data(),bins.size());
            for(std::size_t i=0;i<bins.size();++i) {
                const auto x=a.x+(b.x-a.x)*static_cast<float>(i)/bins.size();
                list->AddRectFilled(ImVec2(x,toY(bins[i])),ImVec2(x+(b.x-a.x)/bins.size(),b.y),theme::alpha(p.accent,.65f));
            }
        } else if (control.kind == Kind::waveform) {
            if(analysis_ && analysis_->waveformCount>1) for(std::size_t i=0;i<analysis_->waveformCount;i+=std::max(1u,analysis_->waveformCount/256))
                points.push_back({a.x+(b.x-a.x)*static_cast<float>(i)/analysis_->waveformCount,toY(.5f+.45f*analysis_->waveform[i])});
            else points={{a.x,toY(.5f)},{b.x,toY(.5f)}};
        } else {
            const auto shape=value01(control.parameterBinding);
            for(auto i=0;i<=count;++i){const auto t=static_cast<float>(i)/count;
                const auto sine=std::sin(t*std::numbers::pi_v<float>*4);
                const auto saw=(t*2-std::floor(t*2))*2-1;
                points.push_back({a.x+t*(b.x-a.x),toY(.5f+.38f*static_cast<float>((1-shape)*sine+shape*saw))});}
        }
        if (points.size() > 1)
            list->AddPolyline(points.data(), static_cast<int>(points.size()), p.accent, dp(1.6f));
        caption(max.y - font * 1.2f);
        break;
    }
    case Kind::levelMeter: {
        list->AddRectFilled(ImVec2(min.x + 2.0f, min.y + 2.0f), ImVec2(max.x - 2.0f, max.y - 2.0f), p.bg1, 2.0f);
        list->AddRectFilled(ImVec2(min.x + 3.0f, min.y + h * (1.0f-(analysis_?std::clamp(analysis_->peak[0],0.0f,1.0f):0.0f))), ImVec2(max.x - 3.0f, max.y - 3.0f), theme::alpha(p.play, 0.7f), 2.0f);
        break;
    }
    case Kind::nativePanel: {
        if(control.design.hasProperty("instrumentType")) {
            std::vector<visual::Param> parameters;std::vector<std::string> keys;
            for(const auto& binding:control.extraParameterBindings) {
                if(const auto* a=parameter(binding))parameters.push_back({a->key,a->label,a->unit,a->minimum,a->maximum,a->value,a->defaultValue});
                else parameters.emplace_back();
            }
            if(const auto* list=control.design.getProperty("instrumentKeys",{}).getArray())for(const auto& key:*list)keys.push_back(key.toString().toStdString());
            const auto type=control.design.getProperty("instrumentType",{}).toString().toStdString();
            const auto panel=control.design.getProperty("instrumentPanel",{}).toString().toStdString();
            const auto compact=static_cast<bool>(control.design.getProperty("instrumentCompact",false));
            if(const auto* spec=instrument_ui::panel(type,panel,compact)) {
                ImGui::PushID(static_cast<int>(control.id));
                ImGui::PushStyleColor(ImGuiCol_ChildBg,p.bg1);ImGui::PushStyleColor(ImGuiCol_Text,p.text);
                instrument_ui::draw("##instrument",type,*spec,keys,parameters,min,ImVec2(w,h),p.accent,
                    [this](const auto& param,double value){if(renderingPlayed_&&componentSet_)componentSet_(param.binding,value);},renderingPlayed_,
                    w/std::max(1.0f,control.width*windowSize().x),control.label.toRawUTF8());
                ImGui::PopStyleColor(2);ImGui::PopID();
            }
            break;
        }
        std::vector<visual::Param> parameters;
        for(const auto& binding:control.extraParameterBindings) if(const auto* p=parameter(binding))
            parameters.push_back({p->key,p->label,p->unit,p->minimum,p->maximum,p->value,p->defaultValue});
        visual::Pane pane;
        pane.kind=static_cast<visual::PaneKind>(juce::jlimit(0,static_cast<int>(visual::PaneKind::roulement),static_cast<int>(control.design.getProperty("pane",0))));
        pane.variant=static_cast<int>(control.design.getProperty("variant",0));
        if(const auto* indices=control.design.getProperty("indices",{}).getArray())for(const auto& i:*indices)pane.parameters.push_back(static_cast<int>(i));
        visual::Live live;live.afterFrame=analysis_;live.beforeFrame=analysis_;live.running=analysis_&&analysis_->sequence>0;
        const visual::Setter set=[this](const auto& p,double value){if(renderingPlayed_&&componentSet_)componentSet_(p.binding,value);};
        const auto cursor=ImGui::GetCursorScreenPos();ImGui::PushID(static_cast<int>(control.id));
        ImGui::BeginDisabled(!renderingPlayed_);
        if(static_cast<bool>(control.design.getProperty("large",false)))visual::drawLargePane("##native",pane,parameters,min,ImVec2(w,h),p.accent,live,set);
        else visual::drawPane("##native",pane,parameters,min,ImVec2(w,h),p.accent,live,set,true);
        ImGui::EndDisabled();ImGui::PopID();ImGui::SetCursorScreenPos(cursor);ImGui::Dummy(ImVec2(0,0));break;
    }
    case Kind::custom: {
        const auto source=control.design.getProperty("customSource",{}).toString();
        if(const auto draw=CustomInterface::renderer(source)) {
            struct Data { const InterfaceEditor* editor; } data {this};
            const auto primary=control.parameterBinding.toStdString(),secondary=control.secondaryParameterBinding.toStdString();
            const auto tertiary=control.tertiaryParameterBinding.toStdString(),quaternary=control.quaternaryParameterBinding.toStdString();
            CyInterfaceContext api;
            api.imgui=ImGui::GetCurrentContext();api.user=&data;api.primary=primary.c_str();api.secondary=secondary.c_str();
            api.tertiary=tertiary.c_str();api.quaternary=quaternary.c_str();
            if(const auto* p=parameter(control.parameterBinding)) {api.minimum=p->minimum;api.maximum=p->maximum;}
            api.get=[](void* user,const char* binding) { const auto* self=static_cast<Data*>(user)->editor;
                if(const auto* p=self->parameter(juce::String::fromUTF8(binding)))return p->value;return 0.0; };
            api.set=[](void* user,const char* binding,double value) {const auto* self=static_cast<Data*>(user)->editor;
                const auto key=juce::String::fromUTF8(binding);if(const auto* p=self->parameter(key);p && std::isfinite(value) && self->renderingPlayed_ && self->componentSet_)
                    self->componentSet_(key,std::clamp(value,p->minimum,p->maximum));};
            const auto cursor=ImGui::GetCursorScreenPos();ImGui::SetCursorScreenPos(min);ImGui::PushID(static_cast<int>(control.id));
            ImGui::BeginChild("##cppComponent",ImVec2(std::max(1.0f,w),std::max(1.0f,h)),0,ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|(!renderingPlayed_?ImGuiWindowFlags_NoInputs:0));
            ImGui::BeginDisabled(!renderingPlayed_);draw(&api);ImGui::EndDisabled();ImGui::EndChild();ImGui::PopID();ImGui::SetCursorScreenPos(cursor);ImGui::Dummy(ImVec2(0,0));
        } else ui::centredText(list,min,max,p.textDim,L("Composant C++ à compiler","C++ component: compile first"));
        break;
    }
    case Kind::inputPort:
    case Kind::outputPort: break;
    }
    ImGui::PopFont();
}

void InterfaceEditor::drawPlayed(const char* id, const app::ModuleInterfaceState& state, const ImVec2 size,
                                 const std::function<void(const juce::String&, double)>& setValue, const engine::AudioAnalysisFrame* analysis)
{
    const auto previousAnalysis=std::exchange(analysis_,analysis);
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    const auto& p = theme::palette();
    auto& io = ImGui::GetIO();
    // The controls read the state through the editor's own lookups.
    const auto kept = std::exchange(interface_, state);
    drawPageBar();
    renderingPlayed_=true;componentSet_=setValue;
    auto& gesture = played_[id];
    const auto window = windowSize();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto scale = std::max(0.1f, std::min(size.x / window.x, size.y / window.y));
    const ImVec2 canvasSize(window.x * scale, window.y * scale);
    const ImVec2 min(origin.x + (size.x - canvasSize.x) * 0.5f, origin.y + (size.y - canvasSize.y) * 0.5f);
    const ImVec2 max(min.x + canvasSize.x, min.y + canvasSize.y);
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(min, max, surfaceColour("background",p.bg1), 6.0f);
    const auto rectOf = [&](const Control& control) {
        const ImVec2 at(min.x + control.x * canvasSize.x, min.y + control.y * canvasSize.y);
        return std::make_pair(at, ImVec2(at.x + control.width * canvasSize.x, at.y + control.height * canvasSize.y));
    };
    const auto hitAt = [&](const ImVec2 screen) -> const Control* {
        for (auto index = interface_.controls.size(); index > 0; --index) {
            const auto& control = interface_.controls[index - 1];
            if (isPort(control.kind) || !visible(control) || control.kind == Kind::group || control.kind == Kind::text || control.kind == Kind::custom || control.kind == Kind::nativePanel)
                continue;
            const auto [a, b] = rectOf(control);
            if (screen.x >= a.x && screen.x <= b.x && screen.y >= a.y && screen.y <= b.y)
                return &control;
        }
        return nullptr;
    };
    // A setting moved to a place of its travel, shown at once.
    const auto set01 = [&](const juce::String& key, const double position) {
        for (auto& item : interface_.parameters)
            if (item.key == key) {
                const auto at = std::clamp(position, 0.0, 1.0);
                item.value = logarithmic(item) ? item.minimum * std::pow(item.maximum / item.minimum, at)
                                               : item.minimum + at * (item.maximum - item.minimum);
                setValue(key, item.value);
            }
    };

    ImGui::PushID(id);
    ImGui::InvisibleButton("##played", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)));
    const auto mouse = io.MousePos;
    if (ImGui::IsItemActivated()) {
        gesture = {};
        if (const auto* control = hitAt(mouse)) {
            gesture.control = control->id;
            gesture.start = mouse;
            gesture.startValue = value01(control->parameterBinding);
            gesture.dragging = true;
            if (control->kind == Kind::toggle) {
                set01(control->parameterBinding, gesture.startValue > 0.5 ? 0.0 : 1.0);
                gesture.dragging = false;
            } else if (control->kind == Kind::selector) {
                if (const auto* item = parameter(control->parameterBinding)) {
                    const auto values = app::moduleSelectorValues(*item);
                    if (!values.empty()) {
                        const auto now = app::closestModuleSelectorValue(values, item->value);
                        setValue(control->parameterBinding, values[(now + 1) % values.size()]);
                    }
                }
                gesture.dragging = false;
            }
        }
    }
    if (ImGui::IsItemActive() && gesture.dragging)
        if (const auto* control = find(gesture.control)) {
            if (control->kind == Kind::xyPad) {
                const auto [a, b] = rectOf(*control);
                set01(control->parameterBinding, (mouse.x - a.x) / std::max(1.0f, b.x - a.x));
                set01(control->secondaryParameterBinding, 1.0 - (mouse.y - a.y) / std::max(1.0f, b.y - a.y));
            } else {
                const auto fine = static_cast<double>(ui::valueSpeed());
                set01(control->parameterBinding, gesture.startValue - (mouse.y - gesture.start.y) / dp(180.0f) * fine);
            }
        }
    if (ImGui::IsItemDeactivated())
        gesture = {};
    // A double click puts a knob back to its default.
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        if (const auto* control = hitAt(mouse); control != nullptr && (control->kind == Kind::knob || control->kind == Kind::fader))
            if (const auto* item = parameter(control->parameterBinding); item != nullptr && item->maximum > item->minimum) {
                const auto fallback = std::isnan(item->defaultValue) ? item->minimum : item->defaultValue;
                setValue(item->key, fallback);
            }
    if (ImGui::IsItemHovered())
        if (const auto* control = hitAt(mouse))
            if (const auto* item = parameter(control->parameterBinding))
                ImGui::SetTooltip("%s", item->label.toRawUTF8());
    ImGui::PopID();

    list->PushClipRect(min, max, true);
    for (const auto pass : { 0, 1 })
        for (const auto& control : interface_.controls) {
            if (isPort(control.kind) || !visible(control) || (control.kind == Kind::group) != (pass == 0))
                continue;
            const auto [a, b] = rectOf(control);
            drawControl(list, control, a, b);
        }
    list->PopClipRect();
    interface_ = kept;
    analysis_ = previousAnalysis;
}

void InterfaceEditor::drawCanvas(GraphSlot& slot)
{
    const auto& p = theme::palette();
    pull(slot);
    auto& io = ImGui::GetIO();

    renderingPlayed_=play_;
    componentSet_=[&slot](const juce::String& key,double value){slot.graph().editorSetParameter(key,value);};
    // The toolbar: edit or play, the automatic layout, the grid.
    if (ui::iconButton(icon::edit, L("Disposer : déplacer et dimensionner les contrôles", "Lay out: move and size the controls"), !play_))
        play_ = false;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::play, L("Jouer : tourner les potards comme dans le plugin", "Play: turn the knobs as in the plugin"), play_,
                       p.play))
        play_ = true;
    ui::toolbarSeparator();
    if (ImGui::Button(L("Disposition automatique", "Automatic layout")))
        autoLayout(slot);
    ui::tooltip(L("Un potard par réglage publié, en rangées, sous le nom du périphérique (remplace les contrôles).",
                  "One knob per published setting, in rows, under the device's name (replaces the controls)."));
    ImGui::SameLine();
    auto snapToGrid = interface_.snapToGrid;
    if (ImGui::Checkbox(L("Aimanter à la grille", "Snap to the grid"), &snapToGrid)) {
        interface_.snapToGrid = snapToGrid;
        (void) push(slot, true);
    }
    ImGui::SameLine();
    ImGui::NewLine();
    auto size=windowSize();int dimensions[]{static_cast<int>(size.x),static_cast<int>(size.y)};
    ImGui::TextUnformatted(deviceLayout_?L("Taille carte","Card size"):L("Taille fenêtre","Window size"));ImGui::SameLine();ImGui::SetNextItemWidth(dp(180));
    if(ImGui::InputInt2("##canvasDimensions",dimensions,ImGuiInputTextFlags_EnterReturnsTrue)) {
        interface_.windowWidth=static_cast<std::uint16_t>(std::clamp(dimensions[0],deviceLayout_?120:240,4096));
        interface_.windowHeight=static_cast<std::uint16_t>(std::clamp(dimensions[1],deviceLayout_?80:160,2160));push(slot,true);
    }
    if(ImGui::IsItemDeactivatedAfterEdit()) {
        interface_.windowWidth=static_cast<std::uint16_t>(std::clamp(dimensions[0],deviceLayout_?120:240,4096));
        interface_.windowHeight=static_cast<std::uint16_t>(std::clamp(dimensions[1],deviceLayout_?80:160,2160));push(slot,true);
    }
    size=windowSize();ImGui::SameLine();ImGui::TextDisabled("px");
    if (error_.isNotEmpty() && ImGui::GetTime() - errorTime_ < 6.0) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.record), "%s", error_.toRawUTF8());
    }

    ImGui::SameLine();
    if(ImGui::Button("Code JSON")) { codeMode_=!codeMode_; code_[0]=0; }
    if(codeMode_) { drawCode(slot); return; }
    ImGui::SameLine();ImGui::SetNextItemWidth(dp(105));ImGui::SliderFloat("Zoom",&zoom_,.25f,3.0f,"%.2fx");
    ImGui::SameLine();if(ImGui::SmallButton(L("Cadrer","Fit"))){zoom_=1;pan_={};actualSize_=false;}
    ImGui::SameLine();if(ImGui::SmallButton("1:1")){actualSize_=true;zoom_=1;pan_={};}
    ui::tooltip(L("Aperçu à la taille choisie en pixels. Bouton du milieu pour déplacer la vue.","Preview at the chosen pixel size. Pan with the middle mouse button."));
    drawPageBar();
    // The window, as large as fits, its proportions kept.
    const auto available = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto margin = dp(16.0f);
    const auto scale = actualSize_?1.0f:std::max(0.1f, std::min((available.x - margin * 2.0f) / size.x, (available.y - margin * 2.0f) / size.y));
    const ImVec2 canvasSize(size.x * scale * zoom_, size.y * scale * zoom_);
    canvasMin_ = ImVec2(origin.x + (available.x - canvasSize.x) * 0.5f+pan_.x, origin.y + (available.y - canvasSize.y) * 0.5f+pan_.y);
    canvasMax_ = ImVec2(canvasMin_.x + canvasSize.x, canvasMin_.y + canvasSize.y);
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + available.x, origin.y + available.y), p.bg0);
    list->AddRectFilled(ImVec2(canvasMin_.x + 4.0f, canvasMin_.y + 6.0f), ImVec2(canvasMax_.x + 4.0f, canvasMax_.y + 6.0f),
                        theme::alpha(0xff000000u, 0.35f), 6.0f);
    list->AddRectFilled(canvasMin_, canvasMax_, surfaceColour("background",p.bg1), 6.0f);
    list->AddRect(canvasMin_, canvasMax_, p.lineStrong, 6.0f, 1.0f);
    const auto cell = ImVec2(canvasSize.x / interface_.gridColumns, canvasSize.y / interface_.gridRows);
    if (!play_)
        for (auto column = 1; column < interface_.gridColumns; ++column)
            for (auto row = 1; row < interface_.gridRows; ++row)
                list->AddCircleFilled(ImVec2(canvasMin_.x + cell.x * column, canvasMin_.y + cell.y * row), 1.0f,
                                      theme::alpha(p.textFaint, 0.5f), 4);

    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##interfaceCanvas", ImVec2(std::max(1.0f, available.x), std::max(1.0f, available.y)));
    const auto hovered = ImGui::IsItemHovered();
    if(hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {pan_.x+=io.MouseDelta.x;pan_.y+=io.MouseDelta.y;}
    const auto mouse = io.MousePos;
    const auto toFraction = [&](const ImVec2 screen) {
        return ImVec2((screen.x - canvasMin_.x) / canvasSize.x, (screen.y - canvasMin_.y) / canvasSize.y);
    };

    // Drops from the palette.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kindPayload); payload != nullptr && payload->DataSize == sizeof(int)) {
            const auto kind = static_cast<Kind>(*static_cast<const int*>(payload->Data));
            (void) add(slot, kind, toFraction(mouse), {});
        }
        if (const auto* payload = ImGui::AcceptDragDropPayload(parameterPayload); payload != nullptr && payload->DataSize > 0)
            (void) add(slot, Kind::knob, toFraction(mouse),
                       juce::String::fromUTF8(static_cast<const char*>(payload->Data), payload->DataSize - 1));
        ImGui::EndDragDropTarget();
    }

    // The selected control's handle, bottom right.
    const auto handle = dp(9.0f);
    const auto rectOf = [&](const Control& control) {
        const ImVec2 min(canvasMin_.x + control.x * canvasSize.x, canvasMin_.y + control.y * canvasSize.y);
        return std::make_pair(min, ImVec2(min.x + control.width * canvasSize.x, min.y + control.height * canvasSize.y));
    };
    if (ImGui::IsItemActivated()) {
        const auto* picked = find(selected_);
        const auto onHandle = !play_ && picked != nullptr && [&] {
            const auto [min, max] = rectOf(*picked);
            return mouse.x >= max.x - handle && mouse.x <= max.x + 2.0f && mouse.y >= max.y - handle && mouse.y <= max.y + 2.0f;
        }();
        if (onHandle) {
            drag_ = Drag::resize;
        } else {
            selected_ = hit(mouse);
            if(!play_) {
                if(!io.KeyCtrl && !io.KeyShift && !selection_.contains(selected_))selection_.clear();
                if(selected_) {
                    if(io.KeyCtrl && selection_.contains(selected_))selection_.erase(selected_);else selection_.insert(selected_);
                    if(const auto* c=find(selected_)){const auto group=c->design.getProperty("group",{}).toString();
                        if(group.isNotEmpty() && !io.KeyCtrl)for(const auto& other:interface_.controls)if(other.design.getProperty("group",{}).toString()==group)selection_.insert(other.id);}
                }
            }
            marquee_=!play_ && selected_==0;
            if(marquee_){dragStart_=mouse;marqueeSelection_=selection_;}
            drag_ = selected_ != 0 ? (play_ ? Drag::play : Drag::move) : Drag::none;
            if(const auto* c=find(selected_);c && !play_ && static_cast<bool>(c->design.getProperty("locked",false)))drag_=Drag::none;
            dragOrigins_.clear();for(const auto& c:interface_.controls)if(selection_.contains(c.id))dragOrigins_[c.id]=c;
        }
        if (const auto* control = find(selected_)) {
            dragOrigin_ = *control;
            dragStart_ = mouse;
            playStart_ = value01(control->parameterBinding);
            // A switch flips, a selector steps, on the click.
            if (play_ && control->kind == Kind::toggle) {
                setValue01(slot, control->parameterBinding, playStart_ > 0.5 ? 0.0 : 1.0);
                drag_ = Drag::none;
            } else if (play_ && control->kind == Kind::selector) {
                if (const auto* item = parameter(control->parameterBinding)) {
                    const auto values = app::moduleSelectorValues(*item);
                    if (!values.empty()) {
                        const auto now = app::closestModuleSelectorValue(values, item->value);
                        const auto next = values[(now + 1) % values.size()];
                        for (auto& stored : interface_.parameters)
                            if (stored.key == control->parameterBinding) {
                                stored.value = next;
                                (void) slot.graph().editorSetParameter(stored.key, next);
                            }
                    }
                }
                drag_ = Drag::none;
            }
        }
    }
    if (ImGui::IsItemActive() && drag_ != Drag::none) {
        if (auto* control = find(selected_)) {
            const ImVec2 delta((mouse.x - dragStart_.x) / canvasSize.x, (mouse.y - dragStart_.y) / canvasSize.y);
            if (drag_ == Drag::move) {
                auto dx=snap(delta.x,interface_.gridColumns),dy=snap(delta.y,interface_.gridRows);
                for(const auto& [id,c]:dragOrigins_) {dx=std::clamp(dx,-c.x,1-c.x-c.width);dy=std::clamp(dy,-c.y,1-c.y-c.height);}
                for(auto& c:interface_.controls)if(auto it=dragOrigins_.find(c.id);it!=dragOrigins_.end() && !static_cast<bool>(c.design.getProperty("locked",false))) {
                    c.x=it->second.x+dx;c.y=it->second.y+dy;
                }
                (void) push(slot, false);
            } else if (drag_ == Drag::resize) {
                const auto minimumWidth = 1.0f / interface_.gridColumns;
                const auto minimumHeight = 1.0f / interface_.gridRows;
                control->width = std::clamp(snap(dragOrigin_.width + delta.x, interface_.gridColumns), minimumWidth, 1.0f - control->x);
                control->height = std::clamp(snap(dragOrigin_.height + delta.y, interface_.gridRows), minimumHeight, 1.0f - control->y);
                (void) push(slot, false);
            } else if (drag_ == Drag::play) {
                if (control->kind == Kind::xyPad) {
                    const auto [min, max] = rectOf(*control);
                    setValue01(slot, control->parameterBinding, (mouse.x - min.x) / std::max(1.0f, max.x - min.x));
                    setValue01(slot, control->secondaryParameterBinding, 1.0 - (mouse.y - min.y) / std::max(1.0f, max.y - min.y));
                } else {
                    const auto fine = static_cast<double>(ui::valueSpeed());
                    setValue01(slot, control->parameterBinding, playStart_ - (mouse.y - dragStart_.y) / dp(180.0f) * fine);
                }
            }
        }
    }
    if(marquee_ && ImGui::IsItemActive()) {
        const ImVec2 a(std::min(mouse.x,dragStart_.x),std::min(mouse.y,dragStart_.y));
        const ImVec2 b(std::max(mouse.x,dragStart_.x),std::max(mouse.y,dragStart_.y));
        selection_=marqueeSelection_;
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left,3))for(const auto& c:interface_.controls) {
            const auto [min,max]=rectOf(c);
            if(visible(c)&&!isPort(c.kind)&&min.x<b.x&&max.x>a.x&&min.y<b.y&&max.y>a.y)selection_.insert(c.id);
        }
        list->AddRectFilled(a,b,theme::alpha(p.accent,.12f));list->AddRect(a,b,p.accent);
    }
    if(marquee_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {marquee_=false;if(!selection_.empty())selected_=*selection_.begin();}
    if (ImGui::IsItemDeactivated() && drag_ != Drag::none) {
        const auto moved = drag_ == Drag::move || drag_ == Drag::resize;
        drag_ = Drag::none;
        if (moved)
            (void) push(slot, true);
    }
    // A double click puts a knob back to its default.
    if (hovered && play_ && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        if (const auto* control = find(hit(mouse)); control != nullptr && control->kind == Kind::knob)
            if (const auto* item = parameter(control->parameterBinding); item != nullptr && item->maximum > item->minimum) {
                const auto fallback = std::isnan(item->defaultValue) ? item->minimum : item->defaultValue;
                const auto position = logarithmic(*item)
                                          ? std::log(std::max(fallback, item->minimum) / item->minimum)
                                                / std::log(item->maximum / item->minimum)
                                          : (fallback - item->minimum) / (item->maximum - item->minimum);
                setValue01(slot, control->parameterBinding, position);
            }

    // The controls: frames first, under the rest.
    list->PushClipRect(canvasMin_, canvasMax_, true);
    for (const auto pass : { 0, 1 })
        for (const auto& control : interface_.controls) {
            if (isPort(control.kind) || !visible(control) || (control.kind == Kind::group) != (pass == 0))
                continue;
            const auto [min, max] = rectOf(control);
            drawControl(list, control, min, max);
            if (!play_) {
                const auto picked = control.id == selected_ || selection_.contains(control.id);
                list->AddRect(min, max, picked ? p.accent : theme::alpha(p.textFaint, 0.35f), 3.0f, picked ? 1.6f : 1.0f);
                if (picked)
                    list->AddRectFilled(ImVec2(max.x - handle, max.y - handle), max, p.accent, 2.0f);
            }
        }
    list->PopClipRect();
    if (interface_.controls.empty() || std::all_of(interface_.controls.begin(), interface_.controls.end(),
                                                    [](const Control& control) { return isPort(control.kind); }))
        ui::centredText(list, canvasMin_, canvasMax_, p.textDim,
                        L("Glissez des contrôles ici, ou « Disposition automatique ».", "Drag controls here, or \"Automatic layout\"."));

    // Keys, while the pointer is over the window and nothing is typed.
    if (hovered && !io.WantTextInput && !play_) {
        if (selected_ != 0 && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
            std::erase_if(interface_.controls, [this](const Control& control) { return control.id == selected_ || selection_.contains(control.id); });
            selected_ = 0;selection_.clear();
            (void) push(slot, true);
        }
        if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A,false)) {
            selection_.clear();for(const auto& c:interface_.controls)if(!isPort(c.kind) && visible(c))selection_.insert(c.id);
            if(!selection_.empty())selected_=*selection_.begin();
        }
        if(selected_ && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C,false)) {
            auto copy=interface_;std::erase_if(copy.controls,[this](const auto& c){return c.id!=selected_ && !selection_.contains(c.id);});
            copy.deviceControls.clear();const auto json=juce::JSON::toString(app::serialiseModuleInterface(copy),false);
            ImGui::SetClipboardText((juce::String("CyDAW-Interface\n")+json).toRawUTF8());
        }
        if(io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V,false)) {
            const auto text=juce::String::fromUTF8(ImGui::GetClipboardText());
            if(text.startsWith("CyDAW-Interface\n")) {
                const auto copied=app::restoreModuleInterface(juce::JSON::parse(text.substring(16)));
                std::vector<Control> additions;bool valid=true;
                for(auto c:copied.controls) {
                    for(const auto* key:{&c.parameterBinding,&c.secondaryParameterBinding,&c.tertiaryParameterBinding,&c.quaternaryParameterBinding})if(key->isNotEmpty() && !parameter(*key))valid=false;
                    c.id=interface_.nextControlId++;c.x=std::min(1-c.width,c.x+1.0f/interface_.gridColumns);c.y=std::min(1-c.height,c.y+1.0f/interface_.gridRows);additions.push_back(std::move(c));
                }
                if(valid){selection_.clear();for(const auto& c:additions)selection_.insert(c.id);interface_.controls.insert(interface_.controls.end(),additions.begin(),additions.end());if(!additions.empty())selected_=additions.back().id;push(slot,true);}
                else report(L("Les paramètres copiés n'existent pas dans ce graphe.","Copied parameters do not exist in this graph."));
            }
        }
        if (selected_ != 0 && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            std::vector<Control> copies;
            for(const auto& c:interface_.controls)if(c.id==selected_ || selection_.contains(c.id)) {
                auto copy=c;copy.id=interface_.nextControlId++;copy.x=std::min(1-copy.width,copy.x+1.0f/interface_.gridColumns);copy.y=std::min(1-copy.height,copy.y+1.0f/interface_.gridRows);copies.push_back(std::move(copy));
            }
            selection_.clear();for(const auto& c:copies)selection_.insert(c.id);
            interface_.controls.insert(interface_.controls.end(),copies.begin(),copies.end());if(!copies.empty())selected_=copies.back().id;push(slot,true);
        }
        if (auto* control = find(selected_)) {
            const auto stepX = 1.0f / interface_.gridColumns;
            const auto stepY = 1.0f / interface_.gridRows;
            auto moved = false;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
                control->x = std::max(0.0f, control->x - stepX);
                moved = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
                control->x = std::min(1.0f - control->width, control->x + stepX);
                moved = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                control->y = std::max(0.0f, control->y - stepY);
                moved = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                control->y = std::min(1.0f - control->height, control->y + stepY);
                moved = true;
            }
            if (moved)
                (void) push(slot, true);
        }
    }
}

void InterfaceEditor::drawPalette(GraphSlot& slot)
{
    const auto& p = theme::palette();
    pull(slot);
    // A free place for a control added by a click: the first cell row by row.
    const auto freeSpot = [this](const KindInfo& info) {
        const auto columns = static_cast<int>(interface_.gridColumns);
        const auto rows = static_cast<int>(interface_.gridRows);
        for (auto row = 0; row + info.rows <= rows; ++row)
            for (auto column = 0; column + info.columns <= columns; ++column) {
                const auto x = static_cast<float>(column) / columns;
                const auto y = static_cast<float>(row) / rows;
                const auto w = static_cast<float>(info.columns) / columns;
                const auto h = static_cast<float>(info.rows) / rows;
                const auto clash = std::any_of(interface_.controls.begin(), interface_.controls.end(), [&](const Control& control) {
                    return !isPort(control.kind) && control.kind != Kind::group && x < control.x + control.width - 1.0e-4f
                           && x + w > control.x + 1.0e-4f && y < control.y + control.height - 1.0e-4f
                           && y + h > control.y + 1.0e-4f;
                });
                if (!clash)
                    return ImVec2(x, y);
            }
        return ImVec2(0.0f, 0.0f);
    };

    drawLayers(slot);
    drawNativePalette(slot);
    ui::sectionTitle(L("Contrôles", "Controls"));
    ImGui::TextDisabled("%s", L("Clic : ajouter. Glisser : où il tombe.", "Click: add. Drag: where it drops."));
    for (const auto& info : kinds) {
        ImGui::PushID(static_cast<int>(info.kind));
        if (ImGui::Selectable(L(info.fr, info.en)))
            (void) add(slot, info.kind, freeSpot(info), {});
        if (ImGui::BeginDragDropSource()) {
            const auto kind = static_cast<int>(info.kind);
            ImGui::SetDragDropPayload(kindPayload, &kind, sizeof(kind));
            ImGui::TextUnformatted(L(info.fr, info.en));
            ImGui::EndDragDropSource();
        }
        ImGui::PopID();
    }

    ImGui::Spacing();
    ui::sectionTitle(L("Réglages publiés", "Published settings"));
    ImGui::TextDisabled("%s", L("Glisser : un potard pour ce réglage.", "Drag: a knob for this setting."));
    if (interface_.parameters.empty())
        ImGui::TextDisabled("%s", L("Aucun : ajoutez des cartes au graphe.", "None: add cards to the graph."));
    for (const auto& item : interface_.parameters) {
        ImGui::PushID(item.key.toRawUTF8());
        const auto shown = std::any_of(interface_.controls.begin(), interface_.controls.end(), [&item](const Control& control) {
            return control.parameterBinding == item.key || control.secondaryParameterBinding == item.key
                   || control.tertiaryParameterBinding == item.key || control.quaternaryParameterBinding == item.key;
        });
        const auto start = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##parameter", false, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetTextLineHeight())))
            (void) add(slot, Kind::knob, freeSpot(kinds[0]), item.key);
        if (ImGui::BeginDragDropSource()) {
            const auto key = item.key.toStdString();
            ImGui::SetDragDropPayload(parameterPayload, key.c_str(), key.size() + 1);
            ImGui::TextUnformatted(item.label.toRawUTF8());
            ImGui::EndDragDropSource();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", item.key.toRawUTF8());
        auto* list = ImGui::GetWindowDrawList();
        list->AddCircleFilled(ImVec2(start.x + dp(5.0f), start.y + ImGui::GetTextLineHeight() * 0.5f), dp(3.0f),
                              shown ? p.accent : p.bg4, 8);
        ui::clippedText(list, ImVec2(start.x + dp(14.0f), start.y), ImGui::GetContentRegionAvail().x - dp(14.0f),
                        shown ? p.text : p.textDim, item.label.toRawUTF8());
        ImGui::PopID();
    }
}

void InterfaceEditor::drawProperties(GraphSlot& slot)
{
    pull(slot);
    const auto width = ImGui::GetContentRegionAvail().x;
    auto* control = find(selected_);
    drawWindowProperties(slot);
    if (control == nullptr || play_) {
        ImGui::TextWrapped("%s",L("Sélectionnez un contrôle pour régler son apparence et sa liaison à une valeur.",
                                 "Select a control to edit its appearance and value binding."));
        return;
    }
    ImGui::Separator();
    if (ImGui::SmallButton(L("Désélectionner le contrôle", "Deselect control"))) {
        selected_=0; selection_.clear(); return;
    }

    const auto* info = infoOf(control->kind);
    ui::sectionTitle(info != nullptr ? L(info->fr, info->en) : L("Contrôle", "Control"));
    ImGui::TextDisabled("%s", L("Libellé", "Label"));
    if (ImGui::GetActiveID() != ImGui::GetID("##controlLabel")) {
        const auto* text = control->label.toRawUTF8();
        std::snprintf(label_.data(), label_.size(), "%s", text);
    }
    ImGui::SetNextItemWidth(width);
    ImGui::InputText("##controlLabel", label_.data(), label_.size());
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        control->label = juce::String::fromUTF8(label_.data());
        (void) push(slot, true);
        control = find(selected_);
        if (control == nullptr)
            return;
    }

    // What it shows.
    const auto binding = [&](const char* fr, const char* en, juce::String& key) {
        ImGui::TextDisabled("%s", L(fr, en));
        const auto* item = parameter(key);
        ImGui::SetNextItemWidth(width);
        ImGui::PushID(L(fr, en));
        if (ImGui::BeginCombo("##binding", item != nullptr ? item->label.toRawUTF8() : "-")) {
            for (const auto& candidate : interface_.parameters)
                if (ImGui::Selectable(candidate.label.toRawUTF8(), candidate.key == key) && candidate.key != key) {
                    const auto before = key;
                    key = candidate.key;
                    if (!push(slot, true))
                        key = before;
                }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    };
    if ((info != nullptr && info->bindings >= 1) || control->kind==Kind::custom)
        binding(info != nullptr && info->bindings == 4 ? "Attaque" : info != nullptr && info->bindings == 2 ? "Réglage (horizontal, ou principal)" : "Réglage",
                info->bindings == 4 ? "Attack" : info->bindings == 2 ? "Setting (horizontal, or main)" : "Setting",
                control->parameterBinding);
    if (info != nullptr && (info->bindings == 2 || info->bindings == 4))
        if ((control = find(selected_)) != nullptr)
            binding(info->bindings == 4 ? "Déclin" : "Second réglage (vertical)", info->bindings == 4 ? "Decay" : "Second setting (vertical)",
                    control->secondaryParameterBinding);
    if (info != nullptr && info->bindings == 4) {
        if ((control = find(selected_)) != nullptr)
            binding("Maintien", "Sustain", control->tertiaryParameterBinding);
        if ((control = find(selected_)) != nullptr)
            binding("Relâchement", "Release", control->quaternaryParameterBinding);
    }
    control = find(selected_);
    if (control == nullptr)
        return;

    styleProperties(slot,*control);
    // Where, in percent of the window.
    ImGui::Spacing();
    ImGui::TextDisabled("%s", L("Position et taille (% de la fenêtre)", "Place and size (% of the window)"));
    float place[4] = { control->x * 100.0f, control->y * 100.0f, control->width * 100.0f, control->height * 100.0f };
    ImGui::SetNextItemWidth(width);
    if (ImGui::DragFloat4("##place", place, 0.2f, 0.0f, 100.0f, "%.1f")) {
        control->width = std::clamp(place[2] / 100.0f, 0.02f, 1.0f);
        control->height = std::clamp(place[3] / 100.0f, 0.02f, 1.0f);
        control->x = std::clamp(place[0] / 100.0f, 0.0f, 1.0f - control->width);
        control->y = std::clamp(place[1] / 100.0f, 0.0f, 1.0f - control->height);
        (void) push(slot, false);
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        (void) push(slot, true);

    ImGui::Spacing();
    const auto half = (width - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (ImGui::Button(L("Dupliquer", "Duplicate"), ImVec2(half, 0.0f))) {
        auto copy = *control;
        copy.id = interface_.nextControlId++;
        copy.x = std::min(1.0f - copy.width, copy.x + 1.0f / interface_.gridColumns);
        copy.y = std::min(1.0f - copy.height, copy.y + 1.0f / interface_.gridRows);
        interface_.controls.push_back(copy);
        selected_ = copy.id;
        (void) push(slot, true);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Retirer", "Remove"), ImVec2(half, 0.0f))) {
        std::erase_if(interface_.controls, [this](const Control& item) { return item.id == selected_; });
        selected_ = 0;
        (void) push(slot, true);
        return;
    }
    if (ImGui::Button(L("Au premier plan", "To the front"), ImVec2(half, 0.0f))) {
        const auto copy = *control;
        std::erase_if(interface_.controls, [this](const Control& item) { return item.id == selected_; });
        interface_.controls.push_back(copy);
        (void) push(slot, true);
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button(L("À l'arrière-plan", "To the back"), ImVec2(half, 0.0f))) {
        const auto copy = *control;
        std::erase_if(interface_.controls, [this](const Control& item) { return item.id == selected_; });
        interface_.controls.insert(interface_.controls.begin(), copy);
        (void) push(slot, true);
    }
}

} // namespace cydaw::v2
