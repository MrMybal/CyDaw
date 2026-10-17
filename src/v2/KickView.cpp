#include "KickView.h"

#include "InstrumentEffects.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
namespace kick = engine::kick;

namespace {

constexpr ImU32 kickRed = IM_COL32(0xff, 0x5a, 0x5a, 0xff);
constexpr ImU32 pitchColour = IM_COL32(0xff, 0xc8, 0x57, 0xff);
constexpr ImU32 envelopeColour = IM_COL32(0x5f, 0xc9, 0xf7, 0xff);
// What the picture draws frequencies between (log), bottom to top.
constexpr double lowestHz = 20.0;
constexpr double highestHz = 10'000.0;
constexpr double pictureRate = 24'000.0;

juce::String parameterName(const int index)
{
    const auto& p = kick::parameter(index);
    return juce::String::fromUTF8((ui::french() ? p.french : p.english).c_str());
}

bool logarithmic(const kick::Parameter& p)
{
    return p.logarithmic && p.minimum > 0.0f;
}

float normalise(const int index, const float value)
{
    const auto& p = kick::parameter(index);
    if (logarithmic(p))
        return static_cast<float>(std::log(std::max(value, p.minimum) / p.minimum) / std::log(p.maximum / p.minimum));
    return (value - p.minimum) / std::max(1.0e-6f, p.maximum - p.minimum);
}

float denormalise(const int index, const float normalised)
{
    const auto& p = kick::parameter(index);
    const auto x = std::clamp(normalised, 0.0f, 1.0f);
    if (logarithmic(p))
        return p.minimum * std::pow(p.maximum / p.minimum, x);
    return p.minimum + x * (p.maximum - p.minimum);
}

// "G#1": the note nearest a frequency (C4 the middle C).
juce::String noteName(const double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto midi = static_cast<int>(std::lround(69.0 + 12.0 * std::log2(std::max(1.0, hz) / 440.0)));
    const auto cents = static_cast<int>(std::lround((69.0 + 12.0 * std::log2(std::max(1.0, hz) / 440.0) - midi) * 100.0));
    auto text = juce::String(names[((midi % 12) + 12) % 12]) + juce::String(midi / 12 - 1);
    if (cents != 0)
        text << (cents > 0 ? " +" : " ") << cents << " ct";
    return text;
}

// The time the picture spans: the kick and a little after it.
double spanOf(const kick::KickValues& values)
{
    return std::clamp(kick::lengthSeconds(values) * 1.12 + 0.02, 0.08, 6.0);
}

} // namespace

KickView::KickView(DeviceHost& studio, UiState& state) : studio_(studio), state_(state), presets_(studio, state) {}

float KickView::paneWidth()
{
    return dp(360.0f);
}

bool KickView::bind(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId)
{
    const auto values = graph.editorKickValues(nodeId);
    if (!values)
        return false;
    card_.track = track;
    card_.graph = &graph;
    card_.nodeId = nodeId;
    card_.values = *values;
    return true;
}

float KickView::value(const int index) const
{
    return card_.values[static_cast<std::size_t>(std::clamp(index, 0, kick::parameterCount - 1))];
}

void KickView::set(const int index, const float newValue)
{
    if (card_.graph == nullptr)
        return;
    const auto clamped = kick::clampValue(index, newValue);
    if (clamped == value(index))
        return;
    card_.values[static_cast<std::size_t>(index)] = clamped;
    const auto binding = "node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".kick."
                         + juce::String(kick::parameter(index).key);
    (void) card_.graph->editorSetParameter(binding, clamped);
    state_.focused = Panel::devices;
}

