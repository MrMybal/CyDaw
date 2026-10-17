#pragma once

#include "StudioTypes.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

// What a controller can move in CyDAW, named by a path (also its OSC address
// under /cydaw/):
//
//   transport/play, transport/stop, transport/record, transport/loop, transport/tempo
//   track/selected/<what>, track/<track key>/<what>      what: volume, pan, mute, solo, arm
//   master/volume, master/pan
//   param/<track key or master>/<binding>                 a card's knob, as its lane names it
//   dj/<A..D>/<what>                                       play, cue, sync, gain, high, mid, low, filter,
//                                                          fader, killHigh, killMid, killLow, pfl
//   dj/crossfader, dj/master
//
// A track key is Studio::routingKey(): it stays while tracks move and in
// the saved project. Values are 0..1 over the target's course.

namespace cydaw::v2 {

class Studio;
struct UiState;

namespace control_targets {

[[nodiscard]] std::string forTrack(std::uint64_t trackKey, const char* what);
[[nodiscard]] std::string forMaster(const char* what);
[[nodiscard]] std::string forParameter(std::uint64_t trackKey, const juce::String& binding);
[[nodiscard]] std::string forDeck(int deck, const char* what);
[[nodiscard]] std::string forTransport(const char* what);
[[nodiscard]] std::string forDj(const char* what);
// A track's mix control in learn mode: its target and its name (both empty
// for what is not a track of the project: a bus of the software, a pad).
[[nodiscard]] std::pair<std::string, std::string> forTrackLearn(const Studio& studio, const TrackRef& track, const char* what,
                                                                const juce::String& label);

} // namespace control_targets

class ControlTargets final {
public:
    ControlTargets(Studio& studio, UiState& state);

    enum class Kind {
        continuous, // goes where the control is
        toggle,     // on or off: a button flips it, anything else sets it
        trigger,    // happens at a press
        hold,       // on while the control is pressed (a deck's CUE)
    };
    struct Info {
        juce::String label;
        Kind kind { Kind::continuous };
        // Kept with the project (a track's or a card's); else with the
        // controllers, for every project.
        bool project {};
    };
    [[nodiscard]] std::optional<Info> describe(const std::string& key) const;
    // Where it is now, 0..1 (none: it does not exist now).
    [[nodiscard]] std::optional<double> value(const std::string& key) const;
    // A control's order: `value` 0..1, and whether it was just pressed or
    // let go; `momentary` for a button or a pad.
    bool apply(const std::string& key, double value, bool pressed, bool released, bool momentary);
    // Every target there is now (the transport, the tracks, their cards'
    // knobs, the decks), for a list to pick from.
    [[nodiscard]] std::vector<std::string> list() const;

private:
    [[nodiscard]] std::optional<TrackRef> trackOf(const std::string& part) const;
    Studio& studio_;
    UiState& state_;
};

} // namespace cydaw::v2
