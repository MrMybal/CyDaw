#include "MixerView.h"
#include "AddTrackMenu.h"
#include "ControlTargets.h"

#include "RoutingUi.h"

#include "Icons.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

constexpr float kStripWidth = 84.0f;
// The pan on its own row under the name, its value beside it; the fader and
// its meter fill the rest.
constexpr float kPanDiameter = 26.0f;
// The meter carries the scale in dB (its numbers on its left).
constexpr float kFaderWidth = 38.0f;
constexpr float kMeterGap = 3.0f;
constexpr float kMeterWidth = 32.0f;
constexpr float kFaderTop = 62.0f;
constexpr float kFaderBlock = kFaderWidth + kMeterGap + kMeterWidth;

// A pan as read: "C", "L 30", "R 20" (percent).
std::string panText(const float pan)
{
    const auto percent = static_cast<int>(std::lround(std::abs(pan) * 100.0f));
    if (percent == 0)
        return "C";
    char text[16];
    std::snprintf(text, sizeof(text), "%s %d", pan < 0.0f ? "L" : "R", percent);
    return text;
}

std::string gainText(float db);

// The text under a fader, a click on it types the level in.
bool gainEntry(ImDrawList* list, const ImVec2 origin, const float width, const float y, float& gainDb)
{
    const auto& p = theme::palette();
    ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 0.78f);
    const auto text = gainText(gainDb);
    const auto textWidth = ImGui::CalcTextSize(text.c_str()).x;
    const auto height = ImGui::GetFontSize() + dp(2.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(4.0f), y));
    auto typed = static_cast<double>(gainDb);
    const auto set = ui::valueEntryButton("##gainText", ImVec2(width - dp(8.0f), height), typed, -60.0, 12.0, 1.0, "dB");
    const auto hovered = ImGui::IsItemHovered();
    list->AddText(ImVec2(origin.x + (width - textWidth) * 0.5f, y + dp(1.0f)), hovered ? p.text : p.textDim, text.c_str());
    ImGui::PopFont();
    if (hovered)
        ImGui::SetTooltip("%s", L("Clic : taper le niveau (dB)", "Click: type the level (dB)"));
    if (set)
        gainDb = static_cast<float>(typed);
    return set;
}

std::string gainText(const float db)
{
    char text[32];
    if (db <= -59.9f)
        std::snprintf(text, sizeof(text), "-inf");
    else
        std::snprintf(text, sizeof(text), "%+.1f", db);
    return text;
}

} // namespace

MixerView::MixerView(Studio& studio, UiState& state) : studio_(studio), state_(state) {}

