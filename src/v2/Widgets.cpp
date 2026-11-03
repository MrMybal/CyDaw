#include "Widgets.h"

#include "Icons.h"
#include "Theme.h"
#include <cyui/Core.h>
#include <cyui/modules/Audio.h>
#include <cyui/modules/Canvas.h>

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
#include <cstring>
#include <map>
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

void arc(ImDrawList* list, const ImVec2 centre, const float radius, const float from, const float to,
         const ImU32 colour, const float thickness)
{
    if (std::abs(to - from) < 0.001f)
        return;
    list->PathClear();
    list->PathArcTo(centre, radius, std::min(from, to), std::max(from, to), 32);
    list->PathStroke(colour, thickness);
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
    cyui::setLocale("en");
}

ModuleText::~ModuleText()
{
    --gModuleDepth;
    cyui::setLocale(gModuleDepth > 0 ? "en" : gLanguage.toRawUTF8());
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
    cyui::setLocale(gLanguage.toRawUTF8());
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

void takeWheel() { cyui::takeWheel(); }
bool wheelTaken() { return cyui::wheelTaken(); }


namespace {

bool scriptedPointer_ {};

} // namespace

static ImVec2 pointerMovement(const ImGuiID key, bool activated)
{
    auto& io = ImGui::GetIO();
#if defined(_WIN32)
    if (!scriptedPointer_) {
        auto* storage = ImGui::GetStateStorage();
        POINT now {};
        if (::GetCursorPos(&now) != 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            if (activated) {
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

ImVec2 heldMovement(ImGuiID key) { return pointerMovement(key, ImGui::IsItemActivated()); }

void setScriptedPointer(const bool scripted)
{
    scriptedPointer_ = scripted;
}

float valueSpeed() { return cyui::valueSpeed(); }
namespace {
// CyUI's ordinary numeric entry handles decimal/k values. CyDAW also accepts
// time units (250 ms in a seconds field and vice versa); keep that host policy.
struct TimeEntry { ImGuiID id {}; char text[64] {}; bool fresh {}; };
TimeEntry& timeEntry()
{
    static std::map<ImGuiContext*, TimeEntry> entries;
    auto [it, added] = entries.try_emplace(ImGui::GetCurrentContext());
    if (added) {
        ImGuiContextHook hook; hook.Type = ImGuiContextHookType_Shutdown;
        hook.Callback = [](ImGuiContext* context, ImGuiContextHook*) { entries.erase(context); };
        ImGui::AddContextHook(ImGui::GetCurrentContext(), &hook);
    }
    return it->second;
}
}
bool valueEntryOpen()
{
    const auto id = timeEntry().id;
    return cyui::valueEntryOpen() || (id && ImGui::IsPopupOpen(id, ImGuiPopupFlags_None));
}
bool valueEntry(ImGuiID id, ImVec2 min, ImVec2 max, double& value, double low, double high,
                bool open, double scale, const char* unit)
{
    const std::string units = unit ? unit : "";
    if (units != "s" && units != "ms") return cyui::valueEntry(id, min, max, value, low, high, open, scale);
    auto& entry = timeEntry();
    if (open) {
        entry.id = id; entry.fresh = true;
        std::snprintf(entry.text, sizeof(entry.text), "%.6g", value * scale);
        ImGui::OpenPopupEx(id, ImGuiPopupFlags_None);
    }
    if (entry.id != id) return false;
    ImGui::SetNextWindowPos(min);
    ImGui::SetNextWindowSize(ImVec2(std::max(max.x-min.x, dp(84)), 0));
    if (!ImGui::BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize)) { entry.id = 0; return false; }
    if (entry.fresh) { ImGui::SetKeyboardFocusHere(); entry.fresh = false; }
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##time", entry.text, sizeof(entry.text), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
        auto text = std::string(entry.text); std::replace(text.begin(), text.end(), ',', '.');
        char* end {}; const auto number = std::strtod(text.c_str(), &end);
        if (end != text.c_str() && std::isfinite(number)) {
            while (*end == ' ') ++end;
            const std::string suffix(end);
            const auto factor = suffix.starts_with("ms") && units == "s" ? .001
                              : suffix.starts_with("s") && units == "ms" ? 1000.
                              : suffix.starts_with("k") || suffix.starts_with("K") ? 1000. : 1.;
            value = std::clamp(number * factor / (scale == 0 ? 1 : scale), std::min(low, high), std::max(low, high));
            changed = true;
        }
        ImGui::CloseCurrentPopup(); entry.id = 0;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); entry.id = 0; }
    ImGui::EndPopup();
    return changed;
}
bool valueEntryButton(const char* id, ImVec2 size, double& value, double low, double high,
                      double scale, const char* unit)
{
    const auto min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    return valueEntry(ImGui::GetItemID(), min, ImVec2(min.x + size.x, min.y + size.y),
                           value, low, high, ImGui::IsItemClicked(), scale, unit);
}


// ---- Controllers: learning ------------------------------------------------------------------

namespace {
struct LearnState {
    bool mode {};
    std::vector<std::pair<std::string, std::string>> scopes;
    std::vector<std::pair<std::string, std::string>> tracks;
    std::vector<LearnSpot> spots;
    // The knobs' bindings and names, learn mode or not (modulators).
    std::vector<std::pair<std::string, std::string>> bindings;
};
LearnState& learnState()
{
    static LearnState state;
    return state;
}
struct SpotRecording {
    bool on {};
    std::map<std::string, LearnSpot> spots;
};
SpotRecording& spotRecording()
{
    static SpotRecording recording;
    return recording;
}
ModulationHost*& hostSlot()
{
    static ModulationHost* host {};
    return host;
}
const std::string& scopedLabel()
{
    static const std::string none;
    const auto& bindings = learnState().bindings;
    return bindings.empty() ? none : bindings.back().second;
}
// The item just drawn takes a modulator dropped on it, for its scope's knob.
void modulationTarget()
{
    auto* host = hostSlot();
    const auto& binding = scopedBinding();
    if (host == nullptr || binding.empty() || ImGui::GetDragDropPayload() == nullptr)
        return;
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(modulatorPayload);
            payload != nullptr && payload->DataSize == static_cast<int>(sizeof(std::uint64_t))) {
            std::uint64_t source {};
            std::memcpy(&source, payload->Data, sizeof(source));
            host->dropped(binding, source);
        }
        ImGui::EndDragDropTarget();
    }
}
} // namespace

