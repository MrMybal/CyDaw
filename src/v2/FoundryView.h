#pragma once

#include "FoundryDocument.h"
#include "UiState.h"
#include "TransportSync.h"
#include "CinemaVideo.h"

#include <imgui.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <optional>
#include <map>
#include <set>
#include <vector>

// The Foundry: a whole window that edits sound, as a sound editor does, on
// several tracks. Sessions in tabs; each track holds clips (sounds dropped
// on it, moved, trimmed, faded, split), with its gain, pan, mute and solo.
// A selection spans a stretch of time on some tracks (or one channel of
// them); cut, copy, paste, mix, crop, silence; gain, normalise, fades,
// reverse, polarity, DC offset, stretch and pitch, the rate, the channels,
// any track's effects of the project rendered into the clips; the
// statistics and spectrum of what is selected; every edit undone. A single
// sound file is saved back as itself (its markers in a WAV), a session as
// a .cyfoundry file, the mix exported as a sound file. Its sound goes into
// the master like the tracks'.

namespace cydaw::app { class FoundryPlayer; }

namespace cydaw::v2 {

class ImGuiHost;
class Studio;
class ModeProcessing;

class FoundryView final {
public:
    FoundryView(Studio& studio, UiState& state, ImGuiHost& host, bool cinema = false);
    ~FoundryView();

    void draw();
    // The transport bar's middle in this mode: play, stop, loop, where.
    void drawTransport();
    [[nodiscard]] juce::var command(const juce::var& request);
    // A sound file or a session in a tab of its own.
    bool open(const juce::File& file);
    // Files into the session shown: on the track under (x, y), else new
    // tracks; with no session, the first opens and the others join it.
    bool dropFiles(const juce::StringArray& files, float x = -1.0f, float y = -1.0f);
    void chooseOpen();
    void chooseImport();
    void newDocument();
    void save(bool as);
    void chooseExport();
    bool undoProcessing(bool redo = false);
    bool undo();
    bool redo();
    void togglePlay();
    [[nodiscard]] TransportSync::State transportState() const;
    void applyTransport(const TransportSync::Update&);
    // A file not saved: its name (the quit asks first).
    [[nodiscard]] juce::StringArray unsaved() const;
    void tickCinemaVideo();
    void showToolsPanel(int panel);

private:
    using Session = engine::foundry::Session;
    using Sound = engine::foundry::Sound;
    using Clip = engine::foundry::Clip;

    struct Tab {
        juce::Uuid cinemaIdentity;
        std::unique_ptr<FoundryDocument> document;
        std::shared_ptr<ModeProcessing> processing;
        const Session* processingSession{};
        std::uint64_t processingRevision{};
        std::uint64_t observedProcessingRevision{};
        double processingChangedAt{};
        std::int64_t selectionStart {};
        std::int64_t selectionEnd {};
        std::vector<bool> tracks;   // the tracks picked
        unsigned mask { 0xffu };    // the channels a selection takes
        std::int64_t cursor {};
        bool masterSelected{};
        bool detailsFocus{};
        std::set<std::uint64_t> automationRows;
        std::map<std::uint64_t,std::string> shownAutomation;
        std::uint64_t note{};
        int midiOctave{4};
        int midiGrid{4};
        std::optional<Session> controlBefore;
        bool controlChanged{};
        engine::foundry::Note noteOriginal;
        std::int64_t noteGrab{};
        bool noteResize{};
        std::unique_ptr<CinemaVideo> video;
        std::uint64_t videoRevision{};
        unsigned long long videoTexture{};
        juce::String videoTextureKey;
        std::uint64_t activeGroup{};
        std::set<std::uint64_t> collapsedGroups;
        double cinemaVisibleSeconds{15.0};
        bool cinemaSnap{true};
        int cinemaGridFrames{1};
        bool cinemaFilmstrip{true};
        std::unique_ptr<CinemaVideo> thumbnailDecoder;
        struct Thumbnail { unsigned long long texture{}; juce::String key; std::uint64_t used{}; float aspect{16.f/9}; };
        std::map<std::int64_t,Thumbnail> thumbnails;
        juce::String thumbnailSource;
        std::int64_t thumbnailPending{-1};
        std::uint64_t thumbnailClock{};
        bool cinemaContentsVisible{true};
        std::uint64_t cinemaFocusTrack{};
        std::uint64_t clip {};      // a clip picked (its header clicked)
        double viewStart {};
        double framesPerPixel {};
        float amplitude { 1.0f };
        float laneZoom { 1.0f };
        float laneScroll {};
        // The analysis of what was selected.
        std::optional<engine::foundry::Statistics> statistics;
        std::vector<float> spectrum;
        std::shared_ptr<const Session> measuredSession;
        std::int64_t measuredStart { -1 };
        std::int64_t measuredEnd { -1 };
        std::vector<bool> measuredTracks;
        double selectionChanged {};
        // The overview, by revision and width.
        std::vector<std::pair<float, float>> overview;
        std::shared_ptr<const Session> overviewSession;
        int overviewWidth {};
    };
    enum class Drag { none, select, pencil, overview, move, trimLeft, trimRight, fadeIn, fadeOut };
    struct Hit {
        int track { -1 };
        int clip { -1 };
        Drag part { Drag::none };
    };

