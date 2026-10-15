#include "Widgets.h"

#include "Icons.h"
#include "Theme.h"

#include <imgui_internal.h>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <unordered_map>

namespace cydaw::v2::ui {
namespace {

bool gFrench = true;
juce::String gLanguage { "fr" };
// Another language's translations, keyed by the English text, and the
// translation each English literal (by address) was found to have.
std::unordered_map<std::string, std::string> gDictionary;
std::unordered_map<const char*, const char*> gTranslated;
constexpr float kPi = 3.14159265358979f;
constexpr float kArcStart = kPi * 0.75f;
constexpr float kArcSweep = kPi * 1.5f;

float toNormalised(const float value, const float minimum, const float maximum, const bool logarithmic)
{
    if (maximum <= minimum)
        return 0.0f;
    if (logarithmic && minimum > 0.0f)
        return std::clamp(std::log(value / minimum) / std::log(maximum / minimum), 0.0f, 1.0f);
    return std::clamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
}

float fromNormalised(const float t, const float minimum, const float maximum, const bool logarithmic)
{
    const auto clamped = std::clamp(t, 0.0f, 1.0f);
    if (logarithmic && minimum > 0.0f)
        return minimum * std::pow(maximum / minimum, clamped);
    return minimum + (maximum - minimum) * clamped;
}

void arc(ImDrawList* list, const ImVec2 centre, const float radius, const float from, const float to,
         const ImU32 colour, const float thickness)
{
    if (std::abs(to - from) < 0.001f)
        return;
    list->PathClear();
    list->PathArcTo(centre, radius, std::min(from, to), std::max(from, to), 32);
    list->PathStroke(colour, thickness);
}

std::string formatValue(const float value, const KnobStyle& style)
{
    if (style.valueText != nullptr && style.valueText[0] != 0)
        return style.valueText;
    char text[64];
    std::snprintf(text, sizeof(text), style.format != nullptr ? style.format : "%.2f",
                  (style.flags & knobInteger) != 0 ? std::round(value) : value);
    std::string out(text);
    if (style.unit != nullptr && style.unit[0] != 0) {
        out += ' ';
        out += style.unit;
    }
    return out;
}

} // namespace

float currentFontBase()
{
    return ImGui::GetCurrentContext()->FontSizeBase;
}

namespace {
int gModuleDepth = 0;
} // namespace

ModuleText::ModuleText() noexcept
{
    ++gModuleDepth;
}

ModuleText::~ModuleText()
{
    --gModuleDepth;
}

bool french()
{
    return gFrench && gModuleDepth == 0;
}

const char* L(const char* fr, const char* en)
{
    if (gModuleDepth > 0)
        return en;
    if (gFrench)
        return fr;
    if (gDictionary.empty() || en == nullptr)
        return en;
    if (const auto known = gTranslated.find(en); known != gTranslated.end())
        return known->second;
    const auto found = gDictionary.find(en);
    const auto* text = found != gDictionary.end() && !found->second.empty() ? found->second.c_str() : en;
    gTranslated.emplace(en, text);
    return text;
}

void setFrench(const bool value)
{
    setLanguage(value ? "fr" : "en");
}

void setLanguage(const juce::String& code)
{
    gLanguage = code.isNotEmpty() ? code : juce::String("en");
    gFrench = gLanguage == "fr";
    gTranslated.clear();
    gDictionary.clear();
    bumpLanguageRevision();
    if (gLanguage == "fr" || gLanguage == "en")
        return;
    const auto file = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                          .getSiblingFile("i18n")
                          .getChildFile("v2")
                          .getChildFile(gLanguage + ".json");
    if (!file.existsAsFile() || file.getSize() > 8 * 1024 * 1024)
        return;
    // The parsed value owns the object: kept alive while it is read.
    const auto parsed = juce::JSON::parse(file);
    if (const auto* object = parsed.getDynamicObject())
        for (const auto& property : object->getProperties())
            gDictionary.emplace(property.name.toString().toStdString(), property.value.toString().toStdString());
}

juce::String language()
{
    return gLanguage;
}

int translationCount()
{
    return static_cast<int>(gDictionary.size());
}

namespace {
int gLanguageRevision {};
}

int languageRevision()
{
    return gLanguageRevision;
}

void bumpLanguageRevision()
{
    ++gLanguageRevision;
}

std::string labelId(const char* label, const char* id)
{
    std::string out(label != nullptr ? label : "");
    out += "###";
    out += id;
    return out;
}

namespace {
int wheelFrame_ = -1;
}

void takeWheel()
{
    wheelFrame_ = ImGui::GetFrameCount();
}

bool wheelTaken()
{
    return wheelFrame_ == ImGui::GetFrameCount();
}

namespace {

bool scriptedPointer_ {};

} // namespace

ImVec2 heldMovement(const ImGuiID key)
{
    auto& io = ImGui::GetIO();
#if defined(_WIN32)
    if (!scriptedPointer_) {
        auto* storage = ImGui::GetStateStorage();
        POINT now {};
        if (::GetCursorPos(&now) != 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            if (ImGui::IsItemActivated()) {
                storage->SetInt(key, static_cast<int>(now.x));
                storage->SetInt(key + 1, static_cast<int>(now.y));
                return {};
            }
            const POINT anchor { static_cast<LONG>(storage->GetInt(key, now.x)), static_cast<LONG>(storage->GetInt(key + 1, now.y)) };
            const ImVec2 moved(static_cast<float>(now.x - anchor.x), static_cast<float>(now.y - anchor.y));
            if (moved.x != 0.0f || moved.y != 0.0f)
                ::SetCursorPos(anchor.x, anchor.y);
            return moved;
        }
    }
#else
    (void) key;
#endif
    return io.MouseDelta;
}

void setScriptedPointer(const bool scripted)
{
    scriptedPointer_ = scripted;
}

float valueSpeed()
{
    const auto& io = ImGui::GetIO();
    return io.KeyCtrl ? 0.15f : io.KeyShift ? 4.0f : 1.0f;
}

namespace {

// The field being typed in: whose, what it holds, whether it was just opened.
ImGuiID entryId_ {};
char entryText_[64] {};
bool entryFresh_ {};

// What was typed as a number: a comma for a point, "k" for a thousand; none
// when it holds no number.
std::optional<double> typedNumber(std::string text, const std::string& unit)
{
    std::replace(text.begin(), text.end(), ',', '.');
    const auto* start = text.c_str();
    while (*start == ' ')
        ++start;
    char* end = nullptr;
    const auto number = std::strtod(start, &end);
    if (end == start || !std::isfinite(number))
        return std::nullopt;
    while (end != nullptr && *end == ' ')
        ++end;
    const std::string rest = end != nullptr ? end : "";
    if (!rest.empty() && (rest[0] == 'k' || rest[0] == 'K'))
        return number * 1'000.0;
    // A time typed in the other unit than the setting's.
    if (unit == "s" && rest.rfind("ms", 0) == 0)
        return number / 1'000.0;
    if (unit == "ms" && rest.rfind("s", 0) == 0)
        return number * 1'000.0;
    return number;
}

std::string entryNumber(const double shown)
{
    char text[64];
    std::snprintf(text, sizeof(text), "%.6g", shown);
    return text;
}

} // namespace

bool valueEntryOpen()
{
    return entryId_ != 0 && ImGui::IsPopupOpen(entryId_, ImGuiPopupFlags_None);
}

bool valueEntry(const ImGuiID id, const ImVec2 min, const ImVec2 max, double& value, const double minimum,
                const double maximum, const bool open, const double scale, const char* unit)
{
    if (open) {
        entryId_ = id;
        const auto shown = entryNumber(value * scale);
        std::snprintf(entryText_, sizeof(entryText_), "%s", shown.c_str());
        entryFresh_ = true;
        ImGui::OpenPopupEx(id, ImGuiPopupFlags_None);
    }
    if (entryId_ != id)
        return false;
    const auto width = std::max(max.x - min.x, dp(72.0f));
    ImGui::SetNextWindowPos(ImVec2((min.x + max.x - width) * 0.5f, min.y - dp(3.0f)));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(dp(2.0f), dp(2.0f)));
    const auto shown = ImGui::BeginPopupEx(id, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                                                   | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::PopStyleVar();
    if (!shown) {
        entryId_ = 0;
        return false;
    }
    auto set = false;
    if (entryFresh_) {
        ImGui::SetKeyboardFocusHere();
        entryFresh_ = false;
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##typed", entryText_, sizeof(entryText_),
                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        if (const auto typed = typedNumber(entryText_, unit != nullptr ? unit : "")) {
            value = std::clamp(*typed / (scale != 0.0 ? scale : 1.0), std::min(minimum, maximum), std::max(minimum, maximum));
            set = true;
        }
        ImGui::CloseCurrentPopup();
        entryId_ = 0;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        entryId_ = 0;
    }
    ImGui::EndPopup();
    return set;
}

bool valueEntryButton(const char* id, const ImVec2 size, double& value, const double minimum, const double maximum,
                      const double scale, const char* unit)
{
    const auto min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)));
    const auto key = ImGui::GetItemID();
    const auto open = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    return valueEntry(key, min, ImVec2(min.x + size.x, min.y + size.y), value, minimum, maximum, open, scale, unit);
}