ModulationHostScope::ModulationHostScope(ModulationHost* host) : previous_(hostSlot())
{
    hostSlot() = host;
}

ModulationHostScope::~ModulationHostScope()
{
    hostSlot() = previous_;
}

ModulationHost* modulationHost()
{
    return hostSlot();
}

const std::string& scopedBinding()
{
    static const std::string none;
    const auto& bindings = learnState().bindings;
    return bindings.empty() ? none : bindings.back().first;
}

LearnScope::LearnScope(std::string key, std::string label)
{
    learnState().scopes.emplace_back(std::move(key), std::move(label));
    learnState().bindings.emplace_back();
}

LearnScope::~LearnScope()
{
    learnState().scopes.pop_back();
    learnState().bindings.pop_back();
}

LearnTrackScope::LearnTrackScope(std::string prefix, std::string trackName)
{
    learnState().tracks.emplace_back(std::move(prefix), std::move(trackName));
}

LearnTrackScope::~LearnTrackScope()
{
    learnState().tracks.pop_back();
}

LearnParameterScope::LearnParameterScope(const juce::String& binding, const juce::String& label, const std::string& prefix,
                                         const juce::String& trackName)
{
    auto& state = learnState();
    std::string key;
    std::string name;
    if (state.mode) {
        const auto* track = state.tracks.empty() ? nullptr : &state.tracks.back();
        const auto& base = prefix.empty() && track != nullptr ? track->first : prefix;
        if (!base.empty() && binding.isNotEmpty()) {
            key = base + binding.toStdString();
            const auto owner = trackName.isNotEmpty() ? trackName.toStdString() : track != nullptr ? track->second : std::string();
            name = (owner.empty() ? std::string() : owner + " \xc2\xb7 ") + label.toStdString();
        }
    }
    state.scopes.emplace_back(std::move(key), std::move(name));
    state.bindings.emplace_back(binding.toStdString(), label.toStdString());
}

