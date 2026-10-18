#include "ArrangementView.h"
#include "ControlTargets.h"
#include "AddTrackMenu.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
using AudioClip = app::AudioTrackController::AudioClip;
using MidiClip = app::AudioTrackController::MidiClip;

namespace {

constexpr float kLaneScale = 0.7f;
constexpr double kMinimumClipSeconds = 0.02;
constexpr float kAutomationMinimumDb = -60.0f;
constexpr float kAutomationMaximumDb = 12.0f;

// Volume on the automation band: squared, so the useful range (-24..+6 dB)
// gets most of the height.
float automationPosition(const float valueDb)
{
    const auto t = std::clamp((valueDb - kAutomationMinimumDb) / (kAutomationMaximumDb - kAutomationMinimumDb), 0.0f, 1.0f);
    return t * t;
}

float automationValue(const float position)
{
    return std::sqrt(std::clamp(position, 0.0f, 1.0f)) * (kAutomationMaximumDb - kAutomationMinimumDb) + kAutomationMinimumDb;
}

const AudioClip* findAudioClip(const app::AudioTrackController& engine, const std::size_t index,
                               const std::uint64_t id)
{
    if (const auto* clips = engine.audioClipsView(index))
        for (const auto& clip : *clips)
            if (clip.id == id)
                return &clip;
    return nullptr;
}

const MidiClip* findMidiClip(const app::AudioTrackController& engine, const std::uint64_t trackId,
                             const std::uint64_t id)
{
    if (const auto* clips = engine.midiClipsView(trackId))
        for (const auto& clip : *clips)
            if (clip.id == id)
                return &clip;
    return nullptr;
}

ImU32 darker(const ImU32 colour, const float amount)
{
    return theme::mix(colour, theme::palette().bg0 | 0xff000000u, amount);
}

bool readableOnColour(const ImU32 colour)
{
    const auto r = static_cast<float>(colour & 0xffu);
    const auto g = static_cast<float>((colour >> 8) & 0xffu);
    const auto b = static_cast<float>((colour >> 16) & 0xffu);
    return 0.299f * r + 0.587f * g + 0.114f * b > 150.0f;
}

} // namespace

ArrangementView::ArrangementView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host)
    : studio_(studio), state_(state), waveforms_(waveforms), host_(host)
{
}

double ArrangementView::secondsPerBeat() const
{
    return 60.0 / std::max(1.0, studio_.engine().tempo());
}

double ArrangementView::snap(const double seconds, const bool force) const
{
    if ((state_.snapBeats <= 0.0 || !ui::snapping(state_.arrangementSnap)) && !force)
        return std::max(0.0, seconds);
    const auto step = std::max(0.25, state_.snapBeats > 0.0 ? state_.snapBeats : 1.0) * secondsPerBeat();
    return std::max(0.0, std::round(seconds / step) * step);
}

float ArrangementView::timeToX(const double seconds) const
{
    return lanesMin_.x + static_cast<float>((seconds - state_.scrollSeconds) * state_.pixelsPerSecond);
}

double ArrangementView::xToTime(const float x) const
{
    return state_.scrollSeconds + static_cast<double>(x - lanesMin_.x) / state_.pixelsPerSecond;
}

float ArrangementView::laneDisplayHeight(const TrackRef& track) const
{
    // A shown automation lane needs room for its curve, whatever height the
    // track was saved at.
    const auto minimum = studio_.automationVisible(track) ? dp(140.0f) : dp(34.0f);
    return std::max(minimum, dp(static_cast<float>(studio_.laneHeight(track)) * kLaneScale) * state_.laneZoom);
}

double ArrangementView::contentEndSeconds() const
{
    auto end = 0.0;
    auto& engine = studio_.engine();
    for (std::size_t index = 0; index < engine.trackCount(); ++index)
        if (const auto* clips = engine.audioClipsView(index))
            for (const auto& clip : *clips)
                end = std::max(end, clip.startSeconds + clip.durationSeconds);
    for (const auto& summary : engine.midiTrackSummaries())
        if (const auto* clips = engine.midiClipsView(summary.id))
            for (const auto& clip : *clips)
                if (clip.launcherScene < 0)
                    end = std::max(end, (clip.startBeats + clip.lengthBeats) * secondsPerBeat());
    return end;
}

const ArrangementView::LaneHit* ArrangementView::laneAt(const std::vector<LaneHit>& lanes, const float y) const
{
    for (const auto& lane : lanes)
        if (y >= lane.top && y < lane.bottom)
            return &lane;
    return nullptr;
}

void ArrangementView::zoom(const float factor, const std::optional<float> anchorX)
{
    const auto anchor = anchorX.value_or((lanesMin_.x + lanesMax_.x) * 0.5f);
    const auto anchorTime = xToTime(anchor);
    state_.pixelsPerSecond = std::clamp(state_.pixelsPerSecond * factor, 2.0, 2'000.0);
    state_.scrollSeconds = std::max(0.0, anchorTime - static_cast<double>(anchor - lanesMin_.x) / state_.pixelsPerSecond);
}

void ArrangementView::zoomToFit()
{
    const auto end = std::max(contentEndSeconds(), 8.0 * secondsPerBeat());
    const auto width = std::max(100.0f, lanesMax_.x - lanesMin_.x);
    state_.pixelsPerSecond = std::clamp(static_cast<double>(width) * 0.95 / end, 2.0, 2'000.0);
    state_.scrollSeconds = 0.0;
}

// ---- Drawing ------------------------------------------------------------------------

void ArrangementView::draw()
{
    // The menus open from inside the canvas child but live at window level.
    clipPopupId_ = ImGui::GetID("##clipContext");
    trackPopupId_ = ImGui::GetID("##trackContext");
    laneChooserId_ = ImGui::GetID("##laneChooser");
    drawToolbar();
    drawCurveBar();
    drawCanvas();
    clipContextMenu();
    trackContextMenu();
    // A lane's menu asked for its chooser.
    if (const auto track = std::exchange(chooseLaneFor_, std::nullopt)) {
        laneChooserTrack_ = *track;
        ImGui::OpenPopup(laneChooserId_);
    }
    laneChooserMenu();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        handleKeys();
}

void ArrangementView::drawToolbar()
{
    const auto& p = theme::palette();
    // A track added: instrument, audio, bus, or an instrument of one's choice.
    add_track::button(studio_, state_, "arrangementAdd", ImVec2(ImGui::GetFrameHeight() * 1.4f, ImGui::GetFrameHeight()));
    ImGui::SameLine(0.0f, dp(4.0f));
    ImGui::TextDisabled("%s", L("Piste", "Track"));

    ui::toolbarSeparator();
    (void) ui::snapToggle("arrangementSnap", &state_.arrangementSnap);
    ImGui::SameLine(0.0f, dp(4.0f));
    ImGui::TextDisabled("%s", L("Grille", "Grid"));
    ImGui::SameLine();
    static constexpr double snaps[] = { 0.0, 4.0, 2.0, 1.0, 0.5, 0.25 };
    const char* snapNames[] = { L("Libre", "Off"), L("Mesure", "Bar"), "1/2", "1/4", "1/8", "1/16" };
    auto current = 3;
    for (auto index = 0; index < 6; ++index)
        if (std::abs(state_.snapBeats - snaps[index]) < 1.0e-6)
            current = index;
    ImGui::SetNextItemWidth(dp(82.0f));
    if (ImGui::Combo("##snap", &current, snapNames, 6))
        state_.snapBeats = snaps[current];
    ui::tooltip(L("Aimantation des clips (Alt pour la désactiver pendant un geste)",
                  "Clip snapping (hold Alt to bypass during a gesture)"));

    ui::toolbarSeparator();
    if (ui::iconButton(icon::zoomOut, L("Dézoomer (Ctrl+molette)", "Zoom out (Ctrl+wheel)")))
        zoom(1.0f / 1.4f);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::zoomIn, L("Zoomer (Ctrl+molette)", "Zoom in (Ctrl+wheel)")))
        zoom(1.4f);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::fullScreen, L("Tout voir", "Zoom to fit")))
        zoomToFit();
    ImGui::SameLine(0.0f, dp(2.0f));
    const auto paused = state_.followPlayhead && followPaused_;
    if (ui::iconButton(icon::forward,
                       paused ? L("Suivi en pause (vous vous êtes déplacé pendant la lecture) : clic pour le reprendre",
                                  "Following paused (you moved during playback): click to resume it")
                              : L("Suivre la lecture", "Follow playback"),
                       state_.followPlayhead && !followPaused_)) {
        if (paused)
            followPaused_ = false;
        else
            state_.followPlayhead = !state_.followPlayhead;
    }

    ui::toolbarSeparator();
    const auto selected = state_.selectedClips.size();
    ImGui::BeginDisabled(selected == 0);
    if (ui::iconButton(icon::cut, L("Scinder à la tête de lecture (S)", "Split at the playhead (S)")))
        splitSelectionAtPlayhead();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::copy, L("Dupliquer (Ctrl+D)", "Duplicate (Ctrl+D)")))
        duplicateSelection();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::trash, L("Supprimer (Suppr)", "Delete (Del)")))
        deleteSelection();
    ImGui::EndDisabled();
    if (selected > 0) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.textFaint), "%d %s", static_cast<int>(selected),
                           selected > 1 ? L("clips", "clips") : L("clip", "clip"));
    }
}