bool knob(const char* id, float* value, const float minimum, const float maximum, const float defaultValue,
          const KnobStyle& style)
{
    const auto& p = theme::palette();
    auto& io = ImGui::GetIO();
    const auto diameter = dp(style.diameter);
    const auto labelHeight = (style.flags & knobNoLabel) != 0 ? 0.0f : ImGui::GetFontSize() * 0.86f;
    const auto width = std::max(diameter, dp(style.diameter + 12.0f));
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::InvisibleButton("##knob", ImVec2(width, diameter + labelHeight));
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    const auto logarithmic = (style.flags & knobLogarithmic) != 0;
    auto changed = false;
    auto t = toNormalised(*value, minimum, maximum, logarithmic);
    const auto steps = std::max(0, style.steps);
    const auto onStep = [steps](const float position) {
        return steps > 0 ? std::round(position * static_cast<float>(steps)) / static_cast<float>(steps) : position;
    };

    // Dragged: where the drag has taken it, kept between frames unrounded,
    // so that small moves add up to a step of a stepped setting.
    auto* storage = ImGui::GetStateStorage();
    const auto heldKey = ImGui::GetID("##held");
    const auto movedKey = ImGui::GetID("##moved");
    const auto knobId = ImGui::GetItemID();
    const auto movement = active ? heldMovement(ImGui::GetID("##anchor")) : ImVec2();
    if (ImGui::IsItemActivated()) {
        storage->SetFloat(heldKey, t);
        storage->SetBool(movedKey, false);
    }
    if (active && (movement.y != 0.0f || movement.x != 0.0f)) {
        storage->SetBool(movedKey, true);
        const auto speed = 0.0055f * valueSpeed() / std::max(0.5f, ImGui::GetStyle().FontScaleDpi);
        const auto held = std::clamp(storage->GetFloat(heldKey, t) + (-movement.y + movement.x * 0.35f) * speed, 0.0f, 1.0f);
        storage->SetFloat(heldKey, held);
        t = onStep(held);
        changed = true;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        // A notch: one step of a stepped setting, else a little.
        const auto notch = steps > 0 ? 1.0f / static_cast<float>(steps) : 0.02f * valueSpeed();
        t = onStep(std::clamp(t + io.MouseWheel * notch, 0.0f, 1.0f));
        changed = true;
        takeWheel();
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        t = toNormalised(defaultValue, minimum, maximum, logarithmic);
        storage->SetFloat(heldKey, t);
        changed = true;
    }
    // Its number, under it, clicked without a drag: typed in.
    const auto onNumber = labelHeight > 0.0f && io.MouseClickedPos[0].y >= origin.y + diameter;
    const auto openEntry = ImGui::IsItemDeactivated() && !storage->GetBool(movedKey, false) && onNumber
                           && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    auto typedIn = false;
    if (labelHeight > 0.0f) {
        auto typed = static_cast<double>(*value);
        if (valueEntry(knobId, ImVec2(origin.x, origin.y + diameter), ImVec2(origin.x + width, origin.y + diameter + labelHeight), typed,
                       minimum, maximum, openEntry)) {
            *value = (style.flags & knobInteger) != 0 ? std::round(static_cast<float>(typed)) : static_cast<float>(typed);
            t = toNormalised(*value, minimum, maximum, logarithmic);
            typedIn = true;
        }
    }
    if (changed) {
        auto next = fromNormalised(t, minimum, maximum, logarithmic);
        if ((style.flags & knobInteger) != 0)
            next = std::round(next);
        changed = next != *value;
        *value = next;
        t = toNormalised(*value, minimum, maximum, logarithmic);
    }
    changed = changed || typedIn;

    auto* list = ImGui::GetWindowDrawList();
    const auto colour = style.colour != 0 ? style.colour : p.accent;
    const ImVec2 centre(origin.x + width * 0.5f, origin.y + diameter * 0.5f);
    const auto radius = diameter * 0.5f - dp(2.0f);
    const auto thickness = std::max(2.0f, diameter * 0.095f);
    arc(list, centre, radius, kArcStart, kArcStart + kArcSweep, p.bg5, thickness);
    const auto bipolar = (style.flags & knobBipolar) != 0;
    const auto from = bipolar ? kArcStart + kArcSweep * 0.5f : kArcStart;
    const auto to = kArcStart + kArcSweep * t;
    arc(list, centre, radius, from, to, active || hovered ? theme::brighter(colour, 0.15f) : colour, thickness);
    list->AddCircleFilled(centre, radius - thickness * 0.9f, hovered || active ? p.bg5 : p.bg4, 24);
    const auto angle = kArcStart + kArcSweep * t;
    const ImVec2 tip(centre.x + std::cos(angle) * (radius - thickness * 1.1f),
                     centre.y + std::sin(angle) * (radius - thickness * 1.1f));
    const ImVec2 inner(centre.x + std::cos(angle) * radius * 0.25f, centre.y + std::sin(angle) * radius * 0.25f);
    list->AddLine(inner, tip, p.text, std::max(1.5f, thickness * 0.55f));
    if (style.modulation >= 0.0f) {
        const auto m = kArcStart + kArcSweep * std::clamp(style.modulation, 0.0f, 1.0f);
        list->AddCircleFilled(ImVec2(centre.x + std::cos(m) * radius, centre.y + std::sin(m) * radius),
                              thickness * 0.7f, p.modulation, 12);
    }

    if (labelHeight > 0.0f) {
        const auto showValue = hovered || active;
        const auto text = showValue ? formatValue(*value, style) : std::string(style.label != nullptr ? style.label : "");
        // Its room: the knob's width and a little, or more when its cell gives it.
        const auto room = std::max(width + dp(12.0f), style.labelWidth);
        auto scale = 0.82f;
        ImGui::PushFont(nullptr, currentFontBase() * scale);
        auto size = ImGui::CalcTextSize(text.c_str());
        if (size.x > room) {
            ImGui::PopFont();
            scale = std::max(0.68f, scale * room / size.x);
            ImGui::PushFont(nullptr, currentFontBase() * scale);
            size = ImGui::CalcTextSize(text.c_str());
        }
        const auto x = origin.x + (width - std::min(size.x, room)) * 0.5f;
        list->PushClipRect(ImVec2(origin.x + (width - room) * 0.5f, origin.y + diameter),
                           ImVec2(origin.x + (width + room) * 0.5f, origin.y + diameter + labelHeight + dp(2.0f)), true);
        list->AddText(ImVec2(x, origin.y + diameter + dp(1.0f)), showValue ? p.text : p.textDim, text.c_str());
        list->PopClipRect();
        ImGui::PopFont();
    } else if (hovered || active) {
        ImGui::SetTooltip("%s", formatValue(*value, style).c_str());
    }
    ImGui::PopID();
    return changed;
}