LearnParameterScope::~LearnParameterScope()
{
    learnState().scopes.pop_back();
    learnState().bindings.pop_back();
}

void setSpotRecording(const bool on)
{
    spotRecording().on = on;
    if (!on)
        spotRecording().spots.clear();
}

void recordSpot(const std::string& key)
{
    auto& recording = spotRecording();
    if (!recording.on || key.empty())
        return;
    const auto origin = ImGui::GetMainViewport()->Pos;
    const auto min = ImGui::GetItemRectMin();
    const auto max = ImGui::GetItemRectMax();
    recording.spots[key] = { key, {}, ImVec2(min.x - origin.x, min.y - origin.y), ImVec2(max.x - origin.x, max.y - origin.y), 0 };
}

std::vector<LearnSpot> recordedSpots()
{
    std::vector<LearnSpot> out;
    for (const auto& [key, spot] : spotRecording().spots)
        out.push_back(spot);
    return out;
}

void learnItem()
{
    modulationTarget();
    if (spotRecording().on && !scopedBinding().empty())
        recordSpot("knob:" + scopedBinding());
    auto& state = learnState();
    if (state.mode && !state.scopes.empty())
        learnItem(state.scopes.back().first, state.scopes.back().second);
}

void learnItem(const std::string& key, const std::string& label)
{
    auto& state = learnState();
    if (!state.mode || key.empty())
        return;
    const auto* viewport = ImGui::GetWindowViewport();
    state.spots.push_back({ key, label, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), viewport != nullptr ? viewport->ID : 0 });
}

void setLearnMode(const bool on)
{
    learnState().mode = on;
}

bool learnMode()
{
    return learnState().mode;
}

std::vector<LearnSpot> takeLearnSpots()
{
    auto spots = std::move(learnState().spots);
    learnState().spots.clear();
    return spots;
}