void ArrangementView::drawCanvas()
{
    const auto& p = theme::palette();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg0);
    const auto open = ImGui::BeginChild("##arrangementCanvas", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return;
    }
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto size = ImGui::GetContentRegionAvail();
    headerWidth_ = dp(188.0f);
    // The ruler: the band to grab on top, the times under it.
    const auto navigatorHeight = dp(14.0f);
    const auto rulerHeight = navigatorHeight + dp(22.0f);
    const auto scrollbarHeight = dp(12.0f);
    const auto scrollbarWidth = dp(12.0f);
    // Between the headers and the lanes, the band to grab for the tracks'
    // height (as the ruler's is for time); the scrollbar at the right.
    const auto rowsBandWidth = dp(16.0f);
    const auto headersRight = origin.x + headerWidth_;
    lanesMin_ = ImVec2(headersRight + rowsBandWidth, origin.y + rulerHeight);
    lanesMax_ = ImVec2(origin.x + size.x - scrollbarWidth, origin.y + size.y - scrollbarHeight);
    auto& engine = studio_.engine();

    // Follow the playhead while playing, unless the person moved away
    // during this playback.
    if (!engine.isPlaying())
        followPaused_ = false;
    if (state_.followPlayhead && !followPaused_ && engine.isPlaying() && drag_ == Drag::none) {
        const auto playX = timeToX(engine.position());
        const auto width = lanesMax_.x - lanesMin_.x;
        if (playX > lanesMin_.x + width * 0.88f || playX < lanesMin_.x)
            state_.scrollSeconds = std::max(0.0, engine.position() - width * 0.1 / state_.pixelsPerSecond);
    }
    const auto scrollBefore = state_.scrollSeconds;
    const auto zoomBefore = state_.pixelsPerSecond;

    // Lanes and their content height.
    const auto tracks = studio_.tracks();
    std::vector<LaneHit> lanes;
    auto y = lanesMin_.y - state_.scrollY;
    for (const auto& track : tracks) {
        const auto height = laneDisplayHeight(track);
        const auto automation = studio_.automationVisible(track);
        // The automation band grows with the track (drag its bottom edge),
        // its clips keeping a row above it.
        const auto band = automation ? std::clamp(height * 0.55f, dp(72.0f), std::max(dp(72.0f), height - dp(40.0f))) : 0.0f;
        LaneHit hit { track, y, y + height, y + height - band, automation, {}, {} };
        if (automation) {
            hit.scale = laneScale(track, hit.lane);
            // A lane whose parameter went with its device: back to the volume.
            if (hit.lane.isEmpty() && studio_.shownLane(track).isNotEmpty())
                studio_.setShownLane(track, {});
        }
        lanes.push_back(std::move(hit));
        y += height;
    }
    const auto contentHeight = y + state_.scrollY - lanesMin_.y + dp(60.0f);
    const auto visibleHeight = lanesMax_.y - lanesMin_.y;
    state_.scrollY = std::clamp(state_.scrollY, 0.0f, std::max(0.0f, contentHeight - visibleHeight));

    // Backgrounds.
    list->AddRectFilled(ImVec2(origin.x, lanesMin_.y), ImVec2(headersRight, lanesMax_.y), p.bg1);
    list->PushClipRect(lanesMin_, lanesMax_, true);
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        const auto& lane = lanes[index];
        if (lane.bottom < lanesMin_.y || lane.top > lanesMax_.y)
            continue;
        const auto selected = state_.selectedTrack && *state_.selectedTrack == lane.track;
        list->AddRectFilled(ImVec2(lanesMin_.x, lane.top), ImVec2(lanesMax_.x, lane.bottom),
                            selected ? theme::mix(p.bg0, p.bg2, 0.8f) : (index % 2 == 0 ? p.bg0 : theme::mix(p.bg0, p.bg1, 0.5f)));
        list->AddLine(ImVec2(lanesMin_.x, lane.bottom - 0.5f), ImVec2(lanesMax_.x, lane.bottom - 0.5f), p.grid);
    }
    drawGrid(list, lanesMin_, lanesMax_);

    // Loop region over the lanes.
    if (studio_.loopEnabled() && engine.hasExplicitLoopRange()) {
        const auto x0 = timeToX(engine.loopStart());
        const auto x1 = timeToX(engine.loopEnd());
        list->AddRectFilled(ImVec2(x0, lanesMin_.y), ImVec2(x1, lanesMax_.y), theme::alpha(p.accent, 0.045f));
    }

    // Clips.
    std::vector<ClipHit> hits;
    const auto spb = secondsPerBeat();
    for (const auto& lane : lanes) {
        if (lane.bottom < lanesMin_.y || lane.top > lanesMax_.y)
            continue;
        const auto top = lane.top + dp(1.0f);
        const auto bottom = lane.clipBottom - dp(1.0f);
        if (!lane.track.midi) {
            if (const auto* clips = engine.audioClipsView(lane.track.audioIndex))
                for (const auto& clip : *clips) {
                    const auto x0 = timeToX(clip.startSeconds);
                    const auto x1 = timeToX(clip.startSeconds + clip.durationSeconds);
                    if (x1 < lanesMin_.x || x0 > lanesMax_.x)
                        continue;
                    hits.push_back({ { lane.track, clip.id }, ImVec2(x0, top), ImVec2(std::max(x0 + 3.0f, x1), bottom),
                                     clip.startSeconds, clip.durationSeconds, false });
                }
        } else if (const auto* clips = engine.midiClipsView(lane.track.midiId)) {
            for (const auto& clip : *clips) {
                // The launcher's clips live in its slots, not here.
                if (clip.launcherScene >= 0)
                    continue;
                const auto start = clip.startBeats * spb;
                const auto duration = clip.lengthBeats * spb;
                const auto x0 = timeToX(start);
                const auto x1 = timeToX(start + duration);
                if (x1 < lanesMin_.x || x0 > lanesMax_.x)
                    continue;
                hits.push_back({ { lane.track, clip.id }, ImVec2(x0, top), ImVec2(std::max(x0 + 3.0f, x1), bottom), start,
                                 duration, true });
            }
        }
    }

    // Input before drawing the clips, so a drag shows where it lands now.
    ImGui::SetCursorScreenPos(lanesMin_);
    ImGui::InvisibleButton("##lanes", ImVec2(std::max(1.0f, lanesMax_.x - lanesMin_.x), std::max(1.0f, lanesMax_.y - lanesMin_.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    canvasHovered_ = ImGui::IsItemHovered();
    // A sample dragged from the browser.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload)) {
            const auto path = juce::String::fromUTF8(static_cast<const char*>(payload->Data));
            dropFiles(juce::StringArray(path), ImGui::GetIO().MousePos);
        }
        ImGui::EndDragDropTarget();
    }
    const auto automationTook = handleAutomationInput(lanes);
    handleLaneInput(hits, lanes, automationTook);

    // Recompute the rectangles after the edits of this frame.
    const auto mouse = ImGui::GetIO().MousePos;
    for (auto& hit : hits) {
        if (!hit.midi) {
            if (const auto* clip = findAudioClip(engine, hit.ref.track.audioIndex, hit.ref.clipId)) {
                hit.startSeconds = clip->startSeconds;
                hit.durationSeconds = clip->durationSeconds;
            }
        } else if (const auto* clip = findMidiClip(engine, hit.ref.track.midiId, hit.ref.clipId)) {
            hit.startSeconds = clip->startBeats * spb;
            hit.durationSeconds = clip->lengthBeats * spb;
        }
        hit.min.x = timeToX(hit.startSeconds);
        hit.max.x = std::max(hit.min.x + 3.0f, timeToX(hit.startSeconds + hit.durationSeconds));
    }
    for (const auto& hit : hits) {
        const auto selected = state_.isSelected(hit.ref);
        const auto hovered = drag_ == Drag::none && ImGui::IsItemHovered() && mouse.x >= hit.min.x && mouse.x < hit.max.x
                             && mouse.y >= hit.min.y && mouse.y < hit.max.y;
        if (!hit.midi) {
            if (const auto* clip = findAudioClip(engine, hit.ref.track.audioIndex, hit.ref.clipId))
                drawAudioClip(list, hit, *clip, selected, hovered);
        } else if (const auto* clip = findMidiClip(engine, hit.ref.track.midiId, hit.ref.clipId)) {
            drawMidiClip(list, hit, *clip, selected, hovered);
        }
    }

    for (const auto& lane : lanes)
        if (lane.automation && lane.bottom >= lanesMin_.y && lane.top <= lanesMax_.y)
            drawAutomation(list, lane);

    // Moving between lanes: where the clips will land.
    if (drag_ == Drag::move && dragLaneTrack_ && !dragOrigins_.empty()
        && !(*dragLaneTrack_ == dragOrigins_.front().ref.track))
        if (const auto* lane = laneAt(lanes, mouse.y)) {
            for (const auto& origin : dragOrigins_) {
                const auto* hit = std::find_if(hits.data(), hits.data() + hits.size(),
                                               [&origin](const ClipHit& h) { return h.ref == origin.ref; });
                if (hit == hits.data() + hits.size())
                    continue;
                list->AddRect(ImVec2(hit->min.x, lane->top + dp(2.0f)), ImVec2(hit->max.x, lane->bottom - dp(2.0f)),
                              p.accent, dp(3.0f), dp(1.5f));
            }
        }

    // Box selection.
    if (drag_ == Drag::box) {
        const ImVec2 a(std::min(dragStartMouse_.x, mouse.x), std::min(dragStartMouse_.y, mouse.y));
        const ImVec2 b(std::max(dragStartMouse_.x, mouse.x), std::max(dragStartMouse_.y, mouse.y));
        list->AddRectFilled(a, b, theme::alpha(p.accent, 0.08f));
        list->AddRect(a, b, theme::alpha(p.accent, 0.7f));
    }

    // Playhead.
    const auto playX = timeToX(engine.position());
    if (playX >= lanesMin_.x && playX <= lanesMax_.x)
        list->AddLine(ImVec2(playX, lanesMin_.y), ImVec2(playX, lanesMax_.y),
                      engine.isPlaying() ? p.play : p.accent, dp(1.0f));
    list->PopClipRect();

    // Headers, scrolled with the lanes.
    ImGui::PushClipRect(ImVec2(origin.x, lanesMin_.y), ImVec2(headersRight, lanesMax_.y), true);
    for (std::size_t index = 0; index < lanes.size(); ++index) {
        const auto& lane = lanes[index];
        if (lane.bottom < lanesMin_.y || lane.top > lanesMax_.y)
            continue;
        drawHeader(lane.track, ImVec2(origin.x, lane.top), ImVec2(headersRight, lane.bottom), static_cast<int>(index));
    }
    ImGui::PopClipRect();
    list->AddLine(ImVec2(lanesMin_.x - 0.5f, lanesMin_.y), ImVec2(lanesMin_.x - 0.5f, lanesMax_.y), p.lineStrong);
    // The wheel over the tracks' headers scrolls them up and down too (a
    // knob there keeps it).
    if (auto& io = ImGui::GetIO(); io.MouseWheel != 0.0f && !io.KeyCtrl && !io.KeyShift && ImGui::IsWindowHovered()
                                   && !ui::wheelTaken() && io.MousePos.x >= origin.x && io.MousePos.x < headersRight
                                   && io.MousePos.y >= lanesMin_.y && io.MousePos.y < lanesMax_.y)
        state_.scrollY -= io.MouseWheel * dp(48.0f);

    // Empty session: what to do first.
    if (tracks.empty()) {
        const ImVec2 centre((lanesMin_.x + lanesMax_.x) * 0.5f, (lanesMin_.y + lanesMax_.y) * 0.45f);
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
        ui::centredText(list, ImVec2(lanesMin_.x, centre.y - dp(26.0f)), ImVec2(lanesMax_.x, centre.y), p.textDim,
                        L("Projet vide", "Empty project"));
        ImGui::PopFont();
        ui::centredText(list, ImVec2(lanesMin_.x, centre.y), ImVec2(lanesMax_.x, centre.y + dp(20.0f)), p.textFaint,
                        L("Glissez des fichiers audio ici, ou ajoutez une piste avec + Audio / + Instrument.",
                          "Drop audio files here, or add a track with + Audio / + Instrument."));
    }

    // Ruler on top: the band to grab, then the times.
    drawNavigator(list, ImVec2(lanesMin_.x, origin.y), ImVec2(lanesMax_.x, origin.y + navigatorHeight));
    drawRuler(list, ImVec2(lanesMin_.x, origin.y + navigatorHeight), ImVec2(lanesMax_.x, lanesMin_.y));
    list->AddRectFilled(origin, ImVec2(headersRight, lanesMin_.y), p.bg1);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.86f);
    char header[64];
    std::snprintf(header, sizeof(header), "%d %s", static_cast<int>(tracks.size()),
                  tracks.size() > 1 ? L("pistes", "tracks") : L("piste", "track"));
    list->AddText(ImVec2(origin.x + dp(8.0f), origin.y + (rulerHeight - ImGui::GetFontSize()) * 0.5f), p.textFaint, header);
    ImGui::PopFont();

    // Horizontal scrollbar.
    {
        const auto end = std::max(contentEndSeconds() + 16.0 * spb, xToTime(lanesMax_.x));
        const auto width = lanesMax_.x - lanesMin_.x;
        const auto visible = static_cast<double>(width) / state_.pixelsPerSecond;
        const ImVec2 barMin(lanesMin_.x, lanesMax_.y + dp(2.0f));
        const ImVec2 barMax(lanesMax_.x, lanesMax_.y + scrollbarHeight - dp(2.0f));
        list->AddRectFilled(barMin, barMax, p.bg1, dp(3.0f));
        const auto thumbStart = barMin.x + static_cast<float>(state_.scrollSeconds / end) * width;
        const auto thumbWidth = std::max(dp(24.0f), static_cast<float>(visible / end) * width);
        ImGui::SetCursorScreenPos(barMin);
        ImGui::InvisibleButton("##hscroll", ImVec2(width, barMax.y - barMin.y));
        if (ImGui::IsItemActive()) {
            state_.scrollSeconds = std::clamp(state_.scrollSeconds + ImGui::GetIO().MouseDelta.x / width * end, 0.0,
                                              std::max(0.0, end - visible * 0.5));
        }
        const auto active = ImGui::IsItemActive() || ImGui::IsItemHovered();
        list->AddRectFilled(ImVec2(thumbStart, barMin.y), ImVec2(std::min(barMax.x, thumbStart + thumbWidth), barMax.y),
                            active ? p.accentDim : theme::mix(p.bg5, p.textDim, 0.35f), dp(3.0f));
    }

    // The band for the tracks' height: up and down move, right (toward
    // them) makes them bigger, left smaller; a double click brings them back.
    if (ui::rowsBand("##rowsBand", ImVec2(headersRight, lanesMin_.y), ImVec2(lanesMin_.x, lanesMax_.y), state_.scrollY, state_.laneZoom,
                     0.35f, 5.0f, true)) {
        state_.laneZoom = 1.0f;
        state_.scrollY = 0.0f;
    }
    if (ui::bandCorner("##bandCorner", ImVec2(headersRight, origin.y), ImVec2(lanesMin_.x, origin.y + navigatorHeight))) {
        zoomToFit();
        state_.laneZoom = 1.0f;
        state_.scrollY = 0.0f;
    }
    list->AddRectFilled(ImVec2(headersRight, origin.y + navigatorHeight), ImVec2(lanesMin_.x, lanesMin_.y), p.bg1);

    // Vertical scrollbar: the tracks, up and down (a click beside the thumb
    // brings it there).
    {
        const ImVec2 barMin(lanesMax_.x + dp(2.0f), lanesMin_.y);
        const ImVec2 barMax(origin.x + size.x - dp(1.0f), lanesMax_.y);
        const auto height = std::max(1.0f, barMax.y - barMin.y);
        const auto maximum = std::max(0.0f, contentHeight - visibleHeight);
        const auto thumbHeight = std::clamp(visibleHeight / std::max(contentHeight, visibleHeight) * height, dp(32.0f), height);
        const auto travel = height - thumbHeight;
        const auto scroll = std::clamp(state_.scrollY, 0.0f, maximum);
        list->AddRectFilled(barMin, barMax, p.bg1, dp(3.0f));
        ImGui::SetCursorScreenPos(barMin);
        ImGui::InvisibleButton("##vscroll", ImVec2(std::max(1.0f, barMax.x - barMin.x), height));
        const auto& io = ImGui::GetIO();
        auto thumbTop = barMin.y + (maximum > 0.0f ? scroll / maximum * travel : 0.0f);
        if (ImGui::IsItemActivated() && maximum > 0.0f && (io.MousePos.y < thumbTop || io.MousePos.y > thumbTop + thumbHeight))
            state_.scrollY = std::clamp((io.MousePos.y - barMin.y - thumbHeight * 0.5f) / std::max(1.0f, travel) * maximum, 0.0f,
                                        maximum);
        else if (ImGui::IsItemActive() && travel > 0.0f)
            state_.scrollY = std::clamp(scroll + io.MouseDelta.y / travel * maximum, 0.0f, maximum);
        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
            state_.scrollY = std::clamp(scroll - io.MouseWheel * dp(48.0f), 0.0f, maximum);
        thumbTop = barMin.y + (maximum > 0.0f ? std::clamp(state_.scrollY, 0.0f, maximum) / maximum * travel : 0.0f);
        const auto active = ImGui::IsItemActive() || ImGui::IsItemHovered();
        list->AddRectFilled(ImVec2(barMin.x, thumbTop), ImVec2(barMax.x, thumbTop + thumbHeight),
                            active ? p.accentDim : theme::mix(p.bg5, p.textDim, 0.35f), dp(3.0f));
    }

    // Moving through the song during playback pauses the following.
    if (engine.isPlaying() && (state_.scrollSeconds != scrollBefore || state_.pixelsPerSecond != zoomBefore))
        followPaused_ = true;

    ImGui::EndChild();
}