float meterPosition(const float gainDb)
{
    if (gainDb <= -70.0f)
        return 0.0f;
    return std::clamp(std::exp((gainDb - 6.0f) / 26.0f), 0.0f, 1.0f);
}

namespace {

float positionToDb(const float position, const float minimumDb, const float maximumDb)
{
    if (position <= 0.0f)
        return minimumDb;
    return std::clamp(6.0f + 26.0f * std::log(position), minimumDb, maximumDb);
}

} // namespace

bool fader(const char* id, float* gainDb, const float minimumDb, const float maximumDb, const ImVec2 size,
           const ImU32 colour)
{
    const auto& p = theme::palette();
    auto& io = ImGui::GetIO();
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::InvisibleButton("##fader", size);
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    auto changed = false;
    const auto travel = size.y - dp(14.0f);
    auto position = meterPosition(*gainDb);
    if (active && io.MouseDelta.y != 0.0f) {
        position = std::clamp(position - io.MouseDelta.y / travel * valueSpeed(), 0.0f, 1.0f);
        changed = true;
    }
    if (hovered && io.MouseWheel != 0.0f) {
        takeWheel();
        *gainDb = std::clamp(*gainDb + io.MouseWheel * valueSpeed(), minimumDb, maximumDb);
        position = meterPosition(*gainDb);
        changed = true;
    } else if (changed) {
        *gainDb = positionToDb(position, minimumDb, maximumDb);
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *gainDb = 0.0f;
        changed = true;
    }
    auto* list = ImGui::GetWindowDrawList();
    const auto centreX = origin.x + size.x * 0.5f;
    const auto top = origin.y + dp(7.0f);
    const auto bottom = top + travel;
    // Scale ticks.
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.7f);
    for (const auto tick : { 6.0f, 0.0f, -6.0f, -12.0f, -24.0f, -48.0f }) {
        const auto y = bottom - meterPosition(tick) * travel;
        list->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + dp(4.0f), y), p.textFaint);
    }
    ImGui::PopFont();
    list->AddRectFilled(ImVec2(centreX - dp(2.0f), top), ImVec2(centreX + dp(2.0f), bottom), p.bg0, dp(2.0f));
    const auto handleY = bottom - meterPosition(*gainDb) * travel;
    const auto fill = colour != 0 ? colour : p.accent;
    list->AddRectFilled(ImVec2(centreX - dp(2.0f), handleY), ImVec2(centreX + dp(2.0f), bottom),
                        theme::alpha(fill, 0.55f), dp(2.0f));
    const auto handleWidth = std::min(size.x - dp(4.0f), dp(22.0f));
    const ImVec2 handleMin(centreX - handleWidth * 0.5f, handleY - dp(6.0f));
    const ImVec2 handleMax(centreX + handleWidth * 0.5f, handleY + dp(6.0f));
    list->AddRectFilled(handleMin, handleMax, hovered || active ? theme::brighter(p.bg5, 0.25f) : p.bg5, dp(3.0f));
    list->AddLine(ImVec2(handleMin.x + dp(3.0f), handleY), ImVec2(handleMax.x - dp(3.0f), handleY), p.text, 1.5f);
    if (hovered || active)
        ImGui::SetTooltip("%.1f dB", *gainDb);
    ImGui::PopID();
    return changed;
}