bool knob(const char* id, float* value, float minimum, float maximum, float defaultValue, const KnobStyle& style)
{
    auto* host = modulationHost();
    const auto binding = scopedBinding();
    std::vector<KnobModulation> modulations;
    const auto armed = host && !binding.empty() ? host->armed() : 0;
    if (host && !binding.empty()) host->modulationsOf(binding, modulations);
    const auto origin = ImGui::GetCursorScreenPos();
    auto shared = cyui::KnobStyle{};
    shared.diameter = style.diameter; shared.colour = style.colour; shared.label = style.label;
    shared.format = style.format; shared.unit = style.unit; shared.flags = style.flags;
    shared.modulation = style.modulation; shared.steps = style.steps;
    // A modulation amount is edited through the shared control; the graph's
    // base parameter is untouched. CyDAW owns the destination and its rings.
    auto amount = 0.0f;
    for (const auto& modulation : modulations) if (modulation.source == armed) amount = modulation.amount;
    auto edited = armed ? amount : *value;
    if (armed) { shared.flags = cyui::knobBipolar; shared.steps = 0; shared.unit = nullptr; shared.format = "%.3f"; }
    ImGui::PushID(id);
    const auto knobId = ImGui::GetID("##knob");
    const auto anchor = ImGui::GetID("##anchor");
    ImGui::PopID();
    auto& io = ImGui::GetIO();
    const auto realDelta = io.MouseDelta;
    if (ImGui::GetActiveID() == knobId) io.MouseDelta = pointerMovement(anchor, false);
    const auto changed = cyui::knob(id, &edited, armed ? -1.f : minimum, armed ? 1.f : maximum,
                                    armed ? 0.f : defaultValue, shared);
    io.MouseDelta = realDelta;
    if (ImGui::IsItemActivated()) (void)pointerMovement(anchor, true);
    learnItem();
    if (changed) {
        if (armed) host->setAmount(binding, armed, std::abs(edited) < 1.e-4f ? 0.f : edited);
        else *value = edited;
    }
    const auto& p = theme::palette();
    const auto diameter = dp(style.diameter);
    const auto width = dp(style.diameter + 12.f);
    const ImVec2 centre(origin.x + width * .5f, origin.y + diameter * .5f);
    const auto thickness = std::max(dp(2.f), diameter * .095f);
    auto radius = diameter * .5f - dp(2.f) + thickness * .95f;
    const auto t = toNormalised(*value, minimum, maximum, (style.flags & knobLogarithmic) != 0);
    for (const auto& modulation : modulations) {
        const auto reach = std::clamp(t + modulation.amount, 0.f, 1.f);
        const auto colour = modulation.colour ? modulation.colour : p.modulation;
        arc(ImGui::GetWindowDrawList(), centre, radius,
            kArcStart + kArcSweep * std::min(t, reach), kArcStart + kArcSweep * std::max(t, reach),
            armed == modulation.source ? theme::brighter(colour, .2f) : colour, thickness * .5f);
        radius += thickness * .6f;
    }
    if (ImGui::IsItemHovered() && !modulations.empty() && ImGui::GetDragDropPayload() == nullptr) {
        ImGui::BeginTooltip();
        for (const auto& modulation : modulations)
            ImGui::Text("%s → %s [%.3f]", modulation.name.c_str(), scopedLabel().c_str(), modulation.amount);
        ImGui::EndTooltip();
    }
    if (style.valueText && ImGui::IsItemHovered()) cyui::tooltip(style.valueText);
    return changed && !armed;
}
float meterPosition(float db) { return cyui::meterPosition(db); }
bool fader(const char* id, float* value, float low, float high, ImVec2 size, ImU32 colour, bool ticks)
{
    (void)ticks; // The common fader includes its scale.
    const auto changed = cyui::fader(id, value, low, high, size, colour);
    learnItem();
    return changed;
}
void meter(const char* id, ImVec2 size, float peakL, float peakR, float rmsL, float rmsR, bool vertical, bool scale)
{
    // The optional dB label column is a host overlay; bars/hold/RMS come from CyUI.
    const auto origin = ImGui::GetCursorScreenPos();
    const auto gutter = scale && vertical ? std::min(size.x * .5f, dp(18.f)) : 0.f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x + gutter, origin.y));
    cyui::meter(id, ImVec2(size.x - gutter, size.y), peakL, peakR, rmsL, rmsR, vertical);
    if (gutter > 0.f) {
        cyui::ScopedFont font(nullptr, .56f);
        for (float db : {0.f, -12.f, -24.f, -48.f}) {
            const auto text = std::to_string(int(db));
            const auto y = origin.y + size.y * (1.f - cyui::meterPosition(db));
            ImGui::GetWindowDrawList()->AddText(ImVec2(origin.x, y), cyui::palette().textDim, text.c_str());
        }
    }
}


bool pictureToggle(const char* id, bool* value, const ImU32 onColour, const Picture picture, const char* tip, float width,
                   float height)
{
    const auto& p = theme::palette();
    if (height <= 0.0f)
        height = ImGui::GetFrameHeight();
    if (width <= 0.0f)
        width = std::round(height * 1.5f);
    const auto origin = ImGui::GetCursorScreenPos();
    const auto clicked = ImGui::InvisibleButton(id, ImVec2(width, height));
    learnItem();
    if (clicked)
        *value = !*value;
    const auto hovered = ImGui::IsItemHovered();
    auto* list = ImGui::GetWindowDrawList();
    const auto on = *value;
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), on ? onColour : (hovered ? p.bg4 : p.bg3),
                        ImGui::GetStyle().FrameRounding);
    // Its drawing inside, clear of the edges.
    const auto insetY = std::round(height * 0.22f);
    const auto insetX = std::round(std::max(insetY, (width - (height - insetY * 2.0f) * 1.6f) * 0.5f));
    picture(list, ImVec2(origin.x + insetX, origin.y + insetY), ImVec2(origin.x + width - insetX, origin.y + height - insetY),
            on ? p.bg0 : (hovered ? p.text : p.textDim));
    if (tip != nullptr)
        tooltip(tip);
    return clicked;
}