void ArrangementView::drawNavigator(ImDrawList* list, const ImVec2 min, const ImVec2 max)
{
    const auto& p = theme::palette();
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##navigator", ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
    const auto hovered = ImGui::IsItemHovered();
    const auto active = ImGui::IsItemActive();
    const auto& io = ImGui::GetIO();
    if (hovered || active)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (ImGui::IsItemActivated()) {
        navigateAnchor_ = xToTime(io.MousePos.x);
        drag_ = Drag::navigate;
    }
    if (active && drag_ == Drag::navigate) {
        // Down zooms in, up zooms out; the time grabbed stays under the
        // pointer, so moving sideways moves through the song.
        if (io.MouseDelta.y != 0.0f)
            state_.pixelsPerSecond = std::clamp(state_.pixelsPerSecond * std::exp(static_cast<double>(io.MouseDelta.y) * 0.012),
                                                2.0, 2'000.0);
        state_.scrollSeconds = std::max(0.0, navigateAnchor_ - static_cast<double>(io.MousePos.x - lanesMin_.x)
                                                                    / state_.pixelsPerSecond);
    }
    if (ImGui::IsItemDeactivated() && drag_ == Drag::navigate)
        drag_ = Drag::none;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        zoomToFit();

    list->AddRectFilled(min, max, active ? p.bg3 : (hovered ? theme::mix(p.bg2, p.bg3, 0.5f) : p.bg2));
    // Its ticks: the beats, lighter than the times' bars under it.
    const auto spb = secondsPerBeat();
    const auto beatPixels = spb * state_.pixelsPerSecond;
    auto step = 1.0;
    while (beatPixels * step < 8.0)
        step *= 2.0;
    const auto firstBeat = std::max(0.0, std::floor(xToTime(min.x) / spb / step) * step);
    for (auto beat = firstBeat;; beat += step) {
        const auto x = timeToX(beat * spb);
        if (x > max.x)
            break;
        const auto bar = std::fmod(beat, studio_.engine().beatsPerBar()) < 1.0e-6;
        list->AddLine(ImVec2(x, bar ? min.y + dp(3.0f) : (min.y + max.y) * 0.5f), ImVec2(x, max.y - dp(1.0f)),
                      theme::alpha(p.textFaint, bar ? 0.55f : 0.25f));
    }
    list->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), p.line);
    if (hovered && !active)
        ui::tooltip(L("Glisser : à gauche et à droite pour se déplacer, vers le bas pour zoomer, vers le haut pour "
                      "dézoomer. Double-clic : tout voir.",
                      "Drag: left and right to move, down to zoom in, up to zoom out. Double click: zoom to fit."));
}

void ArrangementView::drawGrid(ImDrawList* list, const ImVec2 min, const ImVec2 max)
{
    const auto& p = theme::palette();
    const auto spb = secondsPerBeat();
    const auto beatPixels = spb * state_.pixelsPerSecond;
    auto step = 1.0;
    while (beatPixels * step < 10.0)
        step *= 2.0;
    while (beatPixels * step > 80.0 && step > 0.25)
        step *= 0.5;
    const auto firstBeat = std::floor(xToTime(min.x) / spb / step) * step;
    for (auto beat = firstBeat; ; beat += step) {
        const auto x = timeToX(beat * spb);
        if (x > max.x)
            break;
        if (x < min.x)
            continue;
        const auto isBar = std::fmod(beat, studio_.engine().beatsPerBar()) < 1.0e-6;
        list->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), isBar ? p.gridStrong : p.grid);
    }
}

void ArrangementView::drawRuler(ImDrawList* list, const ImVec2 min, const ImVec2 max)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    list->AddRectFilled(min, max, p.bg1);
    list->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), p.lineStrong);
    const auto spb = secondsPerBeat();
    const auto barBeats = engine.beatsPerBar();
    const auto barPixels = barBeats * spb * state_.pixelsPerSecond;
    auto labelEvery = 1;
    while (barPixels * labelEvery < 44.0)
        labelEvery *= 2;
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
    const auto firstBar = std::max(0, static_cast<int>(std::floor(xToTime(min.x) / (barBeats * spb))));
    for (auto bar = firstBar;; ++bar) {
        const auto x = timeToX(bar * barBeats * spb);
        if (x > max.x)
            break;
        const auto labelled = bar % labelEvery == 0;
        list->AddLine(ImVec2(x, labelled ? min.y + (max.y - min.y) * 0.45f : max.y - dp(5.0f)), ImVec2(x, max.y),
                      labelled ? p.textFaint : p.gridStrong);
        if (labelled) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d", bar + 1);
            list->AddText(ImVec2(x + dp(3.0f), min.y + dp(2.0f)), p.textDim, text);
        }
    }
    ImGui::PopFont();

    if (engine.hasExplicitLoopRange()) {
        const auto x0 = timeToX(engine.loopStart());
        const auto x1 = timeToX(engine.loopEnd());
        const auto colour = studio_.loopEnabled() ? p.accent : p.textFaint;
        list->AddRectFilled(ImVec2(x0, min.y + dp(1.0f)), ImVec2(x1, min.y + dp(6.0f)), theme::alpha(colour, 0.85f), dp(2.0f));
    }
    // The playhead's handle.
    const auto playX = timeToX(engine.position());
    if (playX >= min.x - dp(6.0f) && playX <= max.x + dp(6.0f)) {
        const auto colour = engine.isPlaying() ? p.play : p.accent;
        list->AddTriangleFilled(ImVec2(playX - dp(5.0f), max.y - dp(8.0f)), ImVec2(playX + dp(5.0f), max.y - dp(8.0f)),
                                ImVec2(playX, max.y), colour);
    }

    // Click or drag to place the playhead; drag in the top band to draw the
    // loop.
    ImGui::SetCursorScreenPos(min);
    ImGui::InvisibleButton("##ruler", ImVec2(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y)));
    const auto& io = ImGui::GetIO();
    if (ImGui::IsItemActivated()) {
        const auto inLoopBand = io.MousePos.y < min.y + (max.y - min.y) * 0.4f || io.KeyShift;
        drag_ = inLoopBand ? Drag::loop : Drag::seek;
        loopAnchor_ = snap(xToTime(io.MousePos.x), true);
    }
    if (ImGui::IsItemActive()) {
        const auto time = std::max(0.0, xToTime(io.MousePos.x));
        if (drag_ == Drag::seek) {
            engine.setPosition(snap(time));
        } else if (drag_ == Drag::loop) {
            const auto other = snap(time, true);
            if (std::abs(other - loopAnchor_) > 1.0e-3)
                studio_.setLoopRange(loopAnchor_, other);
        }
    }
    if (ImGui::IsItemDeactivated())
        drag_ = Drag::none;
    if (ImGui::IsItemHovered())
        ui::tooltip(L("Clic : placer la tête de lecture. Haut de la règle ou Maj : tracer la boucle.",
                      "Click: place the playhead. Top of the ruler or Shift: draw the loop."));
}