    [[nodiscard]] Tab* tab();
    [[nodiscard]] const Session& shown(const Tab& tab) const;
    void syncProcessing(Tab& tab);
    void commitProcessing(Tab& tab, bool force = false);
    void drawClipInspector(Tab& tab);
    void drawMidiEditor(Tab& tab);
    void finishControlGesture(Tab& tab);
    void drawAutomation(Tab& tab, int track, ImVec2 min, ImVec2 max);
    void reverseClip(Tab& tab);
    void createMidiClip(Tab& tab, int track, std::int64_t at);
    void trackMenu(Tab& tab);
    float processingHeight_ { 300.0f };
    void drawTabs();
    void drawToolbar(Tab& tab);
    void drawOverview(Tab& tab, ImVec2 size);
    void drawRuler(Tab& tab, ImVec2 size);
    void drawTracks(Tab& tab, ImVec2 size);
    void drawTrackHeader(Tab& tab, int track, ImVec2 origin, ImVec2 size);
    void drawClip(const Tab& tab, const engine::foundry::Clip& clip, int track, ImVec2 rowMin, ImVec2 rowMax, float left,
                  float right, ImU32 colour);
    void clipMenu(Tab& tab);
    void drawStatus(Tab& tab);
    void drawSide(Tab& tab, ImVec2 size);
    void drawActions(Tab& tab, ImVec2 size);
    void showMidiEditor(Tab& tab);
    void drawProcesses(Tab& tab);
    void drawMarkers(Tab& tab);
    void drawAnalysis(Tab& tab);
    void handleKeys(Tab& tab);
    void fit(Tab& tab, float width);
    void follow(Tab& tab, float width);
    [[nodiscard]] Hit hitAt(const Tab& tab, ImVec2 mouse) const;

    // What an edit or a process works on: the selection's stretch (or the
    // whole session) on the tracks picked (or all of them).
    [[nodiscard]] std::pair<std::int64_t, std::int64_t> target(const Tab& tab) const;
    [[nodiscard]] engine::foundry::TrackSet picked(const Tab& tab) const;
    [[nodiscard]] bool hasSelection(const Tab& tab) const;
    [[nodiscard]] bool allTracks(const Tab& tab) const;
    void select(Tab& tab, std::int64_t start, std::int64_t end);
    void pickTrack(Tab& tab, int track, bool add);
    void edit(Tab& tab, Session next, const juce::String& label);
    void edit(Tab& tab, Session next, const juce::String& label, std::int64_t at, std::int64_t removed, std::int64_t inserted);
    void cut(Tab& tab);
    void copy(Tab& tab);
    void paste(Tab& tab, bool mix);
    void remove(Tab& tab);
    void crop(Tab& tab);
    void splitAtCursor(Tab& tab);
    void duplicateClip(Tab& tab);
    void addTrack(Tab& tab, bool midi = false);
    void addMarker(Tab& tab, bool region);
    bool importFile(Tab& tab, const juce::File& file, int track, std::int64_t at);
    void applyProcess(Tab& tab);
    juce::Result applyTrackEffects(Tab& tab, const TrackRef& track, double tailSeconds);
    void sendToStudio(Tab& tab, bool eachTrack);
    void playFrom(Tab& tab);
    void stop();
    void measure(Tab& tab, bool force);
    void closeTab(int index);
    void report(const juce::String& message, bool error);
    [[nodiscard]] juce::var tabJson(const Tab& tab) const;

