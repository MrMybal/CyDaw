#pragma once

#include "StudioTypes.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <algorithm>
#include <vector>

// What the panels share: the selection, the views' zoom and scroll, the
// clip shown in the editor, the waveforms already read.

namespace cydaw::v2 {

struct ClipRef {
    TrackRef track;
    std::uint64_t clipId {};

    [[nodiscard]] bool operator==(const ClipRef& other) const noexcept
    {
        return track == other.track && clipId == other.clipId;
    }
};

// Drag-and-drop payload carrying a file path between panels.
inline constexpr const char* kFilePayload = "CYDAW_FILE";

enum class Panel { none, arrangement, launcher, devices, graph, mixer, editor, browser, inspector, workshop, routing, dj,
                   foundry, cinema };

// What the whole window is for: the studio (tracks, arrangement, launcher,
// their panels), the DJ mix, the Foundry (sound files edited), the VST
// workshop. The modes but the studio take the whole window.
enum class AppMode { studio, dj, foundry, workshop, cinema };

// What the device workshop is asked to open (a track's device, a file of My
// devices, a new device). It asks first when the device it holds is not
// saved.
struct WorkshopRequest {
    enum class Kind { trackDevice, file, newEffect, newInstrument };
    Kind kind { Kind::trackDevice };
    TrackRef track;
    std::uint64_t nodeId {}; // 0 with a MIDI track: its instrument
    juce::File file;
    int inputChannels { 2 };  // a new effect's widths
    int outputChannels { 2 };
};

// A device's display shown large in a window of its own: the track, the
// card, which of its displays.
struct ExpandedPane {
    TrackRef track;
    std::uint64_t nodeId {};
    // The display shown large; -1: all of them at once (a device with
    // several opens on that).
    int pane {};
    // Asked once: out of the studio's window, a window of its own on the
    // desktop (as a plug-in's).
    bool detach {};
    // Asked once: to the front.
    bool focus { true };
};

// A device's window, as a plugin's: the card of a graph (0: the track's
// whole graph, an instrument's).
struct PluginWindow {
    TrackRef track;
    std::uint64_t nodeId {};
    [[nodiscard]] bool operator==(const PluginWindow& other) const noexcept
    {
        return track == other.track && nodeId == other.nodeId;
    }
};

// A right click asking for a menu: a track's, or a launcher slot's.
struct ContextRequest {
    // outputs: a track's outputs and sends, to edit.
    enum class Kind { track, launcherSlot, outputs };
    Kind kind { Kind::track };
    TrackRef track;
    int scene { -1 };
};

// A slot of the launcher: a track's cell in a scene (a clip or empty).
struct SlotRef {
    TrackRef track;
    int scene {};
    [[nodiscard]] bool operator==(const SlotRef& other) const noexcept
    {
        return track == other.track && scene == other.scene;
    }
};

struct UiState {
    // The library's loops heard at the project's tempo (stretched, in step
    // with the transport), not as they are.
    bool auditionAtTempo { true };
    std::optional<TrackRef> selectedTrack;
    // The cards whose list of values is open (the track's routing key, the
    // card's node id).
    std::set<std::pair<std::uint64_t, std::uint64_t>> valueLists;
    // The same for a plug-in's card (the track's routing key, its place).
    std::set<std::pair<std::uint64_t, std::size_t>> pluginValueLists;
    // The cards whose modulators show, a column at their left (the same
    // keys), each one's modulator whose settings show, and the modulator
    // whose arrow is armed (the track's key, its node): a knob's drag then
    // sets its amount instead of turning the knob.
    std::set<std::pair<std::uint64_t, std::uint64_t>> modulatorColumns;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> shownModulator;
    std::optional<std::pair<std::uint64_t, std::uint64_t>> armedModulator;
    // Each container's branch shown after it (the track's key, its head).
    std::map<std::pair<std::uint64_t, std::uint64_t>, int> containerBranch;
    std::vector<ClipRef> selectedClips;
    // The launcher's slots picked (a click; Ctrl or Shift adds): what its
    // menu and Ctrl+E act on.
    std::vector<SlotRef> selectedSlots;
    [[nodiscard]] bool isSlotSelected(const SlotRef& slot) const
    {
        return std::find(selectedSlots.begin(), selectedSlots.end(), slot) != selectedSlots.end();
    }
    std::optional<ClipRef> editedClip;
    // The master's launcher clip open in the editor (its scene), when no
    // track's clip is: the one opened last wins.
    std::optional<int> editedMasterScene;
    // The audio editor's face asked for (0 sample, 1 takes, 2 stretch; -1
    // none): taken by the editor at its next frame.
    int audioEditorFace { -1 };
    Panel focused { Panel::arrangement };
    std::optional<WorkshopRequest> workshopRequest;
    std::optional<ContextRequest> contextRequest;
    // The window's mode. The Plugin workshop: the whole window builds devices
    // (graph, its tools, the plugin export); no tracks, no arrangement.
    AppMode mode { AppMode::studio };
    // Kept separately for Studio, DJ and Foundry; the workshop has its own layout.
    std::array<bool, 5> bottomPanelsVisible { true, true, true, true, true };
    [[nodiscard]] bool bottomPanelVisible() const noexcept
    {
        return mode != AppMode::workshop && bottomPanelsVisible[static_cast<std::size_t>(mode)];
    }
    void setBottomPanelVisible(bool visible) noexcept
    {
        if (mode != AppMode::workshop)
            bottomPanelsVisible[static_cast<std::size_t>(mode)] = visible;
    }
    static bool isBottomPanel(Panel panel) noexcept
    {
        return panel == Panel::devices || panel == Panel::graph || panel == Panel::mixer
            || panel == Panel::routing || panel == Panel::editor;
    }
    [[nodiscard]] bool inWorkshop() const noexcept { return mode == AppMode::workshop; }
    // A sound file asked open in the Foundry; a file asked loaded on a deck
    // of the DJ mix (-1: the first free).
    std::optional<juce::File> foundryOpen;
    std::optional<std::pair<int, juce::File>> djLoad;
    // Asks a panel to come to the front (set by one panel, honoured by the
    // frame).
    Panel bringToFront { Panel::none };
    // The audio settings asked open on the card's inputs and outputs, or
    // closed (a bus's effects picked from them, to edit in the panels).
    bool openSoundCard {};
    bool closeAudioSettings {};
    // The sound banks' window shown; the locations asked open.
    bool showBanks {};
    bool showJsfx {};
    juce::File jsfxFileToOpen;
    // The colour themes' window.
    bool showThemes {};
    // Several instruments' windows open at once (one each), or one at a
    // time (opening another closes the last): Options, kept in the settings.
    bool manyInstrumentWindows { true };
    std::string deviceWindowNamespace;
    // A rack's pad opened in the device panel (its instrument and effects):
    // its track, its index.
    std::optional<std::pair<TrackRef, int>> openRackPad;
    bool openLocations {};
    // The semantic sound search's window (its models, licences, index).
    bool showSemantic {};
    // The library's big window: shown, its tab (0 sounds, 1 instruments, 2
    // effects and devices), the track a pick goes to (the device strip's
    // "+", which also closes it once something is added), brought to the
    // front, taken out of the studio's window.
    bool showLibrary {};
    // The graph editor shows the selected track's modulation graph, not its
    // sound's.
    bool graphShowsModulation {};
    int libraryTab {};
    std::optional<TrackRef> libraryTarget;
    bool libraryPick {};
    bool libraryFocus {};
    bool libraryDetach {};
    // The devices' displays open large, each in a window of its own: several
    // at once, as plug-ins' windows.
    std::vector<ExpandedPane> expandedPanes;
    // Opens one (or brings it to the front, on that display).
    void openExpandedPane(const ExpandedPane& wanted)
    {
        for (auto& open : expandedPanes)
            if (open.track == wanted.track && open.nodeId == wanted.nodeId) {
                open.pane = wanted.pane;
                open.focus = true;
                open.detach = open.detach || wanted.detach;
                return;
            }
        expandedPanes.push_back(wanted);
    }
    [[nodiscard]] bool expandedPaneOpen(const TrackRef& track, const std::uint64_t nodeId) const
    {
        return std::any_of(expandedPanes.begin(), expandedPanes.end(),
                           [&](const ExpandedPane& open) { return open.track == track && open.nodeId == nodeId; });
    }
    void closeExpandedPane(const TrackRef& track, const std::uint64_t nodeId)
    {
        std::erase_if(expandedPanes, [&](const ExpandedPane& open) { return open.track == track && open.nodeId == nodeId; });
    }
    // The sampler pane drawn last: files dropped from the system on it are
    // added to its card.
    struct SamplerDrop {
        TrackRef track;
        std::uint64_t nodeId {};
        float x0 {};
        float y0 {};
        float x1 {};
        float y1 {};
    };
    std::optional<SamplerDrop> samplerDrop;