void meter(const char* id, const ImVec2 size, const float peakLeft, const float peakRight, const float rmsLeft,
           const float rmsRight, const bool vertical)
{
    const auto& p = theme::palette();
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::Dummy(size);
    auto* storage = ImGui::GetStateStorage();
    const auto holdId = ImGui::GetID("hold");
    const auto holdTimeId = ImGui::GetID("holdTime");
    auto* list = ImGui::GetWindowDrawList();
    const auto now = static_cast<float>(ImGui::GetTime());
    const auto toDb = [](const float gain) { return gain > 1.0e-5f ? 20.0f * std::log10(gain) : -100.0f; };
    const auto peakDb = std::max(toDb(peakLeft), toDb(peakRight));
    auto hold = storage->GetFloat(holdId, -100.0f);
    auto holdTime = storage->GetFloat(holdTimeId, 0.0f);
    if (peakDb >= hold || now - holdTime > 1.5f) {
        hold = peakDb;
        holdTime = now;
    }
    storage->SetFloat(holdId, hold);
    storage->SetFloat(holdTimeId, holdTime);

    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(2.0f));
    const auto gap = dp(1.0f);
    const auto drawBar = [&](const int index, const float peak, const float rms) {
        const auto peakPos = meterPosition(toDb(peak));
        const auto rmsPos = meterPosition(toDb(rms));
        const auto segment = [&](const float from, const float to, const ImU32 colour) {
            if (vertical) {
                const auto barWidth = (size.x - gap) * 0.5f;
                const auto x0 = origin.x + index * (barWidth + gap);
                list->AddRectFilled(ImVec2(x0, origin.y + size.y * (1.0f - to)),
                                    ImVec2(x0 + barWidth, origin.y + size.y * (1.0f - from)), colour);
            } else {
                const auto barHeight = (size.y - gap) * 0.5f;
                const auto y0 = origin.y + index * (barHeight + gap);
                list->AddRectFilled(ImVec2(origin.x + size.x * from, y0), ImVec2(origin.x + size.x * to, y0 + barHeight),
                                    colour);
            }
        };
        const auto mid = meterPosition(-12.0f);
        const auto high = meterPosition(-3.0f);
        const auto draw = [&](const float level, const float alphaValue) {
            segment(0.0f, std::min(level, mid), theme::alpha(p.meterLow, alphaValue));
            if (level > mid)
                segment(mid, std::min(level, high), theme::alpha(p.meterMid, alphaValue));
            if (level > high)
                segment(high, level, theme::alpha(p.meterHigh, alphaValue));
        };
        draw(peakPos, 0.45f);
        draw(rmsPos, 1.0f);
    };
    drawBar(0, peakLeft, rmsLeft);
    drawBar(1, peakRight, rmsRight);
    const auto holdPos = meterPosition(hold);
    if (holdPos > 0.0f) {
        const auto colour = hold > -0.1f ? p.meterHigh : p.text;
        if (vertical) {
            const auto y = origin.y + size.y * (1.0f - holdPos);
            list->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), theme::alpha(colour, 0.8f), 1.0f);
        } else {
            const auto x = origin.x + size.x * holdPos;
            list->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), theme::alpha(colour, 0.8f), 1.0f);
        }
    }
    ImGui::PopID();
}