    app::FoundryPlayer& player() const;
    void ensureCinema(Tab& tab);
    void drawCinema();
    void drawCinemaTracks(Tab& tab, ImVec2 size);
    void drawCinemaToolbar(Tab&);
    void drawCinemaFilmstrip(Tab&, ImVec2, ImVec2);
    void releaseCinemaThumbnails(Tab&);
    void zoomCinema(Tab&, double factor, double fraction = 0.5);
    void fitCinema(Tab&);
    void drawCinemaVideo(Tab& tab, ImVec2 size);
    void drawCinemaSubtracks(Tab& tab);
    void drawCinemaClipLane(Tab&,int,ImVec2,ImVec2,float,float);
    void fitCinemaContainers(Session&) const;
    void focusCinemaTrack(Tab&,std::uint64_t);
    bool changeCinemaContainer(Tab&,std::uint64_t,std::int64_t,std::int64_t,bool);
    void chooseCinemaVideo();
    void chooseCinemaClipAudio(Tab&,std::uint64_t track,std::int64_t at);
    void createCinemaAudioClip(Tab&,int track,std::int64_t at);
    void importCinemaAudio(bool automatic = false);
    void pollCinemaAudio();
    bool openCinemaVideo(const juce::File& file);
    bool dropCinemaFiles(const juce::StringArray&,float x,float y);
    std::uint64_t addCinemaGroup(Tab&,const juce::String& name);
    std::uint64_t addCinemaTrack(Tab&,std::uint64_t group,bool midi,const juce::String& name = {});
    bool assignCinemaTrack(Tab&,std::uint64_t id,std::uint64_t group);
    juce::var cinemaCommand(const juce::var&);
    struct CinemaAudioTask {
        juce::File source,target;
        juce::Uuid documentIdentity;
        std::uint64_t group{};
        juce::String error;
        std::atomic<bool> done{};
        bool consumed{},hasAudio{},automatic{};
    };
    juce::ThreadPool cinemaAudioWorker_{1};
    std::shared_ptr<CinemaAudioTask> cinemaAudioTask_;
    std::vector<std::shared_ptr<CinemaAudioTask>> cinemaAudioTasks_;
    bool cinema_{};
    bool cinemaVideoVisible_{true};
    bool cinemaFollow_{true};
    bool cinemaFollowPaused_{};
    bool cinemaResetLayout_{};
    ImVec2 cinemaStripMin_{}, cinemaStripMax_{};
    ImVec2 cinemaNavigatorMin_{}, cinemaNavigatorMax_{};
    struct CinemaDropTarget { ImVec2 min,max; int track{}; double framesPerPixel{}; float timelineX{},timelineWidth{}; };
    struct CinemaBusTarget { ImVec2 min,max; std::uint64_t id{}; };
    std::vector<CinemaBusTarget> cinemaBusTargets_;
    std::vector<CinemaBusTarget> cinemaListTargets_;
    std::vector<CinemaDropTarget> cinemaDropTargets_;
    juce::var cinemaPending_;
    std::optional<Session> cinemaGestureBefore_;
    Clip cinemaGestureClip_;
    int cinemaGestureTrack_{-1};
    float cinemaGestureX_{};
    int cinemaGestureKind_{};
    struct CinemaContainerGesture {
        Session before; engine::foundry::Track original; juce::Uuid document;
        float x{}; int kind{}; std::uint64_t destination{};
    };
    std::optional<CinemaContainerGesture> cinemaContainerGesture_;
    Studio& studio_;
    UiState& state_;
    ImGuiHost& host_;
    std::vector<Tab> tabs_;
    int current_ { -1 };
    int shownTab_ { -1 };
    int barSelected_ { -1 };
    std::optional<Session> clipboard_;
    // Playback.
    int playingTab_ { -1 };
    bool loop_ {};
    // Gestures: what is dragged, the session as it would be (drawn until let go).
    Drag drag_ { Drag::none };
    std::int64_t dragFrom_ {};
    int dragTrack_ { -1 };
    int dragLane_ { -1 };
    std::uint64_t dragClip_ {};
    engine::foundry::Clip dragOriginal_ {};
    std::optional<Session> preview_;
    std::int64_t lastPencil_ { -1 };
    float lastPencilValue_ {};
    bool pencil_ {};
    // Where the tracks were drawn (for files dropped from the desktop).
    ImVec2 lanesMin_ {};
    ImVec2 lanesMax_ {};
    std::int64_t newDocuments_ {};
    // The processes panel.
    int process_ {};
    float gainDb_ { 3.0f };
    float normaliseDb_ { -1.0f };
    bool normaliseRms_ {};
    int fadeCurve_ {};
    float silenceSeconds_ { 1.0f };
    float speedPercent_ { 100.0f };
    float pitchSemitones_ {};
    int stretchAlgorithm_ { 1 };
    int newRate_ { 48'000 };
    int channelsAction_ {};
    int effectsTrack_ { -1 };
    float tailSeconds_ { 2.0f };
    int saveBits_ { 24 };
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace cydaw::v2