void MixerView::draw()
{
    const auto& p = theme::palette();
    const auto tracks = studio_.shownTracks();
    const auto height = ImGui::GetContentRegionAvail().y;
    const auto stripWidth = dp(kStripWidth);
    const auto masterWidth = stripWidth + dp(12.0f);

    // The tracks, then the buses beside the master (as Bitwig's effect tracks).
    std::vector<TrackRef> plain;
    std::vector<TrackRef> buses;
    for (const auto& track : tracks)
        (studio_.isEffectBus(track) ? buses : plain).push_back(track);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(3.0f), dp(3.0f)));
    const auto busesWidth = buses.empty() ? 0.0f
                                          : std::min(static_cast<float>(buses.size()) * (stripWidth + dp(3.0f)),
                                                     std::max(stripWidth, ImGui::GetContentRegionAvail().x * 0.35f));
    ImGui::BeginChild("##strips", ImVec2(ImGui::GetContentRegionAvail().x - masterWidth - busesWidth - (buses.empty() ? 0.0f : dp(9.0f)), height),
                      ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    if (tracks.empty())
        ui::emptyState(L("Aucune piste", "No tracks"), L("Ajoutez une piste avec le +.", "Add a track with the +."));
    std::vector<float> lefts;
    const auto stripsTop = ImGui::GetCursorScreenPos().y;
    const auto stripHeight = height - ImGui::GetStyle().ScrollbarSize - dp(4.0f);
    for (std::size_t index = 0; index < plain.size(); ++index) {
        if (index > 0)
            ImGui::SameLine();
        lefts.push_back(ImGui::GetCursorScreenPos().x);
        mixer_strip::track(studio_, state_, plain[index], static_cast<int>(index), stripWidth, stripHeight);
    }
    mixer_strip::groupFrames(studio_, ImGui::GetWindowDrawList(), plain, lefts, stripWidth, stripsTop, stripsTop + stripHeight, dp(3.0f));
    // After the last strip: a "+" that adds a track.
    if (!plain.empty())
        ImGui::SameLine();
    add_track::button(studio_, state_, "mixerAdd", ImVec2(dp(34.0f), dp(34.0f)));
    ImGui::EndChild();
    if (!buses.empty()) {
        ImGui::SameLine(0.0f, dp(6.0f));
        ImGui::BeginChild("##busStrips", ImVec2(busesWidth, height), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        for (std::size_t index = 0; index < buses.size(); ++index) {
            if (index > 0)
                ImGui::SameLine();
            mixer_strip::track(studio_, state_, buses[index], static_cast<int>(index) + 10'000, stripWidth,
                               height - ImGui::GetStyle().ScrollbarSize - dp(4.0f));
        }
        ImGui::EndChild();
    }
    ImGui::SameLine();
    const auto origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x - dp(2.0f), origin.y), ImVec2(origin.x - dp(2.0f), origin.y + height),
                                        p.lineStrong);
    mixer_strip::master(studio_, state_, stripWidth, height - dp(4.0f));
    ImGui::PopStyleVar();
}

float mixer_strip::naturalWidth()
{
    return dp(kStripWidth);
}

void mixer_strip::groupFrames(Studio& studio, ImDrawList* list, const std::vector<TrackRef>& shown, const std::vector<float>& lefts,
                              const float width, const float top, const float bottom, const float gap)
{
    for (std::size_t first = 0; first < shown.size() && first < lefts.size(); ++first) {
        if (!studio.isGroupTrack(shown[first]))
            continue;
        // The group, then its tracks next to it (folded: the group alone).
        auto last = first;
        while (last + 1 < shown.size() && last + 1 < lefts.size()) {
            const auto group = studio.groupOf(shown[last + 1]);
            if (!group || !(*group == shown[first]))
                break;
            ++last;
        }
        const auto colour = theme::fromArgb(studio.trackColour(shown[first]));
        const auto half = std::max(1.0f, gap * 0.5f);
        const ImVec2 min(lefts[first] - half, top - half);
        const ImVec2 max(lefts[last] + width + half, bottom + half);
        list->AddRectFilled(min, ImVec2(max.x, min.y + dp(4.0f) + half), colour, dp(4.0f), ImDrawFlags_RoundCornersTop);
        list->AddRect(min, max, theme::alpha(colour, 0.9f), dp(4.0f), dp(1.6f));
    }
}

