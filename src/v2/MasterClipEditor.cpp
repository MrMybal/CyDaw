#include "MasterClipEditor.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

// A lane's value as read: "-6.0 dB", "1.20 kHz", "0.45".
std::string valueText(const double value, const juce::String& unit)
{
    char text[48];
    if (unit == "Hz" && std::abs(value) >= 1'000.0)
        std::snprintf(text, sizeof(text), "%.2f kHz", value / 1'000.0);
    else if (unit.isNotEmpty())
        std::snprintf(text, sizeof(text), std::abs(value) < 10.0 ? "%.2f %s" : "%.1f %s", value, unit.toRawUTF8());
    else
        std::snprintf(text, sizeof(text), "%.2f", value);
    return text;
}

} // namespace

MasterClipEditor::MasterClipEditor(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void MasterClipEditor::draw(const int scene)
{
    const auto& p = theme::palette();
    const auto& io = ImGui::GetIO();
    const auto* found = studio_.masterClip(scene);
    if (found == nullptr) {
        state_.editedMasterScene.reset();
        return;
    }
    auto clip = *found;
    auto changed = false;
    const auto choices = studio_.masterLaneChoices();
    if (shownScene_ != scene) {
        // Another clip: its first lane with points, else the level.
        shownScene_ = scene;
        dragged_ = -1;
        lane_ = clip.lanes.empty() ? juce::String(Studio::masterGainLane) : clip.lanes.begin()->first;
    }
    // A knob gone with its device: the level (always there) instead.
    auto chosen = std::find_if(choices.begin(), choices.end(), [this](const auto& choice) { return choice.binding == lane_; });
    if (chosen == choices.end())
        chosen = choices.begin();
    const auto& choice = *chosen;
    const auto hasPoints = [&clip](const juce::String& binding) {
        const auto lane = clip.lanes.find(binding);
        return lane != clip.lanes.end() && !lane->second.empty();
    };
    const auto labelOf = [&](const Studio::MasterLaneChoice& lane) {
        return hasPoints(lane.binding) ? juce::String::fromUTF8("\xe2\x97\x8f ") + lane.label : lane.label;
    };

    // ---- Its toolbar: name, length, loop, launch; the lane, the grid.
    {
        const auto chip = ImGui::GetCursorScreenPos();
        const auto height = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(chip.x, chip.y + height * 0.2f), ImVec2(chip.x + dp(4.0f), chip.y + height * 0.8f),
                                                  theme::fromArgb(clip.colour), dp(2.0f));
        ImGui::Dummy(ImVec2(dp(6.0f), height));
    }
    ImGui::SameLine(0.0f, dp(4.0f));
    char name[96];
    std::snprintf(name, sizeof(name), "%s", clip.name.toRawUTF8());
    ImGui::SetNextItemWidth(dp(150.0f));
    if (ImGui::InputText("##masterClipName", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue)) {
        clip.name = juce::String::fromUTF8(name);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s %d", L("Master · scène", "Master · scene"), scene + 1);

    ui::toolbarSeparator();
    ImGui::TextDisabled("%s", L("Longueur", "Length"));
    ImGui::SameLine();
    auto length = static_cast<float>(clip.lengthBeats);
    if (ui::dragNumber("##masterLength", &length, 0.25f, 0.25f, 1'024.0f, L("%.2f temps", "%.2f beats"), dp(96.0f))) {
        clip.lengthBeats = std::max(0.25, static_cast<double>(length));
        changed = true;
    }
    ImGui::SameLine();
    if (ui::toggle(L("Boucle", "Loop"), &clip.looping, p.accent))
        changed = true;
    ImGui::SameLine();
    const auto playing = studio_.playingMasterScene() == scene;
    if (ui::iconButton(playing ? icon::stop : icon::play, playing ? L("Arrêter le clip", "Stop the clip") : L("Lancer le clip", "Launch the clip"),
                       playing, p.play)) {
        if (playing)
            studio_.stopMasterClip();
        else
            (void) studio_.launchMasterClip(scene);
    }

    ui::toolbarSeparator();
    ImGui::TextDisabled("%s", L("Courbe", "Lane"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(240.0f));
    if (ImGui::BeginCombo("##masterLane", labelOf(choice).toRawUTF8(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##laneFilter", L("Chercher un réglage", "Find a setting"), filter_, sizeof(filter_));
        const auto filter = juce::String::fromUTF8(filter_).trim();
        for (const auto& lane : choices) {
            if (filter.isNotEmpty() && !lane.label.containsIgnoreCase(filter))
                continue;
            ImGui::PushID(lane.binding.toRawUTF8());
            if (ImGui::Selectable(labelOf(lane).toRawUTF8(), lane.binding == choice.binding)) {
                lane_ = lane.binding;
                dragged_ = -1;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ui::tooltip(L("Ce que la courbe fait bouger : le volume du master, ou un réglage de ses effets (● : a des points)",
                  "What the lane moves: the master's level, or a setting of its effects (●: has points)"));
    ImGui::SameLine();
    const auto beatsPerBar = std::max(1.0, static_cast<double>(studio_.engine().beatsPerBar()));
    static constexpr double grids[] = { -1.0, 1.0, 0.5, 0.25, 0.0 };
    const char* gridNames[] = { L("1 mesure", "1 bar"), L("1 temps", "1 beat"), "1/8", "1/16", L("Libre", "Off") };
    auto gridIndex = 1;
    for (auto index = 0; index < 5; ++index)
        if (std::abs(gridBeats_ - grids[index]) < 1.0e-6)
            gridIndex = index;
    ImGui::SetNextItemWidth(dp(80.0f));
    if (ImGui::Combo("##masterGrid", &gridIndex, gridNames, 5))
        gridBeats_ = grids[gridIndex];
    ui::tooltip(L("La grille des points (Alt : libre)", "The points' grid (Alt: free)"));
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Effacer la courbe", "Clear the lane"))) {
        clip.lanes.erase(choice.binding);
        dragged_ = -1;
        changed = true;
    }

    // ---- The lane: its points over the clip's beats.
    auto points = hasPoints(choice.binding) ? clip.lanes.at(choice.binding) : std::vector<MasterClip::Point> {};
    const auto available = ImGui::GetContentRegionAvail();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto axisWidth = dp(70.0f);
    const auto rulerHeight = dp(18.0f);
    const ImVec2 gridMin(origin.x + axisWidth, origin.y + rulerHeight);
    const ImVec2 gridMax(origin.x + std::max(available.x, axisWidth + dp(120.0f)) - dp(6.0f),
                         origin.y + std::max(available.y, rulerHeight + dp(80.0f)) - dp(6.0f));
    const auto width = gridMax.x - gridMin.x;
    const auto height = gridMax.y - gridMin.y;
    const auto lengthBeats = clip.lengthBeats;
    const auto minimum = choice.minimum;
    const auto maximum = std::max(choice.maximum, minimum + 1.0e-6);
    const auto logarithmic = choice.logarithmic && minimum > 0.0;
    const auto toY = [&](const double value) {
        const auto t = logarithmic ? std::log(std::max(value, minimum) / minimum) / std::log(maximum / minimum)
                                   : (value - minimum) / (maximum - minimum);
        return gridMax.y - static_cast<float>(std::clamp(t, 0.0, 1.0)) * height;
    };
    const auto fromY = [&](const float y) {
        const auto t = std::clamp(static_cast<double>(gridMax.y - y) / height, 0.0, 1.0);
        return logarithmic ? minimum * std::pow(maximum / minimum, t) : minimum + (maximum - minimum) * t;
    };
    const auto toX = [&](const double beats) { return gridMin.x + static_cast<float>(beats / lengthBeats) * width; };
    const auto fromX = [&](const float x) { return std::clamp(static_cast<double>(x - gridMin.x) / width * lengthBeats, 0.0, lengthBeats); };
    const auto grid = gridBeats_ < 0.0 ? beatsPerBar : gridBeats_;
    const auto snap = [&](const double beats) {
        return io.KeyAlt || grid <= 0.0 ? beats : std::clamp(std::round(beats / grid) * grid, 0.0, lengthBeats);
    };

    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(gridMin, gridMax, p.bg0);
    // Beats and bars, their numbers in the ruler.
    const auto pixelsPerBeat = width / static_cast<float>(lengthBeats);
    for (auto beat = 0.0; beat <= lengthBeats + 1.0e-6; beat += 1.0) {
        const auto bar = std::fmod(beat, beatsPerBar) < 1.0e-6;
        if (!bar && pixelsPerBeat < dp(6.0f))
            continue;
        const auto x = toX(beat);
        list->AddLine(ImVec2(x, gridMin.y), ImVec2(x, gridMax.y), bar ? p.gridStrong : p.grid);
        if (bar && (pixelsPerBeat * beatsPerBar >= dp(28.0f) || std::fmod(beat / beatsPerBar, 4.0) < 1.0e-6)) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d", static_cast<int>(beat / beatsPerBar) + 1);
            list->AddText(ImVec2(x + dp(3.0f), origin.y + dp(2.0f)), p.textDim, text);
        }
    }
    // Five values on the side.
    for (auto step = 0; step <= 4; ++step) {
        const auto y = gridMax.y - height * static_cast<float>(step) / 4.0f;
        list->AddLine(ImVec2(gridMin.x, y), ImVec2(gridMax.x, y), p.grid);
        const auto text = valueText(fromY(y), choice.unit);
        const auto textWidth = ImGui::CalcTextSize(text.c_str()).x;
        list->AddText(ImVec2(gridMin.x - textWidth - dp(6.0f), std::clamp(y - ImGui::GetFontSize() * 0.5f, gridMin.y, gridMax.y - ImGui::GetFontSize())),
                      p.textDim, text.c_str());
    }

    // The points: a click adds one (and drags it), a drag moves it between
    // its neighbours, a right click removes it.
    ImGui::SetCursorScreenPos(gridMin);
    ImGui::InvisibleButton("##masterLaneArea", ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const auto hovered = ImGui::IsItemHovered();
    auto hit = -1;
    {
        auto best = dp(8.0f);
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto dx = toX(points[index].beats) - io.MousePos.x;
            const auto dy = toY(points[index].value) - io.MousePos.y;
            const auto distance = std::sqrt(dx * dx + dy * dy);
            if (distance < best) {
                best = distance;
                hit = static_cast<int>(index);
            }
        }
    }
    if (dragged_ >= static_cast<int>(points.size()))
        dragged_ = -1;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        if (hit >= 0) {
            dragged_ = hit;
        } else {
            const MasterClip::Point point { snap(fromX(io.MousePos.x)), static_cast<float>(fromY(io.MousePos.y)) };
            const auto at = std::upper_bound(points.begin(), points.end(), point.beats,
                                             [](const double beats, const MasterClip::Point& other) { return beats < other.beats; });
            dragged_ = static_cast<int>(std::distance(points.begin(), points.insert(at, point)));
            changed = true;
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && hit >= 0) {
        points.erase(points.begin() + hit);
        dragged_ = -1;
        hit = -1;
        changed = true;
    }
    if (dragged_ >= 0 && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)
        && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
        auto& point = points[static_cast<std::size_t>(dragged_)];
        const auto low = dragged_ > 0 ? points[static_cast<std::size_t>(dragged_) - 1].beats : 0.0;
        const auto high = dragged_ + 1 < static_cast<int>(points.size()) ? points[static_cast<std::size_t>(dragged_) + 1].beats : lengthBeats;
        point.beats = std::clamp(snap(fromX(io.MousePos.x)), low, high);
        point.value = static_cast<float>(fromY(io.MousePos.y));
        changed = true;
    }
    if (!ImGui::IsItemActive())
        dragged_ = -1;

    // The curve, filled under; before the first point and after the last,
    // their values held. With none: the setting as it stands, faint.
    ImGui::PushClipRect(gridMin, gridMax, true);
    const auto curveColour = theme::fromArgb(clip.colour);
    const auto fillColour = (curveColour & 0x00ffffffu) | 0x30000000u;
    if (points.empty()) {
        const auto y = toY(choice.value);
        list->AddLine(ImVec2(gridMin.x, y), ImVec2(gridMax.x, y), p.textFaint, 1.0f);
        ui::centredText(list, gridMin, gridMax, p.textFaint,
                        L("Clic : ajouter un point. La courbe joue quand la scène est lancée.",
                          "Click: add a point. The lane plays when the scene is launched."));
    } else {
        std::vector<ImVec2> line;
        line.emplace_back(gridMin.x, toY(points.front().value));
        for (const auto& point : points)
            line.emplace_back(toX(point.beats), toY(point.value));
        line.emplace_back(gridMax.x, toY(points.back().value));
        for (std::size_t index = 1; index < line.size(); ++index)
            list->AddQuadFilled(line[index - 1], line[index], ImVec2(line[index].x, gridMax.y), ImVec2(line[index - 1].x, gridMax.y), fillColour);
        list->AddPolyline(line.data(), static_cast<int>(line.size()), curveColour, dp(1.8f));
        for (std::size_t index = 0; index < points.size(); ++index) {
            const ImVec2 at(toX(points[index].beats), toY(points[index].value));
            const auto active = static_cast<int>(index) == dragged_ || static_cast<int>(index) == hit;
            list->AddCircleFilled(at, dp(active ? 5.0f : 4.0f), active ? p.text : curveColour);
            list->AddCircle(at, dp(active ? 5.0f : 4.0f), p.bg0, 0, 1.0f);
        }
    }
    // Where the playing clip is.
    if (playing) {
        const auto x = gridMin.x + width * std::clamp(studio_.masterClipProgress(), 0.0f, 1.0f);
        list->AddLine(ImVec2(x, gridMin.y), ImVec2(x, gridMax.y), p.play, dp(1.5f));
    }
    ImGui::PopClipRect();
    list->AddRect(gridMin, gridMax, p.line);

    if (dragged_ >= 0 || (hovered && hit >= 0)) {
        const auto& point = points[static_cast<std::size_t>(dragged_ >= 0 ? dragged_ : hit)];
        ImGui::SetTooltip("%s %.2f  ·  %s%s", L("Temps", "Beat"), point.beats + 1.0, valueText(point.value, choice.unit).c_str(),
                          dragged_ >= 0 ? "" : L("\nClic droit : retirer", "\nRight click: remove"));
    } else if (hovered) {
        ImGui::SetTooltip("%s %.2f  ·  %s", L("Temps", "Beat"), snap(fromX(io.MousePos.x)) + 1.0, valueText(fromY(io.MousePos.y), choice.unit).c_str());
    }

    if (changed) {
        if (points.empty())
            clip.lanes.erase(choice.binding);
        else
            clip.lanes[choice.binding] = std::move(points);
        studio_.setMasterClip(scene, std::move(clip));
    }
}

} // namespace cydaw::v2
