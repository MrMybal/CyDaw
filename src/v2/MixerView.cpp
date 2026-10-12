#include "MixerView.h"

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
// The pan beside the name; the fader and its meter fill the rest.
constexpr float kPanDiameter = 20.0f;
constexpr float kFaderWidth = 44.0f;
constexpr float kMeterWidth = 20.0f;
constexpr float kFaderTop = 40.0f;
constexpr float kFaderBlock = kFaderWidth + 6.0f + kMeterWidth;

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
    const auto tracks = studio_.tracks();
    const auto height = ImGui::GetContentRegionAvail().y;
    const auto stripWidth = dp(kStripWidth);
    const auto masterWidth = stripWidth + dp(12.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(3.0f), dp(3.0f)));
    ImGui::BeginChild("##strips", ImVec2(ImGui::GetContentRegionAvail().x - masterWidth, height), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (tracks.empty())
        ui::emptyState(L("Aucune piste", "No tracks"), L("Ajoutez une piste depuis l'arrangement.", "Add a track from the arrangement."));
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        if (index > 0)
            ImGui::SameLine();
        mixer_strip::track(studio_, state_, tracks[index], static_cast<int>(index), stripWidth,
                           height - ImGui::GetStyle().ScrollbarSize - dp(4.0f));
    }
    ImGui::EndChild();
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
    ImGui::InvisibleButton("##select", ImVec2(width, ImGui::GetFontSize() + dp(4.0f)),
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
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.75f);
    const auto bus = studio_.isBusTrack(track);
    list->AddText(ImVec2(origin.x + dp(6.0f), origin.y + dp(8.0f) + ImGui::GetFontSize() * 1.35f),
                  track.midi ? p.midi : bus ? p.accent : p.audio, track.midi ? "MIDI" : bus ? "BUS" : "AUDIO");
    ImGui::PopFont();

    auto mix = studio_.trackMix(track);
    auto changed = false;
    // Pan: small, beside the name.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - dp(kPanDiameter + 12.0f) - dp(2.0f), origin.y + dp(6.0f)));
    ui::KnobStyle panStyle;
    panStyle.diameter = kPanDiameter;
    panStyle.format = "%+.2f";
    panStyle.flags = ui::knobBipolar | ui::knobNoLabel;
    panStyle.colour = colour;
    changed |= ui::knob("##pan", &mix.pan, -1.0f, 1.0f, 0.0f, panStyle);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pan %+.2f", static_cast<double>(mix.pan));

    // Fader and meter, as tall as the strip lets them.
    const auto faderTop = origin.y + dp(kFaderTop);
    const auto buttonsHeight = dp(60.0f);
    const auto faderHeight = std::max(dp(40.0f), height - (faderTop - origin.y) - buttonsHeight);
    const auto left = origin.x + inset + std::max(dp(4.0f), (dp(kStripWidth) - dp(kFaderBlock)) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(left, faderTop));
    changed |= ui::fader("##fader", &mix.gainDb, -60.0f, 12.0f, ImVec2(dp(kFaderWidth), faderHeight), colour);
    const auto meterState = studio_.trackMeter(track);
    ImGui::SetCursorScreenPos(ImVec2(left + dp(kFaderWidth + 6.0f), faderTop + dp(7.0f)));
    ui::meter("##meter", ImVec2(dp(kMeterWidth), faderHeight - dp(14.0f)), meterState.peak[0], meterState.peak[1],
              meterState.rms[0], meterState.rms[1], true);
    ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 0.78f);
    {
        const auto text = gainText(mix.gainDb);
        const auto textWidth = ImGui::CalcTextSize(text.c_str()).x;
        list->AddText(ImVec2(origin.x + (width - textWidth) * 0.5f, faderTop + faderHeight + dp(1.0f)), p.textDim, text.c_str());
    }
    ImGui::PopFont();

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
    changed |= ui::toggle("M", &mix.muted, p.mute, size, L("Muet", "Mute"));
    ImGui::SameLine(0.0f, dp(3.0f));
    changed |= ui::toggle("S", &mix.solo, p.solo, size, L("Solo", "Solo"));
    ImGui::SameLine(0.0f, dp(3.0f));
    changed |= ui::toggle("R", &mix.armed, p.record, size, L("Armer", "Arm"));
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
    if (ui::fader("##masterFader", &gain, -60.0f, 12.0f, ImVec2(dp(kFaderWidth), faderHeight), p.accent))
        engine.setMasterGain(gain);
    const auto& meter = studio_.masterMeter();
    ImGui::SetCursorScreenPos(ImVec2(left + dp(kFaderWidth + 6.0f), faderTop + dp(7.0f)));
    ui::meter("##masterMeter", ImVec2(dp(kMeterWidth), faderHeight - dp(14.0f)), meter.peak[0], meter.peak[1], meter.rms[0],
              meter.rms[1], true);
    ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 0.78f);
    {
        const auto text = gainText(gain);
        const auto textWidth = ImGui::CalcTextSize(text.c_str()).x;
        list->AddText(ImVec2(origin.x + (width - textWidth) * 0.5f, faderTop + faderHeight + dp(1.0f)), p.textDim, text.c_str());
    }
    ImGui::PopFont();
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