bool KickView::knob(const int index, const char* label, const float diameter)
{
    const auto& p = theme::palette();
    const auto& parameter = kick::parameter(index);
    ImGui::PushID(index);
    ImGui::BeginGroup();
    auto normalised = normalise(index, value(index));
    const auto defaultNormalised = normalise(index, parameter.defaultValue);
    ui::KnobStyle style;
    style.diameter = diameter;
    style.flags = ui::knobNoLabel;
    style.colour = kickRed;
    const auto origin = ImGui::GetCursorScreenPos();
    bool changed {};
    {
        const ui::LearnParameterScope learn("node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".kick."
                                                + juce::String(parameter.key),
                                            juce::String::fromUTF8(label), studio_.learnPrefix(card_.track), studio_.trackName(card_.track));
        changed = ui::knob("##knob", &normalised, 0.0f, 1.0f, defaultNormalised, style);
    }
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    if (changed)
        set(index, denormalise(index, normalised));
    // Right click: its default, a lane in the arrangement.
    if (ImGui::BeginPopupContextItem("##knobMenu")) {
        ImGui::TextDisabled("%s", parameterName(index).toRawUTF8());
        if (ImGui::MenuItem(L("Valeur par défaut", "Default value")))
            set(index, parameter.defaultValue);
        const auto binding = "node." + juce::String(static_cast<juce::int64>(card_.nodeId)) + ".kick." + juce::String(parameter.key);
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
        ImGui::SetTooltip("%s : %s\n%s", parameterName(index).toRawUTF8(),
                          kick::displayValue(index, value(index), ui::french()).c_str(),
                          L("Double-clic : valeur de départ · clic droit : automatiser",
                            "Double click: default · right click: automate"));
    const auto width = dp(diameter + 14.0f);
    // Its name, its value while touched.
    const auto text = hovered || active ? juce::String::fromUTF8(kick::displayValue(index, value(index), ui::french()).c_str())
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

void KickView::choice(const int index, const float width)
{
    const auto& parameter = kick::parameter(index);
    ImGui::PushID(index);
    const auto current = std::clamp(static_cast<int>(std::lround(value(index))), 0, static_cast<int>(parameter.choices.size()) - 1);
    ImGui::SetNextItemWidth(width);
    const auto shown = kick::displayValue(index, static_cast<float>(current), ui::french());
    if (ImGui::BeginCombo("##choice", shown.c_str())) {
        for (int at = 0; at < static_cast<int>(parameter.choices.size()); ++at) {
            const auto name = kick::displayValue(index, static_cast<float>(at), ui::french());
            if (ImGui::Selectable(name.c_str(), at == current))
                set(index, static_cast<float>(at));
        }
        ImGui::EndCombo();
    }
    ui::tooltip(parameterName(index).toRawUTF8());
    ImGui::PopID();
}

void KickView::toggle(const int index, const char* label)
{
    ImGui::PushID(index);
    auto on = value(index) >= 0.5f;
    if (ui::toggle(label, &on, kickRed, ImVec2(0.0f, 0.0f), parameterName(index).toRawUTF8()))
        set(index, on ? 1.0f : 0.0f);
    ImGui::PopID();
}

const std::vector<float>& KickView::rendered()
{
    ++frame_;
    for (auto& picture : pictures_)
        if (picture.values == card_.values) {
            picture.used = frame_;
            return picture.samples;
        }
    // Made again only when a setting moved; the one used longest ago goes.
    if (pictures_.size() < 4)
        pictures_.emplace_back();
    auto& picture = *std::min_element(pictures_.begin(), pictures_.end(),
                                      [](const Picture& a, const Picture& b) { return a.used < b.used; });
    picture.values = card_.values;
    picture.samples = kick::renderKick(card_.values, pictureRate, kick::referenceNote, 1.0f, 6.0);
    picture.used = frame_;
    return picture.samples;
}

// ---- The picture ------------------------------------------------------------------------------

void KickView::drawDisplay(const ImVec2 origin, const ImVec2 size, const bool interactive)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const ImVec2 end(origin.x + size.x, origin.y + size.y);
    list->AddRectFilled(origin, end, p.bg0, dp(4.0f));
    list->AddRect(origin, end, p.line, dp(4.0f));
    const auto& values = card_.values;
    const auto span = dragging_ >= 0 ? dragSpan_ : spanOf(values);
    const auto left = origin.x + dp(6.0f);
    const auto right = end.x - dp(6.0f);
    const auto top = origin.y + dp(6.0f);
    const auto bottom = end.y - dp(interactive ? 16.0f : 6.0f);
    const auto width = std::max(1.0f, right - left);
    const auto height = std::max(1.0f, bottom - top);
    const auto xOf = [&](const double seconds) { return left + static_cast<float>(seconds / span) * width; };
    const auto timeOf = [&](const float x) { return std::clamp(static_cast<double>((x - left) / width) * span, 0.0, span); };
    const auto yOfHz = [&](const double hz) {
        const auto t = std::log(std::clamp(hz, lowestHz, highestHz) / lowestHz) / std::log(highestHz / lowestHz);
        return bottom - static_cast<float>(t) * height;
    };
    const auto hzOf = [&](const float y) {
        const auto t = std::clamp(static_cast<double>((bottom - y) / height), 0.0, 1.0);
        return lowestHz * std::pow(highestHz / lowestHz, t);
    };
    const auto yOfAmp = [&](const double amplitude) { return bottom - static_cast<float>(std::clamp(amplitude, 0.0, 1.0)) * height; };
    const auto compact = !interactive;
    list->PushClipRect(origin, end, true);
    // The grid: time and frequency.
    if (!compact) {
        const double steps[] = { 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1.0 };
        auto step = 1.0;
        for (const auto candidate : steps)
            if (span / candidate <= 12.0) {
                step = candidate;
                break;
            }
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
        for (auto t = step; t < span; t += step) {
            const auto x = xOf(t);
            list->AddLine(ImVec2(x, top), ImVec2(x, bottom), p.grid);
            char text[24];
            if (t < 1.0)
                std::snprintf(text, sizeof(text), "%.0f ms", t * 1'000.0);
            else
                std::snprintf(text, sizeof(text), "%.2f s", t);
            list->AddText(ImVec2(x + dp(2.0f), bottom + dp(1.0f)), p.textFaint, text);
        }
        for (const auto hz : { 50.0, 100.0, 200.0, 500.0, 1'000.0, 2'000.0, 5'000.0 }) {
            const auto y = yOfHz(hz);
            list->AddLine(ImVec2(left, y), ImVec2(right, y), p.grid);
            char text[24];
            std::snprintf(text, sizeof(text), hz >= 1'000.0 ? "%.0fk" : "%.0f", hz >= 1'000.0 ? hz / 1'000.0 : hz);
            list->AddText(ImVec2(left + dp(2.0f), y - ImGui::GetFontSize()), p.textDim, text);
        }
        ImGui::PopFont();
    }
    // The wave, as the voice plays it.
    const auto& samples = rendered();
    const auto middle = (top + bottom) * 0.5f;
    const auto half = height * 0.46f;
    auto peak = 0.0f;
    for (const auto x : samples)
        peak = std::max(peak, std::abs(x));
    const auto columns = static_cast<int>(width);
    const auto waveColour = (kickRed & 0x00ffffffu) | 0x90000000u;
    for (int column = 0; column < columns; ++column) {
        const auto from = static_cast<std::size_t>(static_cast<double>(column) / width * span * pictureRate);
        const auto to = static_cast<std::size_t>(static_cast<double>(column + 1) / width * span * pictureRate);
        if (from >= samples.size())
            break;
        auto low = 0.0f;
        auto high = 0.0f;
        for (auto at = from; at < std::max(from + 1, std::min(to, samples.size())); ++at) {
            low = std::min(low, samples[at]);
            high = std::max(high, samples[at]);
        }
        const auto x = left + static_cast<float>(column) + 0.5f;
        list->AddLine(ImVec2(x, middle - std::clamp(high, -1.1f, 1.1f) * half),
                      ImVec2(x, middle - std::clamp(low, -1.1f, 1.1f) * half + 1.0f), waveColour);
    }
    // Full scale: a line where it would clip.
    if (!compact) {
        const auto clipColour = peak > 1.0f ? p.record : p.gridStrong;
        list->AddLine(ImVec2(left, middle - half), ImVec2(right, middle - half), clipColour);
        list->AddLine(ImVec2(left, middle + half), ImVec2(right, middle + half), clipColour);
    }
    // The envelope and the pitch over it.
    std::vector<ImVec2> pitch;
    std::vector<ImVec2> envelope;
    pitch.reserve(static_cast<std::size_t>(columns) + 1);
    envelope.reserve(static_cast<std::size_t>(columns) + 1);
    for (int column = 0; column <= columns; ++column) {
        const auto t = static_cast<double>(column) / width * span;
        pitch.emplace_back(left + static_cast<float>(column), yOfHz(kick::pitchAt(values, t)));
        envelope.emplace_back(left + static_cast<float>(column), yOfAmp(kick::amplitudeAt(values, t)));
    }
    list->AddPolyline(envelope.data(), static_cast<int>(envelope.size()), envelopeColour, dp(compact ? 1.2f : 1.6f));
    list->AddPolyline(pitch.data(), static_cast<int>(pitch.size()), pitchColour, dp(compact ? 1.4f : 2.0f));
    // The length, where it cuts.
    const auto lengthSeconds = static_cast<double>(value(kick::length)) * 0.001;
    if (lengthSeconds < span)
        list->AddLine(ImVec2(xOf(lengthSeconds), top), ImVec2(xOf(lengthSeconds), bottom), p.textDim, dp(1.0f));
    // What it ends on, how long, how loud.
    {
        const auto endHz = static_cast<double>(value(kick::pitchEnd));
        char text[160];
        std::snprintf(text, sizeof(text), "%s %.0f Hz \xc2\xb7 %s   %s %.0f ms   %s %+.1f dB", L("Fin", "End"), endHz,
                      noteName(endHz).toRawUTF8(), L("Durée", "Length"), kick::lengthSeconds(values) * 1'000.0,
                      L("Crête", "Peak"), 20.0 * std::log10(std::max(1.0e-6f, peak)));
        ImGui::PushFont(nullptr, ui::currentFontBase() * (compact ? 0.72f : 0.85f));
        const auto textSize = ImGui::CalcTextSize(text);
        list->AddText(ImVec2(right - textSize.x - dp(2.0f), top), p.textDim, text);
        ImGui::PopFont();
    }
    if (!interactive) {
        list->PopClipRect();
        return;
    }
    // The handles: the pitch's start, its end (when it lands and where), the
    // middle of its fall (its curve); the end of the hold, of the decay, the
    // middle of the decay (its curve); the length.
    const auto attackS = static_cast<double>(value(kick::attack)) * 0.001;
    const auto holdS = static_cast<double>(value(kick::hold)) * 0.001;
    const auto decayS = static_cast<double>(value(kick::decay)) * 0.001;
    const auto pitchS = static_cast<double>(value(kick::pitchTime)) * 0.001;
    struct Handle {
        ImVec2 at;
        ImU32 colour;
        const char* tip;
    };
    const Handle handles[] = {
        { ImVec2(xOf(0.0) + dp(4.0f), yOfHz(value(kick::pitchStart))), pitchColour,
          L("Hauteur de départ (haut, bas)", "Start pitch (up, down)") },
        { ImVec2(xOf(pitchS), yOfHz(value(kick::pitchEnd))), pitchColour,
          L("Hauteur de fin (haut, bas) et temps de chute (gauche, droite)", "End pitch (up, down) and pitch time (left, right)") },
        { ImVec2(xOf(pitchS * 0.5), yOfHz(kick::pitchAt(values, pitchS * 0.5))), pitchColour,
          L("Courbe de la chute", "Pitch curve") },
        { ImVec2(xOf(attackS + holdS), yOfAmp(1.0) + dp(4.0f)), envelopeColour, L("Maintien", "Hold") },
        { ImVec2(xOf(attackS + holdS + decayS), yOfAmp(0.0) - dp(4.0f)), envelopeColour, L("Déclin", "Decay") },
        { ImVec2(xOf(attackS + holdS + decayS * 0.5), yOfAmp(kick::amplitudeAt(values, attackS + holdS + decayS * 0.5))),
          envelopeColour, L("Courbe du déclin", "Decay curve") },
        { ImVec2(xOf(std::min(lengthSeconds, span)), middle), p.text, L("Longueur (coupe le son)", "Length (cuts the sound)") },
    };
    constexpr int handleCount = static_cast<int>(sizeof(handles) / sizeof(handles[0]));
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##kickDisplay", size);
    const auto mouse = ImGui::GetIO().MousePos;
    auto nearest = -1;
    auto nearestDistance = dp(12.0f);
    for (int index = 0; index < handleCount; ++index) {
        const auto dx = mouse.x - handles[index].at.x;
        const auto dy = mouse.y - handles[index].at.y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = index;
        }
    }
    if (ImGui::IsItemActivated() && nearest >= 0) {
        dragging_ = nearest;
        dragSpan_ = span;
    }
    if (!ImGui::IsItemActive())
        dragging_ = -1;
    if (dragging_ >= 0) {
        const auto t = timeOf(mouse.x);
        switch (dragging_) {
        case 0: set(kick::pitchStart, static_cast<float>(hzOf(mouse.y))); break;
        case 1:
            set(kick::pitchEnd, static_cast<float>(hzOf(mouse.y)));
            set(kick::pitchTime, static_cast<float>(t * 1'000.0));
            break;
        case 2: {
            // The curve that passes where the pointer is, half way down the fall.
            const auto start = static_cast<double>(value(kick::pitchStart));
            const auto finish = static_cast<double>(value(kick::pitchEnd));
            const auto ratio = std::clamp((hzOf(mouse.y) - finish) / std::max(1.0e-3, start - finish), 1.0e-5, 0.99);
            set(kick::pitchCurve, static_cast<float>(std::log(ratio) / std::log(0.5)));
            break;
        }
        case 3: set(kick::hold, static_cast<float>((t - attackS) * 1'000.0)); break;
        case 4: set(kick::decay, static_cast<float>((t - attackS - holdS) * 1'000.0)); break;
        case 5: {
            const auto amplitude = std::clamp(static_cast<double>((bottom - mouse.y) / height), 1.0e-4, 0.99);
            set(kick::decayCurve, static_cast<float>(std::log(amplitude) / std::log(0.5)));
            break;
        }
        case 6: set(kick::length, static_cast<float>(t * 1'000.0)); break;
        default: break;
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    for (int index = 0; index < handleCount; ++index) {
        const auto lit = index == dragging_ || (dragging_ < 0 && index == nearest && ImGui::IsItemHovered());
        const auto radius = dp(lit ? 6.0f : 4.5f);
        list->AddCircleFilled(handles[index].at, radius, lit ? handles[index].colour : p.bg2);
        list->AddCircle(handles[index].at, radius, handles[index].colour, 0, dp(1.6f));
    }
    if (dragging_ < 0 && nearest >= 0 && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", handles[nearest].tip);
    else if (dragging_ >= 0)
        ImGui::SetTooltip("%s", handles[dragging_].tip);
    list->PopClipRect();
}

// ---- The sections -----------------------------------------------------------------------------

void KickView::section(const char* id, const char* title, const float width, const float height, void (KickView::*body)())
{
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextColored(theme::vec(kickRed), "%s", title);
    ImGui::PopFont();
    (this->*body)();
    ImGui::EndChild();
}

void KickView::drawBody()
{
    choice(kick::bodyShape, dp(96.0f));
    knob(kick::pitchStart, L("DÉPART", "START"));
    ImGui::SameLine();
    knob(kick::pitchEnd, L("FIN", "END"));
    ImGui::SameLine();
    knob(kick::pitchTime, L("CHUTE", "TIME"));
    ImGui::SameLine();
    knob(kick::pitchCurve, L("COURBE", "CURVE"));
    ImGui::SameLine();
    knob(kick::bodyLevel, L("NIVEAU", "LEVEL"));
    ImGui::SameLine();
    knob(kick::bodyPhase, "PHASE");
}

void KickView::drawAmplitude()
{
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    knob(kick::attack, L("ATTAQUE", "ATTACK"));
    ImGui::SameLine();
    knob(kick::hold, L("MAINTIEN", "HOLD"));
    ImGui::SameLine();
    knob(kick::decay, L("DÉCLIN", "DECAY"));
    ImGui::SameLine();
    knob(kick::decayCurve, L("COURBE", "CURVE"));
    ImGui::SameLine();
    knob(kick::length, L("LONGUEUR", "LENGTH"));
}

void KickView::drawClick()
{
    choice(kick::clickType, dp(96.0f));
    knob(kick::clickLevel, L("NIVEAU", "LEVEL"));
    ImGui::SameLine();
    knob(kick::clickDecay, L("DÉCLIN", "DECAY"));
    ImGui::SameLine();
    knob(kick::clickTone, L("TON", "TONE"));
}

void KickView::drawSub()
{
    choice(kick::subOctave, dp(96.0f));
    knob(kick::subLevel, L("NIVEAU", "LEVEL"));
    ImGui::SameLine();
    knob(kick::subDecay, L("DÉCLIN", "DECAY"));
}

void KickView::drawDrive()
{
    choice(kick::driveType, dp(96.0f));
    knob(kick::drive, "DRIVE");
    ImGui::SameLine();
    knob(kick::driveTone, L("TON", "TONE"));
}

void KickView::drawTone()
{
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeight()));
    knob(kick::lowCut, L("COUPE-BAS", "LOW CUT"));
    ImGui::SameLine();
    knob(kick::lowBoost, L("GRAVES", "BOOST"));
    ImGui::SameLine();
    knob(kick::highCut, L("COUPE-HAUT", "HIGH CUT"));
}

void KickView::drawGlobal()
{
    toggle(kick::keyTrack, L("Suit la note", "Key tracking"));
    ui::tooltip(L("Allumé : la note joue le kick plus haut ou plus bas (C2 le joue tel qu'il est réglé). Éteint : toutes les "
                  "notes le jouent pareil.",
                  "On: the note plays the kick higher or lower (C2 plays it as set). Off: every note plays it the same."));
    knob(kick::velocity, L("VÉLOCITÉ", "VELOCITY"));
    ImGui::SameLine();
    knob(kick::output, L("SORTIE", "OUTPUT"));
}

// ---- The card's pane, the window --------------------------------------------------------------

void KickView::drawPane(const TrackRef& track, app::NodeGraphComponent& graph, const std::uint64_t nodeId, const ImVec2 origin,
                        const ImVec2 size, const ImU32 colour)
{
    // Inside a module: English, whatever the language.
    const ui::ModuleText english;
    if (!bind(track, graph, nodeId))
        return;
    (void) colour;
    ImGui::PushID(static_cast<int>(nodeId) + 37'000);
    ImGui::SetCursorScreenPos(origin);
    ImGui::BeginGroup();
    // Its window opens from the card's header.
    ImGui::TextDisabled("%s", kick::displayValue(kick::bodyShape, value(kick::bodyShape), ui::french()).c_str());
    const auto knobsHeight = dp(26.0f + 16.0f);
    const auto displayOrigin = ImGui::GetCursorScreenPos();
    const ImVec2 displaySize(size.x, std::max(dp(40.0f), size.y - (displayOrigin.y - origin.y) - knobsHeight - dp(6.0f)));
    drawDisplay(displayOrigin, displaySize, false);
    ImGui::SetCursorScreenPos(ImVec2(displayOrigin.x, displayOrigin.y + displaySize.y + dp(4.0f)));
    // One row: what makes a kick.
    knob(kick::pitchStart, L("DÉPART", "START"), 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(kick::pitchEnd, L("FIN", "END"), 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(kick::pitchTime, L("CHUTE", "TIME"), 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(kick::decay, L("DÉCLIN", "DECAY"), 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(kick::clickLevel, L("CLIC", "CLICK"), 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(kick::subLevel, "SUB", 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(kick::drive, "DRIVE", 26.0f);
    ImGui::SameLine(0.0f, dp(2.0f));
    knob(kick::output, L("SORTIE", "OUTPUT"), 26.0f);
    ImGui::EndGroup();
    ImGui::PopID();
}

void KickView::openEditor(const TrackRef& track, const std::uint64_t nodeId, const bool detached)
{
    const auto id = windows_.open(track, nodeId, state_.manyInstrumentWindows);
    if (detached)
        ui::setDetached(id.c_str(), true);
}

void KickView::drawEditor()
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

bool KickView::drawEditorWindow(const bool focus)
{
    const auto target = *editor_;
    auto* slot = studio_.graphFor(target.track);
    if (slot == nullptr || !studio_.isValid(target.track) || !bind(target.track, slot->graph(), target.nodeId)) {
        return false;
    }
    const auto& p = theme::palette();
    ui::beforeDetachable(windowId_.c_str());
    windows_.placeFirst(target.track, target.nodeId);
    ImGui::SetNextWindowSize(ImVec2(dp(1'080.0f), dp(700.0f)), ImGuiCond_FirstUseEver);
    if (focus)
        ImGui::SetNextWindowFocus();
    auto open = true;
    const auto title = juce::String::fromUTF8("Kick \xe2\x80\x94 ") + studio_.trackName(target.track) + juce::String(windowId_);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, theme::mix(kickRed, p.bg2 | 0xff000000u, 0.65f));
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
        ImGui::TextColored(theme::vec(kickRed), "KICK");
        ImGui::PopFont();
        ImGui::SameLine(0.0f, dp(16.0f));
        if (const auto holder = studio_.devicePresetHolder(target.track, target.nodeId)) {
            const auto track = target.track;
            const auto nodeId = target.nodeId;
            presets_.bar("kickPresets", *holder, [this, track, nodeId] { return studio_.devicePresetState(track, nodeId); },
                         [this, track, nodeId](const juce::var& preset, juce::String& error) {
                             return studio_.applyDevicePreset(track, nodeId, preset, error);
                         },
                         dp(330.0f));
        }
        ImGui::SameLine(0.0f, dp(16.0f));
        if (ImGui::Button(L("Initialiser", "Initialise"))) {
            const auto defaults = kick::defaultValues();
            for (int index = 0; index < kick::parameterCount; ++index)
                set(index, defaults[static_cast<std::size_t>(index)]);
        }
        ui::tooltip(L("Tous les réglages à leur valeur de départ (Ctrl+Z l'annule)", "Every setting back to its start (Ctrl+Z undoes it)"));
        if (ImGui::BeginTabBar("##kickTabs")) {
            if (ImGui::BeginTabItem("KICK")) {
                drawKickTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("EFFETS", "FX"))) {
                // What a drum's channel strip has, as CyDAW's devices.
                static constexpr instrument_effects::Choice effects[] = {
                    { "drive", "Distorsion", "Distortion" },
                    { "transient-shaper", "Transitoires", "Transients" },
                    { "comp-pro", "Compresseur", "Compressor" },
                    { "eq-pro", "Égaliseur", "EQ" },
                    { "multimode-filter", "Filtre", "Filter" },
                    { "bitcrusher", "Réducteur", "Bitcrusher" },
                    { "amp-sim", "Ampli", "Amp" },
                    { "ping-pong", "Délai", "Delay" },
                    { "reverb", "Réverbe", "Reverb" },
                    { "limiter", "Limiteur", "Limiter" },
                };
                ImGui::BeginChild("##kickEffects", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
                instrument_effects::draw(studio_, state_, target.track, "kick-synth", "Kick", effects, kickRed);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
    return open;
}

void KickView::drawKickTab()
{
    {
        ImGui::TextDisabled("%s", L("Glisser les points : hauteur (jaune), enveloppe (bleu), longueur (blanc).",
                                    "Drag the points: pitch (yellow), envelope (blue), length (white)."));
        // The picture takes what the two rows of sections leave.
        const auto available = ImGui::GetContentRegionAvail();
        const auto sectionHeight = dp(32.0f + 16.0f) + ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing()
                                   + dp(14.0f);
        const auto displayHeight = std::max(dp(160.0f), available.y - 2.0f * (sectionHeight + dp(6.0f)) - dp(6.0f));
        const auto displayOrigin = ImGui::GetCursorScreenPos();
        drawDisplay(displayOrigin, ImVec2(available.x, displayHeight), true);
        ImGui::SetCursorScreenPos(ImVec2(displayOrigin.x, displayOrigin.y + displayHeight + dp(6.0f)));
        const auto gap = ImGui::GetStyle().ItemSpacing.x;
        const auto row = available.x;
        // Body, amplitude, click; then sub, drive, tone, global.
        section("##body", L("CORPS", "BODY"), row * 0.38f - gap, sectionHeight, &KickView::drawBody);
        ImGui::SameLine();
        section("##amplitude", "AMPLITUDE", row * 0.34f - gap, sectionHeight, &KickView::drawAmplitude);
        ImGui::SameLine();
        section("##click", L("CLIC", "CLICK"), row * 0.28f, sectionHeight, &KickView::drawClick);
        section("##sub", "SUB", row * 0.22f - gap, sectionHeight, &KickView::drawSub);
        ImGui::SameLine();
        section("##drive", L("SATURATION", "DRIVE"), row * 0.22f - gap, sectionHeight, &KickView::drawDrive);
        ImGui::SameLine();
        section("##tone", L("TIMBRE", "TONE"), row * 0.28f - gap, sectionHeight, &KickView::drawTone);
        ImGui::SameLine();
        section("##global", "GLOBAL", row * 0.28f, sectionHeight, &KickView::drawGlobal);
    }
}

} // namespace cydaw::v2