bool toggle(const char* label, bool* value, const ImU32 onColour, ImVec2 size, const char* tip)
{
    const auto& p = theme::palette();
    const auto textSize = ImGui::CalcTextSize(label, nullptr, true);
    if (size.x <= 0.0f)
        size.x = textSize.x + ImGui::GetStyle().FramePadding.x * 2.0f;
    if (size.y <= 0.0f)
        size.y = ImGui::GetFrameHeight();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto clicked = ImGui::InvisibleButton(label, size);
    if (clicked)
        *value = !*value;
    const auto hovered = ImGui::IsItemHovered();
    auto* list = ImGui::GetWindowDrawList();
    const auto on = *value;
    const auto fill = on ? onColour : (hovered ? p.bg4 : p.bg3);
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), fill, ImGui::GetStyle().FrameRounding);
    const auto textColour = on ? p.bg0 : (hovered ? p.text : p.textDim);
    const auto* end = ImGui::FindRenderedTextEnd(label);
    list->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f, origin.y + (size.y - textSize.y) * 0.5f),
                  textColour, label, end);
    if (tip != nullptr)
        tooltip(tip);
    return clicked;
}

bool iconButton(const char* icon, const char* tip, const bool active, const ImU32 activeColour, ImVec2 size)
{
    const auto& p = theme::palette();
    if (size.x <= 0.0f)
        size.x = ImGui::GetFrameHeight() + dp(2.0f);
    if (size.y <= 0.0f)
        size.y = ImGui::GetFrameHeight();
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(icon);
    ImGui::PushID(tip != nullptr ? tip : "");
    const auto clicked = ImGui::InvisibleButton("##icon", size);
    const auto hovered = ImGui::IsItemHovered();
    const auto held = ImGui::IsItemActive();
    ImGui::PopID();
    ImGui::PopID();
    auto* list = ImGui::GetWindowDrawList();
    const auto colour = activeColour != 0 ? activeColour : p.accent;
    if (active)
        list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), theme::alpha(colour, 0.18f),
                            ImGui::GetStyle().FrameRounding);
    else if (hovered)
        list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), held ? p.bg5 : p.bg4,
                            ImGui::GetStyle().FrameRounding);
    const auto textSize = ImGui::CalcTextSize(icon);
    list->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f, origin.y + (size.y - textSize.y) * 0.5f),
                  active ? colour : (hovered ? p.text : p.textDim), icon);
    if (tip != nullptr)
        tooltip(tip);
    return clicked;
}