namespace picture {

// A horseshoe magnet tilted as the usual sign, its two poles apart.
void magnet(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    // Drawn in its own units (a U opening upwards, radius 1, legs to
    // -1.45), turned 45 degrees and fitted in the box.
    const auto size = std::min(max.x - min.x, max.y - min.y);
    const auto scale = size / 2.9f;
    const auto thickness = std::max(1.5f, scale * 0.95f);
    const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const auto place = [&](const float x, const float y) {
        // Its middle (0, -0.22) on the box's, turned 45 degrees.
        constexpr auto turn = 0.70710678f;
        const auto dx = x;
        const auto dy = y + 0.22f;
        return ImVec2(centre.x + (dx * turn - dy * turn) * scale, centre.y + (dx * turn + dy * turn) * scale);
    };
    constexpr auto legTop = -1.45f;
    constexpr auto poleEnd = -0.55f;
    list->PathLineTo(place(1.0f, poleEnd));
    for (auto step = 0; step <= 12; ++step) {
        const auto angle = IM_PI * static_cast<float>(step) / 12.0f;
        list->PathLineTo(place(std::cos(angle), std::sin(angle) * 1.0f));
    }
    list->PathLineTo(place(-1.0f, poleEnd));
    list->PathStroke(colour, thickness);
    // The poles, apart from the body.
    for (const auto side : { -1.0f, 1.0f })
        list->AddLine(place(side, legTop), place(side, poleEnd - 0.45f), colour, thickness);
}

// Two arrows turning round: the font's loop glyph.
void loop(ImDrawList* list, const ImVec2 min, const ImVec2 max, const ImU32 colour)
{
    const auto size = ImGui::GetFontSize();
    const auto extent = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, icon::loop);
    list->AddText(ImGui::GetFont(), size, ImVec2((min.x + max.x - extent.x) * 0.5f, (min.y + max.y - extent.y) * 0.5f), colour, icon::loop);
}

} // namespace picture

bool snapToggle(const char* id, bool* on)
{
    ImGui::PushID(id);
    const auto changed = pictureToggle("##snap", on, theme::palette().accent, picture::magnet,
                                       L("Aimant : les points, les sons et les bornes se calent sur la grille. Alt pendant un geste : l'inverse.",
                                         "Snap: points, sounds and bounds land on the grid. Alt during a gesture: the other way."));
    ImGui::PopID();
    return changed;
}

bool loopToggle(const char* id, bool* on, const char* tip)
{
    ImGui::PushID(id);
    const auto changed = pictureToggle("##loop", on, theme::palette().accent, picture::loop, tip);
    ImGui::PopID();
    return changed;
}

bool snapping(const bool on)
{
    return on != ImGui::GetIO().KeyAlt;
}

bool toggle(const char* label, bool* value, ImU32 colour, ImVec2 size, const char* tip)
{
    const auto result = cyui::toggleButton(label, value, colour, size, tip);
    learnItem(); return result;
}
bool iconButton(const char* icon, const char* tip, bool active, ImU32 colour, ImVec2 size)
{
    const auto result = cyui::iconButton(icon, tip, active, colour, size);
    learnItem(); return result;
}
bool primaryButton(const char* label, ImVec2 size) { return cyui::primaryButton(label, size); }
void toolbarSeparator() { cyui::toolbarSeparator(); }


void pushTextWrapAt(const float screenX)
{
    ImGui::PushTextWrapPos(std::max(1.0f, screenX - ImGui::GetWindowPos().x + ImGui::GetScrollX()));
}

