#pragma once

#include "CurveEditor.h"
#include "DeviceVisuals.h"
#include "Studio.h"
#include "UiState.h"

#include <imgui.h>
#include <imgui_internal.h>

// The arrangement: compact track headers, the ruler, the clips with their
// waveforms or notes, the playhead and the loop. Clips are moved, trimmed,
// split, duplicated and deleted directly; the wheel scrolls, Ctrl+wheel zooms
// around the pointer.

namespace cydaw::v2 {

class ImGuiHost;

class ArrangementView final {
public:
    ArrangementView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host);

    void draw();
    // Files dropped on the window, at a point of the screen.
    bool dropFiles(const juce::StringArray& files, ImVec2 screenPoint);

    // Commands the menus and shortcuts call.
    void deleteSelection();
    void duplicateSelection();
    void splitSelectionAtPlayhead();
    void selectAll();
    void zoom(float factor, std::optional<float> anchorX = std::nullopt);
    void zoomToFit();
    // Automation edits have their own history; these act when the last
    // edit in the arrangement was one.
    bool undoAutomation();
    bool redoAutomation();

private:
    struct ClipHit {
        ClipRef ref;
        ImVec2 min;
        ImVec2 max;
        double startSeconds {};
        double durationSeconds {};
        bool midi {};
    };
    // How the lane shown under a track maps its values to the band's height.
    struct LaneScale {
        // The volume, in dB; otherwise a parameter of the track's graph.
        bool volume { true };
        juce::String label;
        visual::Param parameter;
    };
    struct LaneHit {
        TrackRef track;
        float top {};
        float bottom {};
        float clipBottom {};   // the automation band starts here when shown
        bool automation {};
        // The lane shown: empty for the volume, else a parameter's binding.
        juce::String lane;
        LaneScale scale;
    };
    enum class Drag { none, move, trimLeft, trimRight, fadeIn, fadeOut, seek, loop, box, resizeLane, scrollbar, navigate };
    struct AutomationEdit {
        TrackRef track;
        juce::String lane;
        std::vector<Studio::AutomationPoint> points;
    };

    void drawToolbar();
    void drawCanvas();
    void drawRuler(ImDrawList* list, ImVec2 min, ImVec2 max);
    // Over the times, the band to grab: left and right move through the
    // song, up and down zoom (around where it was grabbed).
    void drawNavigator(ImDrawList* list, ImVec2 min, ImVec2 max);
    void drawGrid(ImDrawList* list, ImVec2 min, ImVec2 max);
    void drawAudioClip(ImDrawList* list, const ClipHit& hit, const app::AudioTrackController::AudioClip& clip,
                       bool selected, bool hovered);
    void drawMidiClip(ImDrawList* list, const ClipHit& hit, const app::AudioTrackController::MidiClip& clip,
                      bool selected, bool hovered);
    void drawHeader(const TrackRef& track, ImVec2 min, ImVec2 max, int index);
    void handleLaneInput(const std::vector<ClipHit>& clips, const std::vector<LaneHit>& lanes, bool clicksTaken);
    void drawAutomation(ImDrawList* list, const LaneHit& lane);
    bool handleAutomationInput(const std::vector<LaneHit>& lanes);
    void laneChooserMenu();
    void rememberAutomation(const TrackRef& track, const juce::String& lane);
    [[nodiscard]] LaneScale laneScale(const TrackRef& track, juce::String& lane) const;
    [[nodiscard]] static juce::String formatLaneValue(float value, const LaneScale& scale);
    // The lanes' curve editors (CurveEditor.h): a lane as one sees it, the
    // timeline, a lane's points and back, and the bar of the picked points
    // over the canvas.
    [[nodiscard]] static juce::String curveKey(const TrackRef& track, const juce::String& lane);
    [[nodiscard]] curves::Lane curveLane(const TrackRef& track, const juce::String& lane, const LaneScale& scale) const;
    [[nodiscard]] curves::View curveView() const;
    [[nodiscard]] std::vector<curves::Point> laneCurve(const TrackRef& track, const juce::String& lane) const;
    void setLaneCurve(const TrackRef& track, const juce::String& lane, const std::vector<curves::Point>& points);
    void drawCurveBar();
    void unfocusCurves();
    // Where the lane's name is drawn, a button that chooses the lane.
    [[nodiscard]] ImRect laneLabelRect(const LaneHit& lane) const;
    void handleKeys();
    void clipContextMenu();
    void trackContextMenu();
    void beginDrag(Drag drag, const std::vector<ClipHit>& clips);
    void applyDrag(const std::vector<LaneHit>& lanes);
    void endDrag(const std::vector<LaneHit>& lanes);
    void createMidiClipAt(const TrackRef& track, double seconds);
    void openInEditor(const ClipRef& clip);