void ArrangementView::drawAudioClip(ImDrawList* list, const ClipHit& hit, const AudioClip& clip, const bool selected,
                                    const bool hovered)
{
    const auto& p = theme::palette();
    const auto colour = theme::fromArgb(clip.colour != 0 ? clip.colour : 0xffff9b2fu);
    const auto rounding = dp(3.0f);
    const auto headerHeight = std::min(dp(14.0f), (hit.max.y - hit.min.y) * 0.35f);
    list->AddRectFilled(hit.min, hit.max, darker(colour, hovered ? 0.62f : 0.70f), rounding);
    list->AddRectFilled(hit.min, ImVec2(hit.max.x, hit.min.y + headerHeight), darker(colour, 0.15f), rounding,
                        ImDrawFlags_RoundCornersTop);

    // Waveform: one column per pixel, the source mapped through the stretch
    // and each warp marker, as the engine reads it.
    const auto bodyTop = hit.min.y + headerHeight + dp(1.0f);
    const auto bodyBottom = hit.max.y - dp(2.0f);
    const auto centre = (bodyTop + bodyBottom) * 0.5f;
    const auto half = (bodyBottom - bodyTop) * 0.5f;
    const auto waveColour = theme::brighter(colour, 0.25f);
    list->PushClipRect(ImVec2(std::max(hit.min.x, lanesMin_.x), hit.min.y), ImVec2(std::min(hit.max.x, lanesMax_.x), hit.max.y), true);
    // `sourceAt`: where a moment of the segment (from its start) plays in
    // the file.
    const auto drawSegment = [&](const juce::File& file, const double timelineStart, const double timelineDuration,
                                 const auto& sourceAt) {
        auto* thumbnail = waveforms_.get(file);
        if (thumbnail == nullptr || timelineDuration <= 0.0)
            return;
        if (!thumbnail->isFullyLoaded())
            host_.wake(2);
        const auto x0 = std::max(lanesMin_.x, timeToX(timelineStart));
        const auto x1 = std::min(lanesMax_.x, timeToX(timelineStart + timelineDuration));
        const auto channels = std::max(1, thumbnail->getNumChannels());
        const auto pixelSeconds = 1.0 / state_.pixelsPerSecond;
        for (auto x = std::floor(x0); x < x1; x += 1.0f) {
            const auto t = xToTime(x) - timelineStart;
            const auto a = sourceAt(t);
            const auto b = sourceAt(std::min(timelineDuration, t + pixelSeconds));
            const auto s0 = std::min(a, b);
            const auto s1 = std::max(std::max(a, b), s0 + 1.0e-5);
            auto low = 0.0f;
            auto high = 0.0f;
            for (auto channel = 0; channel < std::min(2, channels); ++channel) {
                float mn = 0.0f;
                float mx = 0.0f;
                thumbnail->getApproximateMinMax(s0, s1, channel, mn, mx);
                low = std::min(low, mn);
                high = std::max(high, mx);
            }
            if (high - low < 0.002f)
                continue;
            list->AddRectFilled(ImVec2(x, centre - high * half), ImVec2(x + 1.0f, centre - low * half + 1.0f), waveColour);
        }
    };
    const auto sourceDuration = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
    // A moment of the clip in its source: straight when it has no warp
    // marker (no allocation), else through them.
    const auto speed = sourceDuration / std::max(1.0e-6, clip.durationSeconds);
    const auto clipSource = [&clip, speed](const double t) {
        return clip.stretchMarkers.empty() ? clip.sourceOffsetSeconds + std::clamp(t, 0.0, clip.durationSeconds) * speed
                                           : app::AudioTrackController::clipSourceTime(clip, t);
    };
    if (clip.usesEventTimeline && !clip.events.empty()) {
        for (const auto& event : clip.events) {
            const auto eventSpeed = event.sourceDurationSeconds / std::max(1.0e-6, event.durationSeconds);
            drawSegment(event.sourceFile, clip.startSeconds + event.startSeconds, event.durationSeconds, [&event, eventSpeed](const double t) {
                return event.stretchMarkers.empty() ? event.sourceOffsetSeconds + std::clamp(t, 0.0, event.durationSeconds) * eventSpeed
                                                    : app::AudioTrackController::eventSourceTime(event, t);
            });
            // Where one starts, a faint line.
            if (event.startSeconds > 1.0e-6) {
                const auto x = timeToX(clip.startSeconds + event.startSeconds);
                list->AddLine(ImVec2(x, bodyTop), ImVec2(x, bodyBottom), theme::alpha(p.text, 0.3f), 1.0f);
            }
        }
    } else if (!clip.comp.empty() && clip.takes.size() > 1) {
        // Each passage from the take that plays it.
        for (const auto& passage : clip.comp) {
            const auto& take = clip.takes[std::min(passage.take, clip.takes.size() - 1)];
            const auto length = passage.endSeconds - passage.startSeconds;
            drawSegment(take.sourceFile, clip.startSeconds + passage.startSeconds, length, [&](const double t) {
                return clipSource(passage.startSeconds + t) + take.shiftSeconds;
            });
            if (passage.startSeconds > 1.0e-6) {
                const auto x = timeToX(clip.startSeconds + passage.startSeconds);
                list->AddLine(ImVec2(x, bodyTop), ImVec2(x, bodyBottom), theme::alpha(p.text, 0.45f), 1.0f);
            }
        }
    } else {
        drawSegment(clip.sourceFile, clip.startSeconds, clip.durationSeconds, clipSource);
        // Its warp markers, small, on the header's edge.
        for (const auto& marker : clip.stretchMarkers)
            if (marker.timelineSeconds > 0.0 && marker.timelineSeconds < clip.durationSeconds) {
                const auto x = timeToX(clip.startSeconds + marker.timelineSeconds);
                list->AddTriangleFilled(ImVec2(x - dp(3.0f), bodyTop), ImVec2(x + dp(3.0f), bodyTop), ImVec2(x, bodyTop + dp(4.0f)),
                                        theme::alpha(p.text, 0.7f));
            }
    }
    // Fades.
    if (clip.fadeInSeconds > 0.0)
        list->AddLine(ImVec2(hit.min.x, bodyBottom), ImVec2(timeToX(clip.startSeconds + clip.fadeInSeconds), bodyTop),
                      theme::alpha(p.text, 0.6f), 1.0f);
    if (clip.fadeOutSeconds > 0.0)
        list->AddLine(ImVec2(timeToX(clip.startSeconds + clip.durationSeconds - clip.fadeOutSeconds), bodyTop),
                      ImVec2(hit.max.x, bodyBottom), theme::alpha(p.text, 0.6f), 1.0f);
    // Its fades' handles, to drag.
    if (hovered || selected) {
        const auto handle = dp(3.5f);
        const auto inX = timeToX(clip.startSeconds + clip.fadeInSeconds);
        const auto outX = timeToX(clip.startSeconds + clip.durationSeconds - clip.fadeOutSeconds);
        list->AddRectFilled(ImVec2(inX - handle, hit.min.y + headerHeight - handle), ImVec2(inX + handle, hit.min.y + headerHeight + handle),
                            p.text, dp(1.0f));
        list->AddRectFilled(ImVec2(outX - handle, hit.min.y + headerHeight - handle), ImVec2(outX + handle, hit.min.y + headerHeight + handle),
                            p.text, dp(1.0f));
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
    // What it does beyond its sound: takes, transposition, stretch.
    juce::String badges;
    if (clip.takes.size() > 1)
        badges << static_cast<int>(clip.takes.size()) << juce::String::fromUTF8(L(" prises", " takes")) << (clip.comp.empty() ? "" : "*")
               << "  ";
    if (std::abs(clip.pitchSemitones) > 1.0e-6)
        badges << (clip.pitchSemitones > 0 ? "+" : "") << juce::String(clip.pitchSemitones, std::abs(clip.pitchSemitones - std::round(clip.pitchSemitones)) > 1.0e-6 ? 2 : 0)
               << " st  ";
    // A clip of events has no speed of its own (each event has its own).
    if (clip.usesEventTimeline)
        badges << static_cast<int>(clip.events.size()) << juce::String::fromUTF8(L(" sons", " sounds"));
    else if (std::abs(sourceDuration - clip.durationSeconds) > 1.0e-4)
        badges << juce::String(clip.durationSeconds / std::max(1.0e-6, sourceDuration) * 100.0, 0) << " %";
    if (badges.isNotEmpty() && hit.max.x - hit.min.x > dp(90.0f)) {
        const auto width = ImGui::CalcTextSize(badges.toRawUTF8()).x;
        list->AddText(ImVec2(std::min(hit.max.x, lanesMax_.x) - width - dp(4.0f), hit.min.y + (headerHeight - ImGui::GetFontSize()) * 0.5f),
                      readableOnColour(darker(colour, 0.15f)) ? p.bg0 : p.text, badges.toRawUTF8());
    }
    const auto name = clip.name.isNotEmpty() ? clip.name : clip.sourceFile.getFileNameWithoutExtension();
    ui::clippedText(list, ImVec2(std::max(hit.min.x, lanesMin_.x) + dp(4.0f), hit.min.y + (headerHeight - ImGui::GetFontSize()) * 0.5f),
                    hit.max.x - std::max(hit.min.x, lanesMin_.x) - dp(8.0f),
                    readableOnColour(darker(colour, 0.15f)) ? p.bg0 : p.text, name.toRawUTF8());
    ImGui::PopFont();
    list->PopClipRect();
    if (selected)
        list->AddRect(hit.min, hit.max, p.text, rounding, dp(1.5f));
    else if (hovered)
        list->AddRect(hit.min, hit.max, theme::alpha(p.text, 0.35f), rounding);
}

void ArrangementView::drawMidiClip(ImDrawList* list, const ClipHit& hit, const MidiClip& clip, const bool selected,
                                   const bool hovered)
{
    const auto& p = theme::palette();
    const auto colour = theme::fromArgb(clip.colour != 0 ? clip.colour : 0xff4fd3aeu);
    const auto rounding = dp(3.0f);
    const auto headerHeight = std::min(dp(14.0f), (hit.max.y - hit.min.y) * 0.35f);
    list->AddRectFilled(hit.min, hit.max, darker(colour, hovered ? 0.62f : 0.70f), rounding);
    list->AddRectFilled(hit.min, ImVec2(hit.max.x, hit.min.y + headerHeight), darker(colour, 0.15f), rounding,
                        ImDrawFlags_RoundCornersTop);
    list->PushClipRect(ImVec2(std::max(hit.min.x, lanesMin_.x), hit.min.y), ImVec2(std::min(hit.max.x, lanesMax_.x), hit.max.y), true);
    if (!clip.notes.empty()) {
        auto low = 127;
        auto high = 0;
        for (const auto& note : clip.notes) {
            low = std::min(low, note.noteNumber);
            high = std::max(high, note.noteNumber);
        }
        low = std::max(0, low - 2);
        high = std::min(127, high + 2);
        const auto range = std::max(6, high - low + 1);
        const auto bodyTop = hit.min.y + headerHeight + dp(2.0f);
        const auto bodyHeight = hit.max.y - dp(2.0f) - bodyTop;
        const auto noteHeight = std::max(1.5f, bodyHeight / static_cast<float>(range));
        const auto spb = secondsPerBeat();
        const auto clipStart = clip.startBeats * spb;
        const auto loopLength = clip.looping && clip.loopEndBeats > clip.loopStartBeats
                                    ? clip.loopEndBeats - clip.loopStartBeats : 0.0;
        const auto noteColour = theme::brighter(colour, 0.3f);
        const auto drawPass = [&](const double offsetBeats) {
            for (const auto& note : clip.notes) {
                const auto start = note.startBeats + offsetBeats;
                if (start >= clip.lengthBeats)
                    continue;
                const auto end = std::min(clip.lengthBeats, start + note.durationBeats);
                const auto x0 = timeToX(clipStart + start * spb);
                const auto x1 = std::max(x0 + 1.5f, timeToX(clipStart + end * spb));
                const auto yy = bodyTop + static_cast<float>(high - note.noteNumber) / static_cast<float>(range) * bodyHeight;
                list->AddRectFilled(ImVec2(x0, yy), ImVec2(x1, yy + noteHeight), noteColour, 1.0f);
            }
        };
        drawPass(0.0);
        if (loopLength > 0.0)
            for (auto pass = 1; pass < 64 && clip.loopStartBeats + loopLength * pass < clip.lengthBeats; ++pass)
                drawPass(loopLength * pass);
    }
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.8f);
    ui::clippedText(list, ImVec2(std::max(hit.min.x, lanesMin_.x) + dp(4.0f), hit.min.y + (headerHeight - ImGui::GetFontSize()) * 0.5f),
                    hit.max.x - std::max(hit.min.x, lanesMin_.x) - dp(8.0f),
                    readableOnColour(darker(colour, 0.15f)) ? p.bg0 : p.text, clip.name.toRawUTF8());
    ImGui::PopFont();
    list->PopClipRect();
    const auto edited = state_.editedClip && state_.editedClip->clipId == clip.id && state_.editedClip->track.midi;
    if (selected)
        list->AddRect(hit.min, hit.max, p.text, rounding, dp(1.5f));
    else if (hovered || edited)
        list->AddRect(hit.min, hit.max, theme::alpha(edited ? colour : p.text, edited ? 0.9f : 0.35f), rounding);
}

void ArrangementView::drawHeader(const TrackRef& track, const ImVec2 min, const ImVec2 max, const int index)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto selected = state_.selectedTrack && *state_.selectedTrack == track;
    const auto colour = theme::fromArgb(studio_.trackColour(track));
    list->AddRectFilled(min, max, selected ? p.bg3 : p.bg2);
    list->AddRectFilled(min, ImVec2(min.x + dp(4.0f), max.y), colour);
    list->AddLine(ImVec2(min.x, max.y - 0.5f), ImVec2(max.x, max.y - 0.5f), p.line);

    ImGui::PushID(index);
    ImGui::PushID(track.midi ? "m" : "a");
    // Clicking the header selects the track; the controls sit on top.
    ImGui::SetCursorScreenPos(min);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##headerSelect", ImVec2(max.x - min.x, std::max(1.0f, max.y - min.y)));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        state_.selectTrack(track);
        state_.focused = Panel::arrangement;
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        state_.selectTrack(track);
        state_.contextRequest = ContextRequest { ContextRequest::Kind::track, track };
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        renamingTrack_ = track;
        focusRename_ = true;
        std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", studio_.trackName(track).toRawUTF8());
    }

    const auto padding = dp(10.0f);
    const auto name = studio_.trackName(track);
    if (renamingTrack_ && *renamingTrack_ == track) {
        ImGui::SetCursorScreenPos(ImVec2(min.x + padding, min.y + dp(4.0f)));
        ImGui::SetNextItemWidth(max.x - min.x - padding - dp(8.0f));
        if (focusRename_) {
            ImGui::SetKeyboardFocusHere();
            focusRename_ = false;
        }
        if (ImGui::InputText("##rename", renameBuffer_, sizeof(renameBuffer_),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            studio_.setTrackName(track, juce::String::fromUTF8(renameBuffer_));
            renamingTrack_.reset();
        } else if (ImGui::IsItemDeactivated()) {
            renamingTrack_.reset();
        }
    } else {
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ui::clippedText(list, ImVec2(min.x + padding, min.y + dp(5.0f)), max.x - min.x - padding - dp(30.0f), p.text,
                        name.toRawUTF8());
        ImGui::PopFont();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.78f);
        list->AddText(ImVec2(max.x - dp(30.0f), min.y + dp(7.0f)), track.midi ? p.midi : p.audio,
                      track.midi ? "MIDI" : "AUDIO");
        ImGui::PopFont();
    }

    // Mute, solo, arm and a level, when the lane is tall enough.
    if (max.y - min.y >= dp(40.0f)) {
        auto mix = studio_.trackMix(track);
        const auto size = ImVec2(dp(20.0f), dp(17.0f));
        ImGui::SetCursorScreenPos(ImVec2(min.x + padding, max.y - size.y - dp(6.0f)));
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 0.78f);
        auto changed = false;
        {
            const auto [key, name] = control_targets::forTrackLearn(studio_, track, "mute", ui::Ls("Muet", "Mute"));
            const ui::LearnScope learn(key, name);
            changed |= ui::toggle("M", &mix.muted, p.mute, size, L("Muet", "Mute"));
        }
        ImGui::SameLine(0.0f, dp(3.0f));
        {
            const auto [key, name] = control_targets::forTrackLearn(studio_, track, "solo", "Solo");
            const ui::LearnScope learn(key, name);
            changed |= ui::toggle("S", &mix.solo, p.solo, size, L("Solo", "Solo"));
        }
        ImGui::SameLine(0.0f, dp(3.0f));
        {
            const auto [key, name] = control_targets::forTrackLearn(studio_, track, "arm", ui::Ls("Armer", "Arm"));
            const ui::LearnScope learn(key, name);
            changed |= ui::toggle("R", &mix.armed, p.record, size,
                                  track.midi ? L("Armer : le clavier joue cette piste", "Arm: the keyboard plays this track")
                                             : L("Armer pour l'enregistrement", "Arm for recording"));
        }
        ImGui::SameLine(0.0f, dp(3.0f));
        auto automation = studio_.automationVisible(track);
        if (ui::toggle("A", &automation, p.control, size,
                       L("Automation : le volume ou un paramètre d'un périphérique", "Automation: the volume or a device's parameter")))
            studio_.setAutomationVisible(track, automation);
        ImGui::PopFont();
        if (changed)
            studio_.setTrackMix(track, mix);
        ImGui::SameLine(0.0f, dp(6.0f));
        const auto meterState = studio_.trackMeter(track);
        const auto meterPos = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(meterPos.x, meterPos.y + size.y * 0.25f));
        ui::meter("##laneMeter", ImVec2(max.x - meterPos.x - dp(8.0f), size.y * 0.5f), meterState.peak[0], meterState.peak[1],
                  meterState.rms[0], meterState.rms[1], false);
    }

    // The bottom edge resizes the lane.
    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y - dp(3.0f)));
    ImGui::InvisibleButton("##laneResize", ImVec2(max.x - min.x, dp(5.0f)));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActivated()) {
        resizeTrack_ = track;
        resizeStartHeight_ = laneDisplayHeight(track);
    }
    if (ImGui::IsItemActive()) {
        const auto height = resizeStartHeight_ + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f).y;
        studio_.setLaneHeight(track, static_cast<int>(height / ImGui::GetStyle().FontScaleDpi / kLaneScale
                                                      / std::max(0.1f, state_.laneZoom)));
    }
    ImGui::PopID();
    ImGui::PopID();
}