bool primaryButton(const char* label, const ImVec2 size)
{
    const auto& p = theme::palette();
    ImGui::PushStyleColor(ImGuiCol_Button, p.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::brighter(p.accent, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Text, p.bg0);
    const auto clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

void toolbarSeparator()
{
    ImGui::SameLine(0.0f, dp(6.0f));
    const auto origin = ImGui::GetCursorScreenPos();
    const auto height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, origin.y + height * 0.18f),
                                        ImVec2(origin.x, origin.y + height * 0.82f), theme::palette().lineStrong);
    ImGui::Dummy(ImVec2(dp(1.0f), height));
    ImGui::SameLine(0.0f, dp(6.0f));
}

void pushTextWrapAt(const float screenX)
{
    ImGui::PushTextWrapPos(std::max(1.0f, screenX - ImGui::GetWindowPos().x + ImGui::GetScrollX()));
}

void tooltip(const char* text)
{
    if (text != nullptr && text[0] != 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay))
        ImGui::SetTooltip("%s", text);
}

bool timelineBand(const char* id, const ImVec2 min, const ImVec2 max, double& position, double& pixelsPerUnit,
                  const double minimumZoom, const double maximumZoom, const double tick, const double strong,
                  const double minimumPosition)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto& io = ImGui::GetIO();
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    auto* storage = ImGui::GetStateStorage();
    const auto key = ImGui::GetItemID();
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (ImGui::IsItemActivated())
        storage->SetFloat(key, static_cast<float>(position + static_cast<double>(io.MousePos.x - min.x) / pixelsPerUnit));
    if (active) {
        const auto anchor = static_cast<double>(storage->GetFloat(key, static_cast<float>(position)));
        if (io.MouseDelta.y != 0.0f)
            pixelsPerUnit = std::clamp(pixelsPerUnit * std::exp(static_cast<double>(io.MouseDelta.y) * 0.012), minimumZoom, maximumZoom);
        position = std::max(minimumPosition, anchor - static_cast<double>(io.MousePos.x - min.x) / pixelsPerUnit);
    }
    const auto fit = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    list->AddRectFilled(min, max, active ? p.bg3 : (hovered ? theme::mix(p.bg2, p.bg3, 0.5f) : p.bg2));
    auto step = std::max(1.0e-6, tick);
    while (step * pixelsPerUnit < 8.0)
        step *= 2.0;
    for (auto unit = std::floor(position / step) * step;; unit += step) {
        const auto x = min.x + static_cast<float>((unit - position) * pixelsPerUnit);
        if (x > max.x)
            break;
        if (x < min.x)
            continue;
        const auto strongTick = strong > 0.0 && std::abs(std::remainder(unit, strong)) < step * 1.0e-3;
        list->AddLine(ImVec2(x, strongTick ? min.y + dp(3.0f) : (min.y + max.y) * 0.5f), ImVec2(x, max.y - dp(1.0f)),
                      theme::alpha(p.textFaint, strongTick ? 0.55f : 0.25f));
    }
    list->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), p.line);
    if (hovered && !active)
        tooltip(L("Glisser : à gauche et à droite pour se déplacer, vers le bas pour zoomer, vers le haut pour "
                  "dézoomer. Double-clic : tout voir.",
                  "Drag: left and right to move, down to zoom in, up to zoom out. Double click: zoom to fit."));
    return fit;
}

