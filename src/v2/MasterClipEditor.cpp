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
        curve_.clearSelection();
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
    if (ui::loopToggle("masterClipLoop", &clip.looping, L("Boucle : le clip boucle tant que sa scène joue", "Loop: the clip loops while its scene plays")))
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
                curve_.clearSelection();
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
    (void) ui::snapToggle("masterClipSnap", &state_.masterClipSnap);
    ImGui::SameLine(0.0f, dp(4.0f));
    ImGui::SetNextItemWidth(dp(80.0f));
    if (ImGui::Combo("##masterGrid", &gridIndex, gridNames, 5))
        gridBeats_ = grids[gridIndex];
    ui::tooltip(L("La grille des points (Alt : libre)", "The points' grid (Alt: free)"));
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Effacer la courbe", "Clear the lane"))) {
        clip.lanes.erase(choice.binding);
        curve_.clearSelection();
        changed = true;
    }

    // ---- The lane: its points over the clip's beats, as the curve editor
    // shows them; the bar of the picked ones first, its row kept.
    const auto minimum = choice.minimum;
    const auto maximum = std::max(choice.maximum, minimum + 1.0e-6);
    curves::Lane lane;
    lane.minimum = minimum;
    lane.maximum = maximum;
    lane.logarithmic = choice.logarithmic && minimum > 0.0;
    lane.fallback = choice.value;
    lane.colour = theme::fromArgb(clip.colour);
    lane.filled = true;
    const auto unit = choice.unit;
    lane.formatValue = [unit](const double value) { return juce::String(valueText(value, unit)); };
    lane.formatTime = [beatsPerBar](const double beats) {
        const auto bar = std::floor(beats / beatsPerBar + 1.0e-9);
        return juce::String(static_cast<int>(bar) + 1) + "." + juce::String(beats - bar * beatsPerBar + 1.0, 2);
    };
    // The points with their ids (given to those that have none).
    auto points = std::vector<curves::Point> {};
    if (hasPoints(choice.binding)) {
        std::uint64_t fresh = 1;
        for (const auto& point : clip.lanes.at(choice.binding))
            fresh = std::max(fresh, point.id + 1);
        for (const auto& point : clip.lanes.at(choice.binding))
            points.push_back({ point.id != 0 ? point.id : fresh++, point.beats, point.value, point.shape, point.bend, point.handles });
    }
    const auto grid = gridBeats_ < 0.0 ? beatsPerBar : gridBeats_;
    if (curve_.drawBar(points, lane, grid))
        changed = true;

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
    const auto toX = [&](const double beats) { return gridMin.x + static_cast<float>(beats / lengthBeats) * width; };
    const auto fromX = [&](const float x) { return std::clamp(static_cast<double>(x - gridMin.x) / width * lengthBeats, 0.0, lengthBeats); };
    const auto snap = [&](const double beats) {
        return !ui::snapping(state_.masterClipSnap) || grid <= 0.0 ? beats : std::clamp(std::round(beats / grid) * grid, 0.0, lengthBeats);
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
    // Five values on the side, where the editor puts them.
    const auto pad = dp(5.0f);
    for (auto step = 0; step <= 4; ++step) {
        const auto part = static_cast<double>(step) / 4.0;
        const auto y = gridMax.y - pad - static_cast<float>(part) * std::max(1.0f, height - pad * 2.0f);
        list->AddLine(ImVec2(gridMin.x, y), ImVec2(gridMax.x, y), p.grid);
        const auto text = valueText(curves::Editor::valueOf(lane, part), choice.unit);
        const auto textWidth = ImGui::CalcTextSize(text.c_str()).x;
        list->AddText(ImVec2(gridMin.x - textWidth - dp(6.0f), std::clamp(y - ImGui::GetFontSize() * 0.5f, gridMin.y, gridMax.y - ImGui::GetFontSize())),
                      p.textDim, text.c_str());
    }

    // The curve editor over the grid.
    curves::View view;
    view.xOf = toX;
    view.timeAt = fromX;
    view.snap = snap;
    view.cursor = playing ? lengthBeats * std::clamp(static_cast<double>(studio_.masterClipProgress()), 0.0, 1.0) : 0.0;
    view.minimumTime = 0.0;
    view.maximumTime = lengthBeats;
    view.secondsPerUnit = 60.0 / std::max(20.0, studio_.engine().tempo());
    curve_.setFocused(true);
    if (curve_.draw("##masterCurve", gridMin, gridMax, points, lane, view))
        changed = true;
    ImGui::PushClipRect(gridMin, gridMax, true);
    if (points.empty())
        ui::centredText(list, gridMin, gridMax, p.textFaint,
                        L("Double-clic : ajouter un point. La courbe joue quand la scène est lancée.",
                          "Double click: add a point. The lane plays when the scene is launched."));
    // Where the playing clip is.
    if (playing) {
        const auto x = gridMin.x + width * std::clamp(studio_.masterClipProgress(), 0.0f, 1.0f);
        list->AddLine(ImVec2(x, gridMin.y), ImVec2(x, gridMax.y), p.play, dp(1.5f));
    }
    ImGui::PopClipRect();
    list->AddRect(gridMin, gridMax, p.line);

    if (changed) {
        if (points.empty()) {
            clip.lanes.erase(choice.binding);
        } else {
            auto& stored = clip.lanes[choice.binding];
            stored.clear();
            for (const auto& point : points)
                stored.push_back({ std::clamp(point.time, 0.0, lengthBeats), static_cast<float>(point.value), engine::clampedCurveShape(point.shape),
                                   point.bend, point.handles, point.id });
        }
        studio_.setMasterClip(scene, std::move(clip));
    }
}

} // namespace cydaw::v2
