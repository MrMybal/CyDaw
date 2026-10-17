#pragma once

#include "Studio.h"
#include "Vst3Build.h"

#include <juce_core/juce_core.h>

#include <optional>

// The control port's commands for CyDAW 2: the same verbs, parameters and
// answers as CyDAW 1 (src/app/WorkspaceComponentRemote.cpp), so
// tools/mcp/cydaw_mcp_server.py drives either interface. Called on the
// message thread (app::RemoteControlServer's handler); every answer is
// {ok: true, result: …} or {ok: false, error: "…"}.

namespace cydaw::v2 {

// What the commands need from the interface; AppUi implements it.
class RemoteUi {
public:
    virtual ~RemoteUi() = default;

    // set_view, and record (MIDI) which shows the clip editor. CyDAW 1's
    // view numbers: 1 arrangement, 2 clip editor, 4 launcher, 6 clip editor
    // of a launcher clip, 7 graph, 8 buses, 9 mixer, 10 samples (3 and 5 are
    // older modes). Brings the matching panel to the front.
    virtual void showView(int view) = 0;
    // The view last shown or asked for, as a CyDAW 1 number (status, and
    // set_view's answer right after showView).
    [[nodiscard]] virtual int currentView() const = 0;
    // snapshot: renders the window now — after any panel change still
    // pending — and writes it as a PNG before returning. False when nothing
    // was written.
    virtual bool snapshot(const juce::File& png) = 0;
    // The whole session was replaced (new_project, open_project,
    // load_template, import_project): drop selections, edited clips, caches.
    virtual void sessionReplaced() = 0;
    // Tracks were removed or reordered, so audio track indices moved: drop
    // clip selections and edited clips that point at tracks. A selectTrack()
    // call may follow.
    virtual void tracksChanged() = 0;
    // quit: quit soon, after this answer has gone out (post it; never quit
    // inside the call). No unsaved-changes prompt.
    virtual void requestQuit() = 0;
    // The selected track (the one whose graph the editors show).
    [[nodiscard]] virtual std::optional<TrackRef> selectedTrack() const = 0;
    // Selects a track, as clicking it does; the commands that edit a
    // track's graph select it, as CyDAW 1 did.
    virtual void selectTrack(const TrackRef& track) = 0;
    // select_sample: the sample browser in front, this file selected.
    virtual void selectSample(const juce::File& file) = 0;
    // browse_samples: the sample browser in front, showing a search (by name,
    // or by description when `semantic`) and the tree's filter.
    virtual void browseSamples(const juce::String& query, bool semantic, const juce::String& family, const juce::String& category,
                               const juce::String& source)
    {
        juce::ignoreUnused(query, semantic, family, category, source);
    }
    // library_window: the library's big window shown, set, and asked what it
    // shows; library_perf: the sample catalog's speed measured on as many
    // sounds as asked. Null when there is no interface.
    virtual juce::var library(const juce::String& command, const juce::var& request)
    {
        juce::ignoreUnused(command, request);
        return {};
    }
    // computer_keyboard: the computer's keys as a MIDI keyboard, read or set
    // {enabled, octave, velocity}; midi_inputs: the MIDI devices, read or one
    // switched {device, enabled}. Null when there is no interface.
    // theme: the colour themes, and the one on; {name or key} puts one on,
    // {create, from} makes one of the person's from another, {key, colours}
    // changes its colours ("#RRGGBB" by palette name).
    virtual juce::var themes(const juce::var& request)
    {
        juce::ignoreUnused(request);
        return {};
    }
    // dj: the Mix DJ mode's decks, mixer and collection; foundry: the
    // Foundry's sound files. Their answer, or a string saying what failed;
    // null when there is no interface.
    virtual juce::var jsfx(const juce::var&) { return {}; }
    virtual juce::var cinema(const juce::var&) { return {}; }
    virtual juce::var transportSync(const juce::var&) { return {}; }
    virtual juce::var controllers(const juce::var&) { return {}; }
    virtual juce::var dj(const juce::var& request)
    {
        juce::ignoreUnused(request);
        return {};
    }
    virtual juce::var foundry(const juce::var& request)
    {
        juce::ignoreUnused(request);
        return {};
    }
    // app_mode: the whole window's mode (studio, dj, foundry, workshop), put
    // on when named; the one on ("" when the name is none of them).
    virtual juce::String appMode(const juce::String& mode)
    {
        juce::ignoreUnused(mode);
        return {};
    }
    virtual juce::var midiInputs(const juce::String& command, const juce::var& request)
    {
        juce::ignoreUnused(command, request);
        return {};
    }
    // ui_input: mouse events played into the interface, one per frame, in
    // the window's pixels (those of a snapshot), without the system's
    // pointer: {"type": "move"|"down"|"up"|"wheel"|"wait"|"release", "x",
    // "y", "button" (0 left, 1 right), "dy", "frames", "shift"}. The
    // scripted pointer stays in place of the real one until "release".
    // file_drop: {files:[absolute paths],x,y}, through the desktop drop handler.
    // Empty on success, otherwise why not.
    virtual juce::String injectInput(const juce::var& events)
    {
        (void) events;
        return "this interface takes no scripted input";
    }
    // set_view with "device": that card of the selected track's graph in
    // the device editor window, its display `pane`; 0 closes the window.
    // False when there is no track to show it for.
    // `detached`: in a window of its own out of the studio's (as a plug-in's).
    virtual bool showDeviceEditor(std::uint64_t nodeId, int pane, bool detached = false)
    {
        (void) nodeId;
        (void) pane;
        (void) detached;
        return false;
    }
    // The device workshop's panel in front.
    virtual void showWorkshop() {}
    // show_dialog: a settings window opened ("audio", "companion").
    virtual bool showDialog(const juce::String& name)
    {
        (void) name;
        return false;
    }
    // set_view with "catalogue": the device panel's catalogue (its +) open.
    virtual void showDeviceCatalogue() {}
    // The graph editor on the selected track's modulation graph, or its sound's.
    virtual void showModulationGraph(bool) {}
    // A rack's pad open in the device panel (its chain), or none.
    virtual void openRackPad(const std::optional<std::pair<TrackRef, int>>&) {}
    // A clip in the editor (an audio clip on a face of its editor: 0 sample,
    // 1 takes, 2 stretch).
    virtual void editClip(const TrackRef& track, std::uint64_t clipId, int face)
    {
        (void) track;
        (void) clipId;
        (void) face;
    }
    // device_window: a device's window, as a plugin's (its designed
    // interface), opened or closed. nodeId 0: the track's whole graph.
    // value_list: a card's list of values, opened or closed.
    virtual bool showValueList(const TrackRef& track, std::uint64_t nodeId, bool open)
    {
        (void) track;
        (void) nodeId;
        (void) open;
        return false;
    }
    // modulator: a card's column of modulators shown (the one whose settings
    // show, 0 to keep), and the modulator armed (0: none).
    virtual bool showModulators(const TrackRef& track, std::uint64_t nodeId, bool open, std::uint64_t shown)
    {
        (void) track;
        (void) nodeId;
        (void) open;
        (void) shown;
        return false;
    }
    // container: which branch of a container shows after its card.
    // A pad's chain (a rack's, a Replacer's instrument) shown in the strip.
    virtual bool openPadChain(const TrackRef& owner, int pad)
    {
        (void) owner;
        (void) pad;
        return false;
    }
    virtual bool showContainerBranch(const TrackRef& track, std::uint64_t head, int branch)
    {
        (void) track;
        (void) head;
        (void) branch;
        return false;
    }
    virtual bool armModulator(const TrackRef& track, std::uint64_t modulator)
    {
        (void) track;
        (void) modulator;
        return false;
    }
    virtual bool showDeviceWindow(const TrackRef& track, std::uint64_t nodeId, bool open)
    {
        (void) track;
        (void) nodeId;
        (void) open;
        return false;
    }
    // layout: the named layouts (the panels, their places, the windows
    // detached onto other screens): their names, one saved as the window
    // is now, one loaded (at the next frame), one deleted.
    [[nodiscard]] virtual juce::StringArray layoutNames() const { return {}; }
    virtual juce::Result saveLayoutAs(const juce::String& name)
    {
        (void) name;
        return juce::Result::fail("this interface has no layouts");
    }
    virtual juce::Result loadLayout(const juce::String& name)
    {
        (void) name;
        return juce::Result::fail("this interface has no layouts");
    }
    virtual bool deleteLayout(const juce::String& name)
    {
        (void) name;
        return false;
    }
    // The VST3 build the workshop runs; none without an interface.
    virtual Vst3Build* vst3Build() { return nullptr; }
    // A project opened as the person opens it: in steps, the interface
    // drawing a bar meanwhile. False when it cannot start.
    virtual bool openProjectInSteps(const juce::File& file)
    {
        (void) file;
        return false;
    }
    // A project of another software imported the same way.
    virtual bool importProjectInSteps(const juce::File& file)
    {
        (void) file;
        return false;
    }
};

// One request ({"cmd": "...", ...}) answered.
[[nodiscard]] juce::var handleRemoteCommand(Studio& studio, RemoteUi& ui, const juce::var& request);

} // namespace cydaw::v2