// ---- Input -------------------------------------------------------------------------------------

void ArrangementView::handleLaneInput(const std::vector<ClipHit>& clips, const std::vector<LaneHit>& lanes,
                                      const bool clicksTaken)
{
    auto& io = ImGui::GetIO();
    const auto hovered = ImGui::IsItemHovered();
    const auto clickable = hovered && !clicksTaken;
    const auto mouse = io.MousePos;
    const auto edge = dp(6.0f);

    const ClipHit* under = nullptr;
    for (auto it = clips.rbegin(); it != clips.rend(); ++it)
        if (mouse.x >= it->min.x && mouse.x < it->max.x && mouse.y >= it->min.y && mouse.y < it->max.y) {
            under = &*it;
            break;
        }

    // Wheel: scroll, Shift sideways, Ctrl zooms around the pointer.
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl)
            zoom(io.MouseWheel > 0.0f ? 1.18f : 1.0f / 1.18f, mouse.x);
        else if (io.KeyShift)
            state_.scrollSeconds = std::max(0.0, state_.scrollSeconds - io.MouseWheel * 80.0 / state_.pixelsPerSecond);
        else
            state_.scrollY -= io.MouseWheel * dp(48.0f);
    }
    if (hovered && io.MouseWheelH != 0.0f)
        state_.scrollSeconds = std::max(0.0, state_.scrollSeconds - io.MouseWheelH * 80.0 / state_.pixelsPerSecond);
    // Middle drag pans.
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        state_.scrollSeconds = std::max(0.0, state_.scrollSeconds - io.MouseDelta.x / state_.pixelsPerSecond);
        state_.scrollY -= io.MouseDelta.y;
    }

    // An audio clip's fade handle under the pointer: at the top, where its
    // fade in ends or its fade out starts.
    const auto fadeUnder = [&]() -> Drag {
        if (under == nullptr || under->ref.track.midi)
            return Drag::none;
        const auto* clip = findAudioClip(studio_.engine(), under->ref.track.audioIndex, under->ref.clipId);
        if (clip == nullptr)
            return Drag::none;
        const auto headerHeight = std::min(dp(14.0f), (under->max.y - under->min.y) * 0.35f);
        if (std::abs(mouse.y - (under->min.y + headerHeight)) > dp(6.0f))
            return Drag::none;
        if (std::abs(mouse.x - timeToX(clip->startSeconds + clip->fadeInSeconds)) < dp(6.0f))
            return Drag::fadeIn;
        if (std::abs(mouse.x - timeToX(clip->startSeconds + clip->durationSeconds - clip->fadeOutSeconds)) < dp(6.0f))
            return Drag::fadeOut;
        return Drag::none;
    }();
    // Cursor feedback.
    if (drag_ == Drag::none && clickable && under != nullptr) {
        if (fadeUnder != Drag::none)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        else if (mouse.x - under->min.x < edge || under->max.x - mouse.x < edge)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (drag_ == Drag::trimLeft || drag_ == Drag::trimRight) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    } else if (drag_ == Drag::fadeIn || drag_ == Drag::fadeOut) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    if (clickable && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state_.focused = Panel::arrangement;
        const auto* lane = laneAt(lanes, mouse.y);
        if (lane != nullptr)
            state_.selectTrack(lane->track);
        if (under != nullptr) {
            if (io.KeyCtrl) {
                const auto found = std::find(state_.selectedClips.begin(), state_.selectedClips.end(), under->ref);
                if (found != state_.selectedClips.end())
                    state_.selectedClips.erase(found);
                else
                    state_.selectedClips.push_back(under->ref);
            } else if (!state_.isSelected(under->ref)) {
                state_.selectedClips = { under->ref };
            }
            if (under->midi)
                state_.editedClip = under->ref;
            const auto mode = fadeUnder != Drag::none        ? fadeUnder
                              : mouse.x - under->min.x < edge ? Drag::trimLeft
                              : under->max.x - mouse.x < edge ? Drag::trimRight
                                                              : Drag::move;
            if (mode != Drag::move)
                state_.selectedClips = { under->ref };
            beginDrag(mode, clips);
        } else {
            if (!io.KeyCtrl)
                state_.selectedClips.clear();
            beginDrag(Drag::box, clips);
        }
    }
    if (clickable && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (under != nullptr) {
            openInEditor(under->ref);
        } else if (const auto* lane = laneAt(lanes, mouse.y); lane != nullptr && mouse.y < lane->clipBottom) {
            if (lane->track.midi)
                createMidiClipAt(lane->track, xToTime(mouse.x));
            else if (studio_.isBusTrack(lane->track))
                createAutomationClipAt(lane->track, xToTime(mouse.x));
        }
    }
    if (clickable && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (under != nullptr) {
            if (!state_.isSelected(under->ref))
                state_.selectedClips = { under->ref };
            contextClip_ = under->ref;
            ImGui::OpenPopup(clipPopupId_);
        } else if (const auto* lane = laneAt(lanes, mouse.y)) {
            state_.selectTrack(lane->track);
            contextTrack_ = lane->track;
            ImGui::OpenPopup(trackPopupId_);
        }
    }

    if (drag_ != Drag::none && drag_ != Drag::seek && drag_ != Drag::loop && drag_ != Drag::navigate) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            applyDrag(lanes);
        else
            endDrag(lanes);
    }

    // Box selection follows the pointer.
    if (drag_ == Drag::box) {
        const ImVec2 a(std::min(dragStartMouse_.x, mouse.x), std::min(dragStartMouse_.y, mouse.y));
        const ImVec2 b(std::max(dragStartMouse_.x, mouse.x), std::max(dragStartMouse_.y, mouse.y));
        if (b.x - a.x > 3.0f || b.y - a.y > 3.0f) {
            std::vector<ClipRef> inside;
            for (const auto& clip : clips)
                if (clip.max.x >= a.x && clip.min.x <= b.x && clip.max.y >= a.y && clip.min.y <= b.y)
                    inside.push_back(clip.ref);
            state_.selectedClips = inside;
        }
    }
}

void ArrangementView::beginDrag(const Drag drag, const std::vector<ClipHit>&)
{
    drag_ = drag;
    dragStartMouse_ = ImGui::GetIO().MousePos;
    dragStartTime_ = xToTime(dragStartMouse_.x);
    dragOrigins_.clear();
    dragLaneTrack_.reset();
    if (drag == Drag::box)
        return;
    auto& engine = studio_.engine();
    const auto spb = secondsPerBeat();
    for (const auto& ref : state_.selectedClips) {
        Origin origin;
        origin.ref = ref;
        if (!ref.track.midi) {
            const auto* clip = findAudioClip(engine, ref.track.audioIndex, ref.clipId);
            if (clip == nullptr)
                continue;
            origin.start = clip->startSeconds;
            origin.duration = clip->durationSeconds;
            origin.offset = clip->sourceOffsetSeconds;
            origin.sourceDuration = clip->sourceDurationSeconds > 0.0 ? clip->sourceDurationSeconds : clip->durationSeconds;
            origin.fadeIn = clip->fadeInSeconds;
            origin.fadeOut = clip->fadeOutSeconds;
        } else {
            const auto* clip = findMidiClip(engine, ref.track.midiId, ref.clipId);
            if (clip == nullptr)
                continue;
            origin.start = clip->startBeats * spb;
            origin.duration = clip->lengthBeats * spb;
            origin.lengthBeats = clip->lengthBeats;
        }
        dragOrigins_.push_back(origin);
    }
    if (dragOrigins_.empty()) {
        drag_ = Drag::none;
        return;
    }
    automationLastEdit_ = false;
    engine.beginArrangementEditBatch();
}

void ArrangementView::applyDrag(const std::vector<LaneHit>& lanes)
{
    auto& engine = studio_.engine();
    const auto& io = ImGui::GetIO();
    const auto spb = secondsPerBeat();
    if (drag_ == Drag::box || dragOrigins_.empty())
        return;
    const auto& lead = dragOrigins_.front();
    const auto mouseTime = xToTime(io.MousePos.x);
    if (drag_ == Drag::move) {
        if (std::abs(io.MousePos.x - dragStartMouse_.x) < 3.0f && std::abs(io.MousePos.y - dragStartMouse_.y) < 3.0f)
            return;
        auto delta = snap(lead.start + (mouseTime - dragStartTime_)) - lead.start;
        for (const auto& origin : dragOrigins_)
            delta = std::max(delta, -origin.start);
        for (const auto& origin : dragOrigins_) {
            if (!origin.ref.track.midi)
                (void) engine.moveAudioClip(origin.ref.track.audioIndex, origin.ref.clipId, origin.start + delta);
            else
                (void) engine.setMidiClipStartBeats(origin.ref.track.midiId, origin.ref.clipId, (origin.start + delta) / spb);
        }
        if (const auto* lane = laneAt(lanes, io.MousePos.y); lane != nullptr && lane->track.midi == lead.ref.track.midi)
            dragLaneTrack_ = lane->track;
        studio_.markDirty();
    } else if (drag_ == Drag::trimRight) {
        const auto end = std::max(lead.start + kMinimumClipSeconds, snap(lead.start + lead.duration + (mouseTime - dragStartTime_)));
        const auto duration = end - lead.start;
        if (!lead.ref.track.midi) {
            const auto ratio = lead.duration > 0.0 ? lead.sourceDuration / lead.duration : 1.0;
            (void) engine.updateAudioClip(lead.ref.track.audioIndex, lead.ref.clipId, lead.start, lead.offset, duration,
                                          -1.0, -1.0, duration * ratio);
        } else if (const auto* clip = findMidiClip(engine, lead.ref.track.midiId, lead.ref.clipId)) {
            const auto beats = std::max(0.25, duration / spb);
            const auto loopEnd = clip->loopEndBeats > 0.0 ? std::min(clip->loopEndBeats, beats) : beats;
            (void) engine.setMidiClipTimelineBounds(lead.ref.track.midiId, lead.ref.clipId, beats,
                                                    std::min(clip->playStartBeats, beats - 0.25), beats,
                                                    std::min(clip->loopStartBeats, loopEnd - 0.25), loopEnd, clip->looping);
        }
        studio_.markDirty();
    } else if ((drag_ == Drag::fadeIn || drag_ == Drag::fadeOut) && !lead.ref.track.midi) {
        // A fade as long as dragged (not past the other one).
        const auto inside = std::clamp(mouseTime - lead.start, 0.0, lead.duration);
        const auto fadeIn = drag_ == Drag::fadeIn ? std::min(inside, lead.duration - lead.fadeOut) : lead.fadeIn;
        const auto fadeOut = drag_ == Drag::fadeOut ? std::min(lead.duration - inside, lead.duration - lead.fadeIn) : lead.fadeOut;
        (void) engine.updateAudioClip(lead.ref.track.audioIndex, lead.ref.clipId, lead.start, lead.offset, lead.duration, fadeIn, fadeOut,
                                      lead.sourceDuration);
        studio_.markDirty();
    } else if (drag_ == Drag::trimLeft && !lead.ref.track.midi) {
        const auto ratio = lead.duration > 0.0 ? lead.sourceDuration / lead.duration : 1.0;
        auto start = snap(lead.start + (mouseTime - dragStartTime_));
        start = std::clamp(start, std::max(0.0, lead.start - lead.offset / ratio), lead.start + lead.duration - kMinimumClipSeconds);
        const auto delta = start - lead.start;
        const auto duration = lead.duration - delta;
        (void) engine.updateAudioClip(lead.ref.track.audioIndex, lead.ref.clipId, start, lead.offset + delta * ratio, duration,
                                      -1.0, -1.0, duration * ratio);
        studio_.markDirty();
    }
}