void mixer_strip::track(Studio& studio_, UiState& state_, const TrackRef& track, const int index, const float width,
                        const float height)
{
    const auto& p = theme::palette();
    // The controls keep their size; a wider strip centres them.
    const auto inset = std::max(0.0f, (width - dp(kStripWidth)) * 0.5f);
    const auto selected = state_.selectedTrack && *state_.selectedTrack == track;
    const auto colour = theme::fromArgb(studio_.trackColour(track));
    ImGui::PushID(index);
    ImGui::PushID(track.midi ? "m" : "a");
    const auto origin = ImGui::GetCursorScreenPos();
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), selected ? p.bg3 : p.bg2, dp(4.0f));
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + dp(4.0f)), colour, dp(4.0f), ImDrawFlags_RoundCornersTop);
    if (selected)
        list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), theme::alpha(p.accent, 0.6f), dp(4.0f));

    ImGui::BeginGroup();
    // Name, clicking selects.
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + dp(6.0f)));
    ImGui::InvisibleButton("##select", ImVec2(width, ImGui::GetFontSize() + dp(2.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (ImGui::IsItemClicked())
        state_.selectTrack(track);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        state_.selectTrack(track);
        state_.contextRequest = ContextRequest { ContextRequest::Kind::track, track };
    }
    ui::tooltip(L("Clic : choisir la piste. Clic droit : son menu.", "Click: pick the track. Right click: its menu."));
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 0.9f);
    ui::clippedText(list, ImVec2(origin.x + dp(6.0f), origin.y + dp(7.0f)), width - dp(40.0f), p.text,
                    studio_.trackName(track).toRawUTF8());
    ImGui::PopFont();
    auto mix = studio_.trackMix(track);
    auto changed = false;
    // What a controller can move here (learn mode): a track's own, not a
    // bus of the software or a rack's pad.
    const auto learnable = !track.master && track.bus == 0 && track.pad < 0;
    const auto learnKey = learnable ? studio_.routingKey(track) : 0;
    const auto learnTarget = [&](const char* what) { return learnable ? control_targets::forTrack(learnKey, what) : std::string(); };
    const auto learnLabel = [&](const juce::String& what) { return (studio_.trackName(track) + juce::String::fromUTF8(" \xc2\xb7 ") + what).toStdString(); };
    // Pan: its own row under the name, a knob big enough to take, its value
    // beside it ("C", "L 30"), which a click types in; the track's kind on
    // the right.
    const auto panTop = origin.y + dp(24.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + inset + dp(2.0f), panTop));
    ui::KnobStyle panStyle;
    panStyle.diameter = kPanDiameter;
    panStyle.format = "%+.2f";
    panStyle.flags = ui::knobBipolar | ui::knobNoLabel;
    panStyle.colour = colour;
    {
        const ui::LearnScope learn(learnTarget("pan"), learnLabel(ui::Ls("Panoramique", "Pan")));
        changed |= ui::knob("##pan", &mix.pan, -1.0f, 1.0f, 0.0f, panStyle);
    }
    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
        ImGui::SetTooltip("%s", (L("Panoramique ", "Pan ") + panText(mix.pan) + L(" · double-clic : centre", " · double click: centre"))
                                    .c_str());
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
        const auto text = panText(mix.pan);
        const auto textHeight = ImGui::GetFontSize() + dp(4.0f);
        const ImVec2 at(origin.x + inset + dp(kPanDiameter + 18.0f), panTop + (dp(kPanDiameter) - textHeight) * 0.5f);
        ImGui::SetCursorScreenPos(at);
        auto typed = static_cast<double>(mix.pan);
        if (ui::valueEntryButton("##panText", ImVec2(dp(34.0f), textHeight), typed, -1.0, 1.0, 100.0)) {
            mix.pan = static_cast<float>(typed);
            changed = true;
        }
        const auto hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetTooltip("%s", L("Clic : taper le panoramique (-100 à gauche, 100 à droite)",
                                      "Click: type the pan (-100 left, 100 right)"));
        list->AddText(ImVec2(at.x, at.y + dp(2.0f)), hovered ? p.text : p.textDim, text.c_str());
        ImGui::PopFont();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.7f);
        const auto bus = studio_.isBusTrack(track);
        const auto group = studio_.isGroupTrack(track);
        const auto hybrid = !track.master && studio_.companionOf(track).has_value();
        const auto* kind = group ? L("GRP", "GRP") : hybrid ? "HYB" : track.midi ? "MIDI" : bus ? "BUS" : "AUDIO";
        const auto kindWidth = ImGui::CalcTextSize(kind).x;
        list->AddText(ImVec2(origin.x + width - kindWidth - dp(6.0f), at.y + dp(3.0f)),
                      group ? colour : hybrid ? p.accent : track.midi ? p.midi : bus ? p.accent : p.audio, kind);
        ImGui::PopFont();
    }

    // Fader and meter, as tall as the strip lets them.
    const auto faderTop = origin.y + dp(kFaderTop);
    const auto buttonsHeight = dp(60.0f);
    const auto faderHeight = std::max(dp(40.0f), height - (faderTop - origin.y) - buttonsHeight);
    const auto left = origin.x + inset + std::max(dp(4.0f), (dp(kStripWidth) - dp(kFaderBlock)) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(left, faderTop));
    {
        const ui::LearnScope learn(learnTarget("volume"), learnLabel(ui::Ls("Volume", "Volume")));
        changed |= ui::fader("##fader", &mix.gainDb, -60.0f, 12.0f, ImVec2(dp(kFaderWidth), faderHeight), colour, false);
    }
    const auto meterState = studio_.trackMeter(track);
    ImGui::SetCursorScreenPos(ImVec2(left + dp(kFaderWidth + kMeterGap), faderTop + dp(7.0f)));
    ui::meter("##meter", ImVec2(dp(kMeterWidth), faderHeight - dp(14.0f)), meterState.peak[0], meterState.peak[1],
              meterState.rms[0], meterState.rms[1], true, true);
    changed |= gainEntry(list, origin, width, faderTop + faderHeight, mix.gainDb);

    // Where it goes: a click edits its outputs and sends.
    {
        const ImVec2 at(origin.x + dp(5.0f), origin.y + height - dp(41.0f));
        const ImVec2 size(width - dp(10.0f), dp(16.0f));
        ImGui::SetCursorScreenPos(at);
        if (ImGui::InvisibleButton("##output", size)) {
            state_.selectTrack(track);
            state_.contextRequest = ContextRequest { ContextRequest::Kind::outputs, track };
        }
        const auto hovered = ImGui::IsItemHovered();
        list->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), hovered ? p.bg4 : p.bg1, dp(3.0f));
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.75f);
        ui::clippedText(list, ImVec2(at.x + dp(4.0f), at.y + (size.y - ImGui::GetFontSize()) * 0.5f), size.x - dp(8.0f),
                        hovered ? p.text : p.textDim, routing_ui::mainOutputLabel(studio_, track).toRawUTF8());
        ImGui::PopFont();
        ui::tooltip(L("Sa sortie (le master, un bus, une autre piste) et ses envois : clic pour les régler.",
                      "Its output (the master, a bus, another track) and its sends: click to set them."));
    }

    // Mute, solo, arm.
    const auto buttonY = origin.y + height - dp(22.0f);
    const ImVec2 size((width - dp(16.0f)) / 3.0f, dp(18.0f));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(5.0f), buttonY));
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 0.78f);
    {
        const ui::LearnScope learn(learnTarget("mute"), learnLabel(ui::Ls("Muet", "Mute")));
        changed |= ui::toggle("M", &mix.muted, p.mute, size, L("Muet", "Mute"));
    }
    ImGui::SameLine(0.0f, dp(3.0f));
    {
        const ui::LearnScope learn(learnTarget("solo"), learnLabel("Solo"));
        changed |= ui::toggle("S", &mix.solo, p.solo, size, L("Solo", "Solo"));
    }
    ImGui::SameLine(0.0f, dp(3.0f));
    {
        const ui::LearnScope learn(learnTarget("arm"), learnLabel(ui::Ls("Armer", "Arm")));
        changed |= ui::toggle("R", &mix.armed, p.record, size, L("Armer", "Arm"));
    }
    ImGui::PopFont();
    if (changed)
        studio_.setTrackMix(track, mix);
    if (changed || ImGui::IsItemActivated())
        state_.selectTrack(track);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    ImGui::Dummy(ImVec2(width, 0.0f));
    ImGui::EndGroup();
    ImGui::PopID();
    ImGui::PopID();
}