bool rowsBand(const char* id, const ImVec2 min, const ImVec2 max, float& scroll, float& zoom, const float minimumZoom,
              const float maximumZoom)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto& io = ImGui::GetIO();
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    auto* storage = ImGui::GetStateStorage();
    const auto key = ImGui::GetItemID();
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    // The content grabbed, in units of the zoom: it stays under the pointer.
    if (ImGui::IsItemActivated())
        storage->SetFloat(key, (io.MousePos.y - min.y + scroll) / std::max(1.0e-3f, zoom));
    if (active) {
        const auto anchor = storage->GetFloat(key, 0.0f);
        if (io.MouseDelta.x != 0.0f)
            zoom = std::clamp(zoom * std::exp(-io.MouseDelta.x * 0.012f), minimumZoom, maximumZoom);
        scroll = std::max(0.0f, anchor * zoom - (io.MousePos.y - min.y));
    }
    const auto fit = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    list->AddRectFilled(min, max, active ? p.bg3 : (hovered ? theme::mix(p.bg2, p.bg3, 0.5f) : p.bg2));
    // Its marks: the rows' spacing, so the zoom shows.
    const auto spacing = std::max(dp(6.0f), dp(12.0f) * zoom / std::max(1.0e-3f, (minimumZoom + maximumZoom) * 0.25f));
    for (auto y = min.y - std::fmod(scroll, spacing); y < max.y; y += spacing)
        if (y > min.y)
            list->AddLine(ImVec2((min.x + max.x) * 0.5f, y), ImVec2(max.x - dp(2.0f), y), theme::alpha(p.textFaint, 0.3f));
    list->AddLine(ImVec2(min.x + 0.5f, min.y), ImVec2(min.x + 0.5f, max.y), p.line);
    if (hovered && !active)
        tooltip(L("Glisser : vers le haut et le bas pour se déplacer, vers la gauche pour agrandir, vers la droite pour "
                  "réduire. Double-clic : taille normale.",
                  "Drag: up and down to move, left to make bigger, right to make smaller. Double click: normal size."));
    return fit;
}

bool dragNumber(const char* id, float* value, const float speed, const float minimum, const float maximum,
                const char* format, const float width)
{
    ImGui::SetNextItemWidth(width);
    return ImGui::DragFloat(id, value, speed, minimum, maximum, format, ImGuiSliderFlags_AlwaysClamp);
}

void sectionTitle(const char* text)
{
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 0.85f);
    ImGui::PushStyleColor(ImGuiCol_Text, theme::palette().textDim);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

bool colourChip(const char* id, const ImU32 colour, const ImVec2 size)
{
    const auto origin = ImGui::GetCursorScreenPos();
    const auto clicked = ImGui::InvisibleButton(id, size);
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), colour,
                                              dp(2.0f));
    return clicked;
}

void centredText(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour, const char* text)
{
    const auto size = ImGui::CalcTextSize(text);
    list->AddText(ImVec2(min.x + (max.x - min.x - size.x) * 0.5f, min.y + (max.y - min.y - size.y) * 0.5f), colour,
                  text);
}

juce::String sizeText(const juce::int64 bytes)
{
    const auto value = static_cast<double>(bytes);
    juce::String text;
    if (value >= 1e9)
        text = juce::String(value / 1e9, 1) + L(" Go", " GB");
    else if (value >= 1e6)
        text = juce::String(juce::roundToInt(value / 1e6)) + L(" Mo", " MB");
    else if (value >= 1e3)
        text = juce::String(juce::roundToInt(value / 1e3)) + L(" Ko", " kB");
    else
        text = juce::String(bytes) + L(" o", " B");
    return french() ? text.replaceCharacter('.', ',') : text;
}