void ArrangementView::endDrag(const std::vector<LaneHit>&)
{
    auto& engine = studio_.engine();
    if (drag_ == Drag::move && dragLaneTrack_ && !dragOrigins_.empty()
        && !(*dragLaneTrack_ == dragOrigins_.front().ref.track) && !dragLaneTrack_->midi) {
        // Audio clips moved to another audio track.
        const auto from = static_cast<int>(dragOrigins_.front().ref.track.audioIndex);
        const auto to = static_cast<int>(dragLaneTrack_->audioIndex);
        std::vector<std::vector<std::uint64_t>> byTrack(engine.trackCount());
        for (const auto& origin : dragOrigins_)
            if (!origin.ref.track.midi && origin.ref.track.audioIndex < byTrack.size())
                byTrack[origin.ref.track.audioIndex].push_back(origin.ref.clipId);
        const auto moved = engine.moveAudioClipsAcrossTracks(byTrack, to - from, 0.0);
        if (moved.succeeded) {
            state_.selectedClips.clear();
            for (std::size_t index = 0; index < moved.clipIdsByTrack.size(); ++index)
                for (const auto id : moved.clipIdsByTrack[index])
                    state_.selectedClips.push_back({ { false, index, 0 }, id });
        }
    } else if (drag_ == Drag::move && dragLaneTrack_ && !dragOrigins_.empty()
               && !(*dragLaneTrack_ == dragOrigins_.front().ref.track) && dragLaneTrack_->midi) {
        // MIDI clips moved to another instrument track: copied, then removed.
        std::vector<ClipRef> moved;
        for (const auto& origin : dragOrigins_) {
            const auto* clip = findMidiClip(engine, origin.ref.track.midiId, origin.ref.clipId);
            if (clip == nullptr)
                continue;
            const auto copy = *clip;
            const auto id = engine.addMidiClip(dragLaneTrack_->midiId, copy.name, copy.startBeats, copy.lengthBeats,
                                               copy.looping, copy.colour);
            if (id == 0)
                continue;
            (void) engine.setMidiClipTimelineBounds(dragLaneTrack_->midiId, id, copy.lengthBeats, copy.playStartBeats,
                                                    copy.playEndBeats, copy.loopStartBeats, copy.loopEndBeats,
                                                    copy.looping);
            for (const auto& note : copy.notes)
                (void) engine.copyMidiNote(dragLaneTrack_->midiId, id, note, note.startBeats);
            (void) engine.removeMidiClip(origin.ref.track.midiId, origin.ref.clipId);
            moved.push_back({ *dragLaneTrack_, id });
        }
        if (!moved.empty())
            state_.selectedClips = moved;
    }
    if (drag_ != Drag::box && drag_ != Drag::none)
        (void) engine.endArrangementEditBatch();
    drag_ = Drag::none;
    dragOrigins_.clear();
    dragLaneTrack_.reset();
}

void ArrangementView::handleKeys()
{
    const auto& io = ImGui::GetIO();
    // An automation lane clicked last has the keys (its points).
    if (io.WantTextInput || !focusedCurve_.isEmpty())
        return;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        deleteSelection();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
        duplicateSelection();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        splitSelectionAtPlayhead();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        selectAll();
}

// ---- Commands ----------------------------------------------------------------------------------

void ArrangementView::deleteSelection()
{
    auto& engine = studio_.engine();
    if (state_.selectedClips.empty())
        return;
    engine.beginArrangementEditBatch();
    for (const auto& ref : state_.selectedClips) {
        if (!ref.track.midi)
            (void) engine.removeAudioClip(ref.track.audioIndex, ref.clipId);
        else
            (void) engine.removeMidiClip(ref.track.midiId, ref.clipId);
        if (state_.editedClip && *state_.editedClip == ref)
            state_.editedClip.reset();
    }
    (void) engine.endArrangementEditBatch();
    state_.selectedClips.clear();
    studio_.markDirty();
}

void ArrangementView::duplicateSelection()
{
    auto& engine = studio_.engine();
    const auto spb = secondsPerBeat();
    std::vector<ClipRef> copies;
    engine.beginArrangementEditBatch();
    for (const auto& ref : state_.selectedClips) {
        if (!ref.track.midi) {
            const auto* clip = findAudioClip(engine, ref.track.audioIndex, ref.clipId);
            if (clip == nullptr)
                continue;
            const auto id = engine.duplicateAudioClip(ref.track.audioIndex, ref.clipId, clip->startSeconds + clip->durationSeconds);
            if (id != 0)
                copies.push_back({ ref.track, id });
        } else if (const auto* clip = findMidiClip(engine, ref.track.midiId, ref.clipId)) {
            const auto copy = *clip;
            const auto id = engine.addMidiClip(ref.track.midiId, copy.name, copy.startBeats + copy.lengthBeats,
                                               copy.lengthBeats, copy.looping, copy.colour);
            if (id == 0)
                continue;
            (void) engine.setMidiClipTimelineBounds(ref.track.midiId, id, copy.lengthBeats, copy.playStartBeats,
                                                    copy.playEndBeats, copy.loopStartBeats, copy.loopEndBeats, copy.looping);
            for (const auto& note : copy.notes)
                (void) engine.copyMidiNote(ref.track.midiId, id, note, note.startBeats);
            copies.push_back({ ref.track, id });
        }
    }
    (void) engine.endArrangementEditBatch();
    juce::ignoreUnused(spb);
    if (!copies.empty())
        state_.selectedClips = copies;
    studio_.markDirty();
}

void ArrangementView::splitSelectionAtPlayhead()
{
    auto& engine = studio_.engine();
    const auto at = engine.position();
    engine.beginArrangementEditBatch();
    for (const auto& ref : state_.selectedClips)
        if (!ref.track.midi)
            (void) engine.splitAudioClip(ref.track.audioIndex, ref.clipId, at);
    (void) engine.endArrangementEditBatch();
    studio_.markDirty();
}

void ArrangementView::selectAll()
{
    auto& engine = studio_.engine();
    state_.selectedClips.clear();
    for (const auto& track : studio_.tracks()) {
        if (!track.midi) {
            if (const auto* clips = engine.audioClipsView(track.audioIndex))
                for (const auto& clip : *clips)
                    state_.selectedClips.push_back({ track, clip.id });
        } else if (const auto* clips = engine.midiClipsView(track.midiId)) {
            for (const auto& clip : *clips)
                state_.selectedClips.push_back({ track, clip.id });
        }
    }
}

void ArrangementView::createMidiClipAt(const TrackRef& track, const double seconds)
{
    auto& engine = studio_.engine();
    const auto spb = secondsPerBeat();
    const auto barBeats = studio_.engine().beatsPerBar();
    const auto startBeats = std::floor(snap(seconds, true) / spb / barBeats) * barBeats;
    const auto id = engine.addMidiClip(track.midiId, {}, std::max(0.0, startBeats), 4.0, false,
                                       studio_.trackColour(track));
    if (id == 0)
        return;
    const ClipRef ref { track, id };
    state_.selectedClips = { ref };
    openInEditor(ref);
    studio_.markDirty();
}

void ArrangementView::createAutomationClipAt(const TrackRef& track, const double seconds)
{
    auto& engine = studio_.engine();
    const auto spb = secondsPerBeat();
    const auto bar = spb * std::max(1.0, static_cast<double>(engine.beatsPerBar()));
    const auto start = std::max(0.0, std::floor(snap(seconds, true) / bar) * bar);
    // No sound: a container of lanes alone.
    const auto id = engine.addAudioClip(track.audioIndex, start, 0.0, bar * 4.0, ui::Ls("Automation", "Automation"), studio_.trackColour(track),
                                        {}, 0.0, 0.0);
    if (id == 0)
        return;
    const ClipRef ref { track, id };
    state_.selectedClips = { ref };
    openInEditor(ref);
    studio_.markDirty();
}

void ArrangementView::openInEditor(const ClipRef& clip)
{
    state_.editedClip = clip;
    state_.selectTrack(clip.track);
    state_.bringToFront = Panel::editor;
}

bool ArrangementView::dropFiles(const juce::StringArray& files, const ImVec2 screenPoint)
{
    if (screenPoint.x < lanesMin_.x || screenPoint.x > lanesMax_.x || screenPoint.y < lanesMin_.y - dp(30.0f)
        || screenPoint.y > lanesMax_.y)
        return false;
    auto& engine = studio_.engine();
    const auto time = snap(xToTime(screenPoint.x), true);
    // The lane under the drop, when it is an audio track; a new track below
    // otherwise.
    std::optional<std::size_t> target;
    auto y = lanesMin_.y - state_.scrollY;
    for (const auto& track : studio_.tracks()) {
        const auto height = laneDisplayHeight(track);
        if (screenPoint.y >= y && screenPoint.y < y + height && !track.midi)
            target = track.audioIndex;
        y += height;
    }
    auto added = false;
    for (const auto& path : files) {
        const juce::File file(path);
        if (!file.existsAsFile())
            continue;
        if (target) {
            std::uint64_t clipId {};
            if (engine.addAudioClipFromFile(*target, file, time, &clipId).wasOk()) {
                added = true;
                continue;
            }
        }
        if (const auto track = studio_.addAudioTrack(file, time)) {
            state_.selectedTrack = track;
            added = true;
        }
    }
    if (added)
        studio_.markDirty();
    return added;
}

// ---- Context menus -----------------------------------------------------------------------------