void tooltip(const char* text) { cyui::tooltip(text); }

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
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (active && !ImGui::IsItemActivated()) {
        cyui::CanvasView view {{position, 0}, pixelsPerUnit};
        const ImVec2 previous(io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y);
        const auto zoom = std::clamp(pixelsPerUnit * std::exp(double(io.MouseDelta.y) * .012), minimumZoom, maximumZoom);
        view.zoomAt(previous, zoom, min);
        view.origin.x -= double(io.MouseDelta.x) / view.zoom;
        position = std::max(minimumPosition, view.origin.x);
        pixelsPerUnit = view.zoom;
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
              const float maximumZoom, const bool rowsAtRight)
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
            zoom = std::clamp(zoom * std::exp((rowsAtRight ? io.MouseDelta.x : -io.MouseDelta.x) * 0.012f), minimumZoom, maximumZoom);
        scroll = std::max(0.0f, anchor * zoom - (io.MousePos.y - min.y));
    }
    const auto fit = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    list->AddRectFilled(min, max, active ? p.bg3 : (hovered ? theme::mix(p.bg2, p.bg3, 0.5f) : p.bg2));
    // Its marks, as the time band's (turned): the rows' spacing, so the zoom
    // shows, longer every fourth; they move with the rows.
    const auto spacing = std::max(dp(6.0f), dp(12.0f) * zoom / std::max(1.0e-3f, (minimumZoom + maximumZoom) * 0.25f));
    auto index = static_cast<long long>(std::floor(scroll / spacing));
    // The marks start on the rows' side.
    const auto inner = rowsAtRight ? max.x - dp(1.0f) : min.x + dp(1.0f);
    const auto outer = rowsAtRight ? min.x + dp(3.0f) : max.x - dp(3.0f);
    for (auto y = min.y - std::fmod(scroll, spacing); y < max.y; y += spacing, ++index) {
        if (y <= min.y)
            continue;
        const auto strong = index % 4 == 0;
        list->AddLine(ImVec2(inner, y), ImVec2(strong ? outer : (min.x + max.x) * 0.5f, y), theme::alpha(p.textFaint, strong ? 0.55f : 0.25f));
    }
    const auto edge = rowsAtRight ? max.x - 0.5f : min.x + 0.5f;
    list->AddLine(ImVec2(edge, min.y), ImVec2(edge, max.y), p.line);
    if (hovered && !active)
        tooltip(rowsAtRight ? L("Glisser : vers le haut et le bas pour se déplacer, vers la droite pour agrandir, vers la gauche pour "
                                "réduire. Double-clic : taille normale.",
                                "Drag: up and down to move, right to make bigger, left to make smaller. Double click: normal size.")
                            : L("Glisser : vers le haut et le bas pour se déplacer, vers la gauche pour agrandir, vers la droite pour "
                                "réduire. Double-clic : taille normale.",
                                "Drag: up and down to move, left to make bigger, right to make smaller. Double click: normal size."));
    return fit;
}

bool bandCorner(const char* id, const ImVec2 min, const ImVec2 max)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(min);
    const auto clicked = ImGui::InvisibleButton(id, ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
    const auto hovered = ImGui::IsItemHovered();
    list->AddRectFilled(min, max, hovered ? p.bg3 : p.bg2);
    ImGui::PushFont(nullptr, std::min(ImGui::GetStyle().FontSizeBase * 0.72f, (max.y - min.y) * 0.9f / ImGui::GetStyle().FontScaleDpi));
    centredText(list, min, max, hovered ? p.accent : p.textDim, icon::zoomIn);
    ImGui::PopFont();
    tooltip(L("Les bandes à saisir : celle du haut pour le temps, celle de droite pour la hauteur. Clic : tout voir.",
              "The bands to grab: the top one for time, the right one for height. Click: see everything."));
    return clicked;
}

bool dragNumber(const char* id, float* value, float speed, float low, float high, const char* format, float width)
{ return cyui::dragValue(id, value, speed, low, high, format, width); }
void sectionTitle(const char* text) { cyui::sectionTitle(text); }
bool colourChip(const char* id, ImU32 colour, ImVec2 size) { return cyui::colourChip(id, colour, size); }
void centredText(ImDrawList* list, ImVec2 min, ImVec2 max, ImU32 colour, const char* text)
{ cyui::centredText(list, min, max, colour, text); }


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

void emptyState(const char* title, const char* message) { cyui::emptyState(title, message); }
void clippedText(ImDrawList* list, ImVec2 position, float width, ImU32 colour, const char* text)
{ cyui::clippedText(list, position, width, colour, text); }

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