void mixer_strip::master(Studio& studio_, UiState& state_, const float width, const float height)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto inset = std::max(0.0f, (width - dp(kStripWidth)) * 0.5f);
    const auto origin = ImGui::GetCursorScreenPos();
    auto* list = ImGui::GetWindowDrawList();
    const auto selected = state_.selectedTrack && state_.selectedTrack->isMaster();
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), selected ? p.bg3 : p.bg2, dp(4.0f));
    list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + dp(4.0f)), p.accent, dp(4.0f), ImDrawFlags_RoundCornersTop);
    if (selected)
        list->AddRect(origin, ImVec2(origin.x + width, origin.y + height), theme::alpha(p.accent, 0.6f), dp(4.0f));
    ImGui::PushID("##masterStrip");
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + dp(6.0f)));
    if (ImGui::InvisibleButton("##selectMaster", ImVec2(width, ImGui::GetFontSize() + dp(16.0f))))
        state_.selectTrack(Studio::masterTrack());
    ui::tooltip(L("Clic : le master et ses périphériques (Périphériques, Graphe)",
                  "Click: the master and its devices (Devices, Graph)"));
    ImGui::PopID();
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 0.9f);
    list->AddText(ImVec2(origin.x + dp(6.0f), origin.y + dp(7.0f)), p.text, "MASTER");
    ImGui::PopFont();
    {
        // Its devices, counted under its name.
        auto devices = 0;
        if (const auto* slot = studio_.graphFor(Studio::masterTrack()))
            for (const auto& node : slot->graph().editorNodes())
                devices += node.boundary ? 0 : 1;
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.75f);
        const auto text = juce::String(devices) + ui::Ls(devices > 1 ? " effets" : " effet", devices > 1 ? " effects" : " effect");
        list->AddText(ImVec2(origin.x + dp(6.0f), origin.y + dp(8.0f) + ImGui::GetFontSize() * 1.35f), p.textDim,
                      text.toRawUTF8());
        ImGui::PopFont();
    }
    // Level with the tracks' faders.
    const auto faderTop = origin.y + dp(kFaderTop);
    const auto buttonsHeight = dp(40.0f);
    const auto faderHeight = std::max(dp(40.0f), height - dp(kFaderTop) - buttonsHeight);
    const auto left = origin.x + inset + std::max(dp(4.0f), (dp(kStripWidth) - dp(kFaderBlock)) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(left, faderTop));
    auto gain = engine.masterGainDb();
    {
        const ui::LearnScope learn(control_targets::forMaster("volume"), "Master \xc2\xb7 Volume");
        if (ui::fader("##masterFader", &gain, -60.0f, 12.0f, ImVec2(dp(kFaderWidth), faderHeight), p.accent, false))
            engine.setMasterGain(gain);
    }
    const auto& meter = studio_.masterMeter();
    ImGui::SetCursorScreenPos(ImVec2(left + dp(kFaderWidth + kMeterGap), faderTop + dp(7.0f)));
    ui::meter("##masterMeter", ImVec2(dp(kMeterWidth), faderHeight - dp(14.0f)), meter.peak[0], meter.peak[1], meter.rms[0],
              meter.rms[1], true, true);
    if (gainEntry(list, origin, width, faderTop + faderHeight, gain))
        engine.setMasterGain(gain);
    // Mute: the whole mix silenced, its level kept.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(5.0f), origin.y + height - buttonsHeight + dp(16.0f)));
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 0.78f);
    auto muted = engine.isMasterMuted();
    if (ui::toggle("M##master", &muted, p.mute, ImVec2(width - dp(10.0f), dp(18.0f)), L("Couper le master", "Mute the master")))
        engine.setMasterMuted(muted);
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
    ImGui::Dummy(ImVec2(width, 0.0f));
}

} // namespace cydaw::v2