void ArrangementView::clipContextMenu()
{
    if (!ImGui::BeginPopup("##clipContext"))
        return;
    auto& engine = studio_.engine();
    if (contextClip_) {
        if (contextClip_->track.midi && ImGui::MenuItem(L("Ouvrir dans l'éditeur", "Open in the editor"), "Double-clic"))
            openInEditor(*contextClip_);
        if (!contextClip_->track.midi)
            if (const auto* clip = findAudioClip(engine, contextClip_->track.audioIndex, contextClip_->clipId)) {
                const auto index = contextClip_->track.audioIndex;
                const auto id = clip->id;
                const auto reportStatus = [this](const juce::String& message) {
                    state_.status = message;
                    state_.statusIsError = true;
                    state_.statusTime = ImGui::GetTime();
                };
                const auto open = [this](const ClipRef& ref, const int face) {
                    state_.editedClip = ref;
                    state_.audioEditorFace = face;
                    state_.bringToFront = Panel::editor;
                };
                if (ImGui::MenuItem(L("Éditer le son", "Edit the sound"), "Double-clic"))
                    open(*contextClip_, 0);
                if (ImGui::MenuItem(L("Prises et comping", "Takes and comping")))
                    open(*contextClip_, 1);
                if (ImGui::MenuItem(L("Étirement et transposition", "Stretch and transpose")))
                    open(*contextClip_, 2);
                if (ImGui::MenuItem(L("Ouvrir son fichier dans la Fonderie", "Open its file in the Foundry"))) {
                    state_.foundryOpen = clip->sourceFile;
                    state_.mode = AppMode::foundry;
                }
                if (ImGui::MenuItem(L("Charger sur une platine (Mix DJ)", "Load on a deck (DJ mix)"))) {
                    state_.djLoad = std::pair { -1, clip->sourceFile };
                    state_.mode = AppMode::dj;
                }
                if (clip->takes.size() > 1 && ImGui::BeginMenu(L("Prise jouée", "Take played"))) {
                    for (std::size_t take = 0; take < clip->takes.size(); ++take) {
                        char label[160];
                        std::snprintf(label, sizeof(label), "%d  %s", static_cast<int>(take) + 1, clip->takes[take].name.toRawUTF8());
                        if (ImGui::MenuItem(label, nullptr, clip->comp.empty() && take == clip->activeTakeIndex)) {
                            (void) engine.setActiveAudioTake(index, id, take);
                            (void) engine.setAudioClipComp(index, id, {});
                            studio_.markDirty();
                        }
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem(L("Fondu enchaîné avec le voisin", "Crossfade with the neighbour"))) {
                    if (engine.autoCrossfadeAudioClip(index, id))
                        studio_.markDirty();
                    else
                        reportStatus(ui::Ls("Aucun clip ne chevauche celui-ci", "No clip overlaps this one"));
                }
                if (clip->sourceTempoBpm > 0.0 && ImGui::MenuItem(L("Caler sur le tempo du projet", "Fit to the project's tempo"))) {
                    const auto source = clip->sourceDurationSeconds > 0.0 ? clip->sourceDurationSeconds : clip->durationSeconds;
                    if (engine.stretchAudioClip(index, id, source * clip->sourceTempoBpm / std::max(1.0, engine.tempo())))
                        studio_.markDirty();
                }
                if (ImGui::MenuItem(L("Normaliser", "Normalize"))) {
                    app::AudioEditRequest request;
                    request.operation = app::AudioEditOperation::normalisePeak;
                    request.levelDb = -1.0f;
                    if (const auto done = studio_.editAudioClip(contextClip_->track, id, request, clip->sourceOffsetSeconds,
                                                                clip->sourceDurationSeconds > 0.0 ? clip->sourceDurationSeconds : clip->durationSeconds,
                                                                juce::String::fromUTF8("Normaliser"));
                        done.failed())
                        reportStatus(done.getErrorMessage());
                }
                if (ImGui::MenuItem(L("Inverser le clip (Reverse)", "Reverse clip")))
                    if(const auto done=studio_.reverseAudioClip(contextClip_->track,id);done.failed())reportStatus(done.getErrorMessage());
                ImGui::Separator();
            }
        if (ImGui::MenuItem(L("Dupliquer", "Duplicate"), "Ctrl+D"))
            duplicateSelection();
        if (!contextClip_->track.midi && ImGui::MenuItem(L("Scinder à la tête de lecture", "Split at the playhead"), "S"))
            splitSelectionAtPlayhead();
        if (ImGui::BeginMenu(L("Couleur", "Colour"))) {
            for (auto index = 0; index < 12; ++index) {
                ImGui::PushID(index);
                const auto colour = theme::trackColour(index);
                if (ui::colourChip("##c", colour, ImVec2(dp(18.0f), dp(18.0f)))) {
                    for (const auto& ref : state_.selectedClips)
                        if (ref.track.midi)
                            (void) engine.setMidiClipPresentation(ref.track.midiId, ref.clipId, {}, theme::toArgb(colour));
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
                if (index % 6 != 5)
                    ImGui::SameLine(0.0f, dp(3.0f));
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem(L("Supprimer", "Delete"), "Suppr"))
            deleteSelection();
    }
    ImGui::EndPopup();
}

// ---- Automation ----------------------------------------------------------------------------------

// The lane shown under a track and how it maps values: the volume, or the
// parameter of the track's graph it names; one whose parameter is gone
// shows the volume and `lane` comes back empty.
const std::vector<app::IsolatedPluginHost::ParameterSnapshot>&
ArrangementView::pluginParameters(const std::size_t trackIndex, const std::size_t plugin, const bool refresh) const
{
    const auto plugins = studio_.engine().trackPlugins(trackIndex);
    const auto key = juce::String(static_cast<juce::int64>(trackIndex)) + ":" + juce::String(static_cast<juce::int64>(plugin)) + ":"
                     + (plugin < plugins.size() ? plugins[plugin].file.getFullPathName() : juce::String());
    auto& entry = pluginParameters_[key];
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (refresh && now - entry.fetchedAt > 3.0 && plugin < plugins.size() && plugins[plugin].running) {
        entry.fetchedAt = now;
        std::vector<app::IsolatedPluginHost::ParameterSnapshot> fresh;
        if (studio_.engine().refreshTrackPluginParameters(trackIndex, plugin, fresh).wasOk())
            entry.values = std::move(fresh);
    }
    return entry.values;
}

ArrangementView::LaneScale ArrangementView::laneScale(const TrackRef& track, juce::String& lane) const
{
    LaneScale scale;
    scale.label = ui::Ls("Volume", "Volume");
    lane = studio_.shownLane(track);
    if (lane.isEmpty())
        return scale;
    // A plugin's parameter: 0 to 1, named by the plugin.
    std::size_t plugin {};
    std::uint32_t parameterIndex {};
    if (Studio::parsePluginLane(lane, plugin, parameterIndex)) {
        const auto plugins = track.midi ? std::vector<app::AudioTrackController::TrackInfo::PluginInfo> {}
                                        : studio_.engine().trackPlugins(track.audioIndex);
        if (plugin >= plugins.size()) {
            lane.clear();
            return scale;
        }
        auto value = 0.5;
        for (const auto& snapshot : pluginParameters(track.audioIndex, plugin, false))
            if (snapshot.index == parameterIndex)
                value = snapshot.value;
        const auto name = studio_.pluginParameterName(track, lane);
        scale.volume = false;
        scale.parameter = { lane, name, {}, 0.0, 1.0, value, 0.5 };
        scale.label = plugins[plugin].name + juce::String::fromUTF8(" \xc2\xb7 ") + name;
        return scale;
    }
    if (const auto* slot = studio_.graphFor(track))
        for (const auto& node : slot->graph().editorNodes())
            for (const auto& parameter : node.parameters)
                if (parameter.binding == lane) {
                    scale.volume = false;
                    scale.parameter = { parameter.binding, parameter.label, parameter.unit, parameter.minimum,
                                        parameter.maximum, parameter.value, parameter.defaultValue };
                    scale.label = node.title + juce::String::fromUTF8(" \xc2\xb7 ") + parameter.label;
                    return scale;
                }
    lane.clear();
    return scale;
}

juce::String ArrangementView::formatLaneValue(const float value, const LaneScale& scale)
{
    if (scale.volume)
        return juce::String(value, 1) + " dB";
    return visual::formatValue(scale.parameter, value);
}

juce::String ArrangementView::curveKey(const TrackRef& track, const juce::String& lane)
{
    const auto owner = track.master ? juce::String("master")
                       : track.midi ? "midi" + juce::String(static_cast<juce::int64>(track.midiId))
                                    : "audio" + juce::String(static_cast<juce::int64>(track.audioIndex));
    return owner + "|" + lane;
}

curves::Lane ArrangementView::curveLane(const TrackRef& track, const juce::String& name, const LaneScale& scale) const
{
    const auto& p = theme::palette();
    curves::Lane lane;
    if (scale.volume) {
        // The volume on its squared scale, as the band always showed it.
        lane.minimum = kAutomationMinimumDb;
        lane.maximum = kAutomationMaximumDb;
        lane.toPart = [](const double value) { return static_cast<double>(automationPosition(static_cast<float>(value))); };
        lane.fromPart = [](const double part) { return static_cast<double>(automationValue(static_cast<float>(part))); };
        lane.fallback = studio_.trackMix(track).gainDb;
        lane.colour = p.control;
    } else {
        const auto parameter = scale.parameter;
        lane.minimum = std::min(parameter.minimum, parameter.maximum);
        lane.maximum = std::max(parameter.minimum, parameter.maximum);
        lane.toPart = [parameter](const double value) { return visual::positionOf(parameter, value); };
        lane.fromPart = [parameter](const double part) { return visual::valueAtPosition(parameter, part); };
        // It glides as the engine plays it: a frequency, a time on their
        // logarithmic scale.
        lane.logarithmic = !name.startsWith("plugin:") && studio_.parameterIsLogarithmic(track, name);
        lane.fallback = parameter.value;
        lane.colour = p.modulation;
    }
    lane.formatValue = [scale](const double value) { return formatLaneValue(static_cast<float>(value), scale); };
    const auto secondsPerBeat = this->secondsPerBeat();
    const auto beatsPerBar = std::max(1.0, static_cast<double>(studio_.engine().beatsPerBar()));
    lane.formatTime = [secondsPerBeat, beatsPerBar](const double seconds) {
        const auto beats = seconds / std::max(1.0e-9, secondsPerBeat);
        const auto bar = std::floor(beats / beatsPerBar + 1.0e-9);
        const auto inBar = beats - bar * beatsPerBar;
        const auto beat = std::floor(inBar + 1.0e-6);
        const auto sixteenth = std::floor((inBar - beat) * 4.0 + 1.0e-6);
        return juce::String(static_cast<int>(bar) + 1) + "." + juce::String(static_cast<int>(beat) + 1) + "."
               + juce::String(static_cast<int>(sixteenth) + 1) + "  (" + juce::String(seconds, 3) + " s)";
    };
    return lane;
}

curves::View ArrangementView::curveView() const
{
    curves::View view;
    view.xOf = [this](const double seconds) { return timeToX(seconds); };
    view.timeAt = [this](const float x) { return xToTime(x); };
    view.snap = [this](const double seconds) { return snap(seconds); };
    view.cursor = studio_.engine().position();
    view.minimumTime = 0.0;
    view.hostInput = true;
    return view;
}

std::vector<curves::Point> ArrangementView::laneCurve(const TrackRef& track, const juce::String& lane) const
{
    std::vector<curves::Point> points;
    for (const auto& point : studio_.lanePoints(track, lane))
        points.push_back({ point.id, point.timeSeconds, point.valueDb, static_cast<int>(point.curve), point.curveAmount, point.handles });
    return points;
}

void ArrangementView::setLaneCurve(const TrackRef& track, const juce::String& lane, const std::vector<curves::Point>& points)
{
    rememberAutomation(track, lane);
    std::vector<Studio::AutomationPoint> stored;
    for (const auto& point : points)
        stored.push_back({ point.id, std::max(0.0, point.time), static_cast<float>(point.value),
                           static_cast<Studio::AutomationCurve>(engine::clampedCurveShape(point.shape)), point.bend, point.handles });
    studio_.setLanePoints(track, lane, stored);
}

void ArrangementView::unfocusCurves()
{
    for (auto& [key, editor] : curveEditors_)
        editor.setFocused(false);
    focusedCurve_.clear();
    focusedCurveTrack_.reset();
    focusedCurveLane_.clear();
}

// Over the canvas while lanes are shown: the points picked in the lane last
// clicked, their values to read and edit (the row stays, so the lanes do
// not move under the pointer when one is clicked).
void ArrangementView::drawCurveBar()
{
    const auto& p = theme::palette();
    const auto tracks = studio_.tracks();
    if (std::none_of(tracks.begin(), tracks.end(), [this](const TrackRef& track) { return studio_.automationVisible(track); })) {
        unfocusCurves();
        return;
    }
    ImGui::AlignTextToFramePadding();
    const auto found = curveEditors_.find(focusedCurve_);
    if (found == curveEditors_.end() || !focusedCurveTrack_ || !studio_.isValid(*focusedCurveTrack_)
        || !studio_.automationVisible(*focusedCurveTrack_)) {
        ImGui::TextDisabled("%s", L("Automation : cliquez un point d'un couloir pour lire et modifier ses valeurs, encadrez-en plusieurs pour les modifier ensemble.",
                                    "Automation: click a lane's point to read and edit its values, frame several to edit them together."));
        return;
    }
    const auto track = *focusedCurveTrack_;
    auto shown = focusedCurveLane_;
    const auto scale = laneScale(track, shown);
    ImGui::TextColored(theme::vec(scale.volume ? p.control : p.modulation), "%s",
                       (studio_.trackName(track) + juce::String::fromUTF8(" \xc2\xb7 ") + scale.label).toRawUTF8());
    ImGui::SameLine(0.0f, dp(10.0f));
    auto points = laneCurve(track, focusedCurveLane_);
    const auto gridSeconds = state_.snapBeats > 0.0 ? state_.snapBeats * secondsPerBeat() : secondsPerBeat();
    if (found->second.drawBar(points, curveLane(track, focusedCurveLane_, scale), gridSeconds))
        setLaneCurve(track, focusedCurveLane_, points);
}

ImRect ArrangementView::laneLabelRect(const LaneHit& lane) const
{
    const auto size = ImGui::GetStyle().FontSizeBase * 0.78f * ImGui::GetStyle().FontScaleMain * ImGui::GetStyle().FontScaleDpi;
    const auto text = lane.scale.label + "  " + juce::String::fromUTF8(icon::chevronDown);
    const auto width = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, text.toRawUTF8()).x;
    const ImVec2 min(lanesMin_.x + dp(3.0f), lane.clipBottom + dp(1.0f));
    return { min, ImVec2(min.x + width + dp(8.0f), min.y + size + dp(3.0f)) };
}

// The lane over time, its curve editor: the curve as the engine plays it,
// its points to pick, move, shape. Its name, top left, is the button that
// chooses which lane the track shows.
void ArrangementView::drawAutomation(ImDrawList* list, const LaneHit& lane)
{
    const auto& p = theme::palette();
    const ImVec2 min(lanesMin_.x, lane.clipBottom);
    const ImVec2 max(lanesMax_.x, lane.bottom - 1.0f);
    // The editor under the lane's name.
    const ImVec2 editorMin(min.x, lane.clipBottom + dp(12.0f));
    list->AddRectFilled(min, max, theme::alpha(0xff000000u, 0.22f));
    list->AddLine(min, ImVec2(max.x, min.y), p.grid);
    const auto description = curveLane(lane.track, lane.lane, lane.scale);
    if (lane.scale.volume) {
        // 0 dB, dashed.
        const auto pad = dp(5.0f);
        const auto zero = max.y - pad - static_cast<float>(curves::Editor::partOf(description, 0.0)) * std::max(1.0f, max.y - editorMin.y - pad * 2.0f);
        for (auto x = min.x; x < max.x; x += dp(8.0f))
            list->AddLine(ImVec2(x, zero), ImVec2(std::min(max.x, x + dp(4.0f)), zero), theme::alpha(p.textFaint, 0.35f));
    }

    // A device's lane in its own colour, so it reads apart from the volume.
    const auto colour = lane.scale.volume ? p.control : p.modulation;
    auto points = laneCurve(lane.track, lane.lane);
    const auto label = laneLabelRect(lane);
    const auto key = curveKey(lane.track, lane.lane);
    auto& editor = curveEditors_[key];
    auto view = curveView();
    view.hostHovered = canvasHovered_ && drag_ == Drag::none && !label.Contains(ImGui::GetIO().MousePos);
    view.menu = [this, track = lane.track, name = lane.lane] {
        if (ImGui::MenuItem(L("Effacer l'automation", "Clear automation"), nullptr, false, !studio_.lanePoints(track, name).empty())) {
            rememberAutomation(track, name);
            studio_.setLanePoints(track, name, {});
        }
        if (ImGui::MenuItem(L("Choisir le paramètre…", "Choose the parameter…")))
            chooseLaneFor_ = track;
        if (ImGui::MenuItem(L("Masquer le couloir", "Hide the lane")))
            studio_.setAutomationVisible(track, false);
    };
    const auto wasFocused = editor.focused();
    if (editor.draw(key.toRawUTF8(), editorMin, max, points, description, view))
        setLaneCurve(lane.track, lane.lane, points);
    // One lane takes the keys at a time: the one last clicked.
    if (editor.focused() && (!wasFocused || focusedCurve_ != key)) {
        for (auto& [other, otherEditor] : curveEditors_)
            if (other != key)
                otherEditor.setFocused(false);
        focusedCurve_ = key;
        focusedCurveTrack_ = lane.track;
        focusedCurveLane_ = lane.lane;
        state_.focused = Panel::arrangement;
        state_.selectTrack(lane.track);
    }

    // The lane's name, over the curve: the button that chooses the lane.
    const auto labelHovered = (laneChooserTrack_ && *laneChooserTrack_ == lane.track && ImGui::IsPopupOpen(laneChooserId_, 0))
                              || ImGui::IsMouseHoveringRect(label.Min, label.Max);
    list->AddRectFilled(label.Min, label.Max, labelHovered ? theme::mix(p.bg2, colour, 0.35f) : theme::alpha(p.bg1, 0.92f), dp(3.0f));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.78f);
    const auto text = lane.scale.label + "  " + juce::String::fromUTF8(icon::chevronDown);
    list->AddText(ImVec2(label.Min.x + dp(4.0f), label.Min.y + dp(1.0f)), labelHovered ? p.text : theme::alpha(colour, 0.95f), text.toRawUTF8());
    if (points.empty())
        list->AddText(ImVec2(label.Max.x + dp(8.0f), label.Min.y + dp(1.0f)), p.textFaint,
                      L("double-clic : ajouter un point", "double click: add a point"));
    ImGui::PopFont();
}

void ArrangementView::rememberAutomation(const TrackRef& track, const juce::String& lane)
{
    automationUndo_.push_back({ track, lane, studio_.lanePoints(track, lane) });
    if (automationUndo_.size() > 64)
        automationUndo_.erase(automationUndo_.begin());
    automationRedo_.clear();
    automationLastEdit_ = true;
}

bool ArrangementView::undoAutomation()
{
    if (!automationLastEdit_ || automationUndo_.empty())
        return false;
    auto edit = std::move(automationUndo_.back());
    automationUndo_.pop_back();
    if (!studio_.isValid(edit.track))
        return undoAutomation();
    automationRedo_.push_back({ edit.track, edit.lane, studio_.lanePoints(edit.track, edit.lane) });
    studio_.setLanePoints(edit.track, edit.lane, edit.points);
    if (automationUndo_.empty())
        automationLastEdit_ = false;
    return true;
}

bool ArrangementView::redoAutomation()
{
    if (automationRedo_.empty())
        return false;
    auto edit = std::move(automationRedo_.back());
    automationRedo_.pop_back();
    if (!studio_.isValid(edit.track))
        return redoAutomation();
    automationUndo_.push_back({ edit.track, edit.lane, studio_.lanePoints(edit.track, edit.lane) });
    studio_.setLanePoints(edit.track, edit.lane, edit.points);
    automationLastEdit_ = true;
    return true;
}

// Clicks on an automation band: its name chooses the lane; the rest is its
// curve editor's (drawn with the lane). True when the band took the click.
bool ArrangementView::handleAutomationInput(const std::vector<LaneHit>& lanes)
{
    const auto mouse = ImGui::GetIO().MousePos;
    // A curve editor in a gesture keeps the pointer.
    if (std::any_of(curveEditors_.begin(), curveEditors_.end(), [](const auto& entry) { return entry.second.busy(); }))
        return true;
    if (!ImGui::IsItemHovered())
        return false;
    const LaneHit* lane = nullptr;
    for (const auto& candidate : lanes)
        if (candidate.automation && mouse.y >= candidate.clipBottom && mouse.y < candidate.bottom - dp(3.0f)) {
            lane = &candidate;
            break;
        }
    if (lane == nullptr) {
        // A click among the clips: the keys go back to them.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            unfocusCurves();
        return false;
    }

    // The lane's name chooses the lane.
    if (const auto label = laneLabelRect(*lane); label.Contains(mouse)) {
        ImGui::SetTooltip("%s", L("Choisir ce que le couloir automatise", "Choose what the lane automates"));
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            state_.selectTrack(lane->track);
            laneChooserTrack_ = lane->track;
            ImGui::OpenPopup(laneChooserId_);
        }
        return true;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        state_.selectTrack(lane->track);
    return true;
}

// The lanes a track can show: its volume, the parameters already automated,
// then every device of its chain with its parameters.
void ArrangementView::laneChooserMenu()
{
    if (!ImGui::BeginPopup("##laneChooser"))
        return;
    if (!laneChooserTrack_ || !studio_.isValid(*laneChooserTrack_)) {
        ImGui::EndPopup();
        return;
    }
    const auto track = *laneChooserTrack_;
    const auto shown = studio_.shownLane(track);
    const auto automated = studio_.automatedParameters(track);
    const auto isAutomated = [&automated](const juce::String& binding) {
        return std::find(automated.begin(), automated.end(), binding) != automated.end();
    };
    const auto mark = "\xe2\x97\x8f";
    if (ImGui::MenuItem(L("Volume", "Volume"), studio_.lanePoints(track, {}).empty() ? nullptr : mark, shown.isEmpty()))
        studio_.setShownLane(track, {});
    const auto* slot = studio_.graphFor(track);
    if (slot == nullptr) {
        ImGui::EndPopup();
        return;
    }
    auto nodes = slot->graph().editorNodes();
    std::erase_if(nodes, [](const auto& node) { return node.boundary || node.parameters.empty(); });
    std::stable_sort(nodes.begin(), nodes.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.position.x - b.position.x) > 0.02f)
            return a.position.x < b.position.x;
        return a.position.y < b.position.y;
    });
    const auto nameOf = [](const auto& node, const auto& parameter) {
        return node.title + juce::String::fromUTF8(" \xc2\xb7 ") + parameter.label;
    };
    if (!automated.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", L("Automatisés", "Automated"));
        for (const auto& node : nodes)
            for (const auto& parameter : node.parameters)
                if (isAutomated(parameter.binding)
                    && ImGui::MenuItem(nameOf(node, parameter).toRawUTF8(), mark, shown == parameter.binding))
                    studio_.setShownLane(track, parameter.binding);
    }
    for (const auto& binding : automated) {
        std::size_t plugin {};
        std::uint32_t parameter {};
        if (!Studio::parsePluginLane(binding, plugin, parameter))
            continue;
        const auto plugins = studio_.engine().trackPlugins(track.audioIndex);
        const auto name = (plugin < plugins.size() ? plugins[plugin].name : juce::String("Plugin"))
                          + juce::String::fromUTF8(" \xc2\xb7 ") + studio_.pluginParameterName(track, binding);
        if (ImGui::MenuItem(name.toRawUTF8(), mark, shown == binding))
            studio_.setShownLane(track, binding);
    }
    if (!nodes.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", L("Périphériques", "Devices"));
    }
    for (const auto& node : nodes) {
        ImGui::PushID(static_cast<int>(node.id));
        if (ImGui::BeginMenu(node.title.toRawUTF8())) {
            // What the engine can follow: the chain's cards, the knobs
            // inside its modules, the instrument's own settings.
            const auto item = [&](const auto& parameter, const juce::String& label) {
                const auto reachable = studio_.canAutomate(track, parameter.binding);
                if (ImGui::MenuItem(label.toRawUTF8(), isAutomated(parameter.binding) ? mark : nullptr, shown == parameter.binding,
                                    reachable))
                    studio_.setShownLane(track, parameter.binding);
            };
            // Many values (an equaliser's eight bands and their dynamics):
            // a menu for each group their names start with ("Bande 3 · Gain").
            const auto grouped = node.parameters.size() > 16;
            const auto separator = juce::String::fromUTF8(" \xc2\xb7 ");
            std::vector<juce::String> groups;
            for (const auto& parameter : node.parameters) {
                const auto at = parameter.label.indexOf(separator);
                if (!grouped || at <= 0) {
                    item(parameter, parameter.label);
                    continue;
                }
                const auto group = parameter.label.substring(0, at);
                if (std::find(groups.begin(), groups.end(), group) != groups.end())
                    continue;
                groups.push_back(group);
                if (ImGui::BeginMenu(group.toRawUTF8())) {
                    for (const auto& member : node.parameters)
                        if (member.label.startsWith(group + separator))
                            item(member, member.label.substring(group.length() + separator.length()));
                    ImGui::EndMenu();
                }
            }
            ImGui::EndMenu();
        }
        ImGui::PopID();
    }
    // The plugins of an audio track, with what their workers report.
    const auto plugins = track.midi ? std::vector<app::AudioTrackController::TrackInfo::PluginInfo> {}
                                    : studio_.engine().trackPlugins(track.audioIndex);
    if (!plugins.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", L("Plugins", "Plugins"));
        for (std::size_t plugin = 0; plugin < plugins.size(); ++plugin) {
            ImGui::PushID(static_cast<int>(1'000 + plugin));
            if (ImGui::BeginMenu(plugins[plugin].name.toRawUTF8(), plugins[plugin].running)) {
                const auto& parameters = pluginParameters(track.audioIndex, plugin, true);
                if (parameters.empty())
                    ImGui::TextDisabled("%s", L("Aucun paramètre lu", "No parameter read"));
                for (const auto& parameter : parameters) {
                    if (!parameter.automatable)
                        continue;
                    const auto lane = Studio::pluginLaneName(plugin, parameter.index);
                    ImGui::PushID(static_cast<int>(parameter.index));
                    if (ImGui::MenuItem(parameter.name.isNotEmpty() ? parameter.name.toRawUTF8() : "?",
                                        isAutomated(lane) ? mark : nullptr, shown == lane)) {
                        studio_.rememberPluginParameterName(lane, parameter.name);
                        studio_.setShownLane(track, lane);
                    }
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndPopup();
}

void ArrangementView::trackContextMenu()
{
    if (!ImGui::BeginPopup("##trackContext"))
        return;
    if (contextTrack_ && studio_.isValid(*contextTrack_)) {
        const auto track = *contextTrack_;
        ImGui::TextDisabled("%s", studio_.trackName(track).toRawUTF8());
        ImGui::Separator();
        if (ImGui::MenuItem(L("Renommer", "Rename"), "Double-clic")) {
            renamingTrack_ = track;
            focusRename_ = true;
            std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", studio_.trackName(track).toRawUTF8());
        }
        if (ImGui::BeginMenu(L("Couleur", "Colour"))) {
            for (auto index = 0; index < 12; ++index) {
                ImGui::PushID(index);
                const auto colour = theme::trackColour(index);
                if (ui::colourChip("##c", colour, ImVec2(dp(18.0f), dp(18.0f)))) {
                    studio_.setTrackColour(track, theme::toArgb(colour));
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
                if (index % 6 != 5)
                    ImGui::SameLine(0.0f, dp(3.0f));
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(L("Monter", "Move up")))
            (void) studio_.moveTrack(track, -1);
        if (ImGui::MenuItem(L("Descendre", "Move down")))
            (void) studio_.moveTrack(track, 1);
        if (track.midi && ImGui::MenuItem(L("Nouveau clip à la tête de lecture", "New clip at the playhead")))
            createMidiClipAt(track, studio_.engine().position());
        if (ImGui::MenuItem(L("Automation du volume", "Volume automation"), nullptr, studio_.automationVisible(track)))
            studio_.setAutomationVisible(track, !studio_.automationVisible(track));
        ImGui::Separator();
        if (ImGui::MenuItem(L("Supprimer la piste", "Delete track"))) {
            state_.selectedClips.clear();
            state_.editedClip.reset();
            if (studio_.removeTrack(track))
                state_.selectedTrack.reset();
        }
    }
    ImGui::EndPopup();
}

} // namespace cydaw::v2