    [[nodiscard]] double secondsPerBeat() const;
    [[nodiscard]] double snap(double seconds, bool force = false) const;
    [[nodiscard]] float timeToX(double seconds) const;
    [[nodiscard]] double xToTime(float x) const;
    [[nodiscard]] float laneDisplayHeight(const TrackRef& track) const;
    [[nodiscard]] double contentEndSeconds() const;
    [[nodiscard]] const LaneHit* laneAt(const std::vector<LaneHit>& lanes, float y) const;

    Studio& studio_;
    UiState& state_;
    WaveformCache& waveforms_;
    ImGuiHost& host_;

    // Geometry of the last frame.
    ImVec2 lanesMin_ {};
    ImVec2 lanesMax_ {};
    float headerWidth_ {};

    Drag drag_ { Drag::none };
    ImVec2 dragStartMouse_ {};
    double dragStartTime_ {};
    struct Origin {
        ClipRef ref;
        double start {};
        double duration {};
        double offset {};
        double sourceDuration {};
        double lengthBeats {};
        double fadeIn {};
        double fadeOut {};
    };
    std::vector<Origin> dragOrigins_;
    std::optional<TrackRef> dragLaneTrack_;
    TrackRef resizeTrack_ {};
    float resizeStartHeight_ {};
    double loopAnchor_ {};
    // The time grabbed in the navigator, kept under the pointer.
    double navigateAnchor_ {};
    // Following the playhead waits while the person moves through the song
    // during playback, until the next play or the follow button.
    bool followPaused_ {};
    ImGuiID clipPopupId_ {};
    ImGuiID trackPopupId_ {};
    ImGuiID laneChooserId_ {};
    std::optional<TrackRef> laneChooserTrack_;
    // A plugin's parameters as its worker last reported them, for the lane
    // chooser and the lanes' names: when, and what.
    struct PluginParameters {
        double fetchedAt { -1.0e9 };
        std::vector<app::IsolatedPluginHost::ParameterSnapshot> values;
    };
    mutable std::map<juce::String, PluginParameters> pluginParameters_;
    const std::vector<app::IsolatedPluginHost::ParameterSnapshot>& pluginParameters(std::size_t trackIndex,
                                                                                   std::size_t plugin, bool refresh) const;
    // Automation editing: each lane's curve editor (by curveKey), the one
    // last clicked (it has the keys), whether the canvas has the pointer,
    // and a lane whose chooser its menu asked for.
    std::map<juce::String, curves::Editor> curveEditors_;
    juce::String focusedCurve_;
    std::optional<TrackRef> focusedCurveTrack_;
    juce::String focusedCurveLane_;
    bool canvasHovered_ {};
    std::optional<TrackRef> chooseLaneFor_;
    std::vector<AutomationEdit> automationUndo_;
    std::vector<AutomationEdit> automationRedo_;
    bool automationLastEdit_ {};
    std::optional<ClipRef> contextClip_;
    std::optional<TrackRef> contextTrack_;
    std::optional<TrackRef> renamingTrack_;
    char renameBuffer_[128] {};
    bool focusRename_ {};
};

} // namespace cydaw::v2