void emptyState(const char* title, const char* message)
{
    const auto& p = theme::palette();
    const auto area = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorScreenPos();
    auto* list = ImGui::GetWindowDrawList();
    const auto middle = origin.y + std::max(dp(40.0f), area.y * 0.45f);
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.05f);
    centredText(list, ImVec2(origin.x, middle - dp(22.0f)), ImVec2(origin.x + area.x, middle), p.textDim, title);
    ImGui::PopFont();
    centredText(list, ImVec2(origin.x, middle), ImVec2(origin.x + area.x, middle + dp(20.0f)), p.textFaint, message);
}

void clippedText(ImDrawList* list, const ImVec2 position, const float maxWidth, const ImU32 colour, const char* text)
{
    if (maxWidth <= 4.0f)
        return;
    const auto size = ImGui::CalcTextSize(text);
    if (size.x <= maxWidth) {
        list->AddText(position, colour, text);
        return;
    }
    list->PushClipRect(position, ImVec2(position.x + maxWidth, position.y + size.y + 2.0f), true);
    list->AddText(position, colour, text);
    list->PopClipRect();
}

// ---- Windows of their own -------------------------------------------------

namespace {

struct Detachment {
    bool detached {};
    // It was out, its class to give back.
    bool wasDetached {};
    // Frames left to place it (where it goes, or back over the studio).
    int placing {};
};

std::unordered_map<std::string, Detachment>& detachments()
{
    static std::unordered_map<std::string, Detachment> map;
    return map;
}

} // namespace

namespace {
bool gEmbedded = false;
} // namespace

void setEmbedded(const bool embedded)
{
    gEmbedded = embedded;
}

bool embedded()
{
    return gEmbedded;
}

bool beginEditorWindow(const char* name, bool* open, const ImGuiWindowFlags flags)
{
    if (!gEmbedded)
        return ImGui::Begin(name, open, flags);
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);
    return ImGui::Begin(name, nullptr,
                        flags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
                            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
}

void beforeDetachable(const char* id)
{
    if (gEmbedded)
        return;
    auto& detachment = detachments()[id];
    if (detachment.placing > 0) {
        // Out: over the right half of the studio, a window of its own the
        // person drags anywhere. Back: in the middle of the studio, where
        // it joins it again.
        const auto* main = ImGui::GetMainViewport();
        if (detachment.detached)
            ImGui::SetNextWindowPos(ImVec2(main->Pos.x + main->Size.x * 0.52f, main->Pos.y + main->Size.y * 0.1f), ImGuiCond_Always);
        else
            ImGui::SetNextWindowPos(ImVec2(main->Pos.x + main->Size.x * 0.5f, main->Pos.y + main->Size.y * 0.45f), ImGuiCond_Always,
                                    ImVec2(0.5f, 0.5f));
        --detachment.placing;
    }
    if (detachment.detached) {
        // Its own platform window even when moved back over the studio.
        ImGuiWindowClass own;
        own.ViewportFlagsOverrideSet = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&own);
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    } else if (detachment.placing > 0 || detachment.wasDetached) {
        // A window keeps the class it was last given: an ordinary one again,
        // so it joins the studio's window where it now is.
        ImGuiWindowClass ordinary;
        ordinary.ViewportFlagsOverrideClear = ImGuiViewportFlags_NoAutoMerge;
        ImGui::SetNextWindowClass(&ordinary);
    }
    detachment.wasDetached = detachment.detached || detachment.placing > 0;
}

bool isDetached(const char* id)
{
    const auto found = detachments().find(id);
    return found != detachments().end() && found->second.detached;
}

void setDetached(const char* id, const bool detached)
{
    auto& detachment = detachments()[id];
    if (detachment.detached == detached)
        return;
    detachment.detached = detached;
    // A few frames: the first may come before the window exists.
    detachment.placing = 3;
}

bool detachButton(const char* id, const ImVec2 size)
{
    // A plug-in's window has nothing to take out: its place kept empty.
    if (gEmbedded) {
        ImGui::Dummy(size);
        return false;
    }
    const auto detached = isDetached(id);
    ImGui::PushID(id);
    const auto pressed = iconButton(detached ? icon::backToWindow : icon::newWindow,
                                    detached ? L("Remettre dans la fenêtre du studio", "Back into the studio's window")
                                             : L("Fenêtre séparée : hors du studio, comme un plug-in (sur un autre écran)",
                                                 "Separate window: out of the studio, as a plug-in's (onto another screen)"),
                                    detached, theme::palette().accent, size);
    ImGui::PopID();
    if (pressed)
        setDetached(id, !detached);
    return pressed;
}

} // namespace cydaw::v2::ui