    // Arrangement.
    double pixelsPerSecond { 48.0 };
    // The arrangement's vertical zoom: every track's height times this.
    float laneZoom { 1.0f };
    double scrollSeconds {};
    float scrollY {};
    double snapBeats { 1.0 };   // 0: off
    bool followPlayhead { true };

    // Status line: the last thing the interface has to say.
    juce::String status;
    bool statusIsError {};
    double statusTime {};

    [[nodiscard]] bool isSelected(const ClipRef& clip) const
    {
        return std::find(selectedClips.begin(), selectedClips.end(), clip) != selectedClips.end();
    }
    void selectTrack(const TrackRef& track)
    {
        selectedTrack = track;
    }
    // The device panel's catalogue asked open (the control port).
    bool openDeviceCatalogue {};
    bool inlineDeviceCatalogue {};
    // The devices' windows open (PluginWindows draws them).
    std::vector<PluginWindow> pluginWindows;
    void togglePluginWindow(const PluginWindow& window)
    {
        if (const auto found = std::find(pluginWindows.begin(), pluginWindows.end(), window); found != pluginWindows.end())
            pluginWindows.erase(found);
        else
            pluginWindows.push_back(window);
    }
    [[nodiscard]] bool pluginWindowOpen(const PluginWindow& window) const
    {
        return std::find(pluginWindows.begin(), pluginWindows.end(), window) != pluginWindows.end();
    }
};

// Peaks of audio files, read in the background and drawn per pixel.
class WaveformCache final {
public:
    explicit WaveformCache(juce::AudioFormatManager& formats);
    ~WaveformCache();

    [[nodiscard]] juce::AudioThumbnail* get(const juce::File& file);
    [[nodiscard]] bool anyLoading() const;
    void clear();

private:
    juce::AudioFormatManager& formats_;
    juce::AudioThumbnailCache cache_ { 256 };
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbnails_;
};

} // namespace cydaw::v2
