#include <cydaw/base/Text.h>
#pragma once

#include "Studio.h"

#include <cstdint>

#include <imgui.h>

// A track's inputs and outputs, as the inspector, the routing panel, the
// track's menu and the mixer strip's output button show them: where its
// sound goes (the master, other tracks, buses, one of their inputs, an
// output of the sound card), each at a level, before or after its fader,
// what reaches it, its format and its channels' levels.

namespace cydaw::v2::routing_ui {

// "Master", the destination track's name, or "Bus › Sidechain" for one of
// its other inputs.
[[nodiscard]] cydaw::base::Text destinationName(const Studio& studio, std::uint64_t key, std::uint64_t input = 0);
// An output of the sound card: "Carte › Main Out (1-2)".
[[nodiscard]] cydaw::base::Text hardwareName(const Studio& studio, std::uint64_t busId);
// The sound card's channels a bus uses, from 1: "1-2", "3-6", "1,4".
[[nodiscard]] cydaw::base::Text deviceChannelsText(const app::VirtualAudioBus& bus);
// Where one output goes, whichever kind.
[[nodiscard]] cydaw::base::Text outputName(const Studio& studio, const TrackOutput& output);
// Where its main output goes, short ("→ Bus FX"), for a strip.
[[nodiscard]] cydaw::base::Text mainOutputLabel(const Studio& studio, const TrackRef& track);
// Its outputs as rows (destination, before the fader, level, remove) and a
// button for another; a change is applied at once. `labelWidth` lines the
// rows up with the panel's own labels. Why a change was refused goes to
// `error`.
void outputsEditor(Studio& studio, const TrackRef& track, float labelWidth, cydaw::base::Text& error);
// One output's destination picked in a combo of `width`, grouped under
// headings (the master, the card's outputs, the buses, the other tracks and
// their other inputs); true when it changed.
bool destinationCombo(const char* id, const Studio& studio, const TrackRef& track, TrackOutput& output, float width);
// Its main output's destination alone, as a combo of `width`.
void mainOutputCombo(Studio& studio, const TrackRef& track, float width, cydaw::base::Text& error);
// Its extra audio inputs (a sidechain, a second source), named and
// removable, and a button for another.
void inputsEditor(Studio& studio, const TrackRef& track, float labelWidth, cydaw::base::Text& error);
// The tracks routed into it, as one line (with the input when not the main).
void sendersLine(const Studio& studio, const TrackRef& track, float labelWidth);
// The same names, comma separated.
[[nodiscard]] cydaw::base::Text sendersText(const Studio& studio, const TrackRef& track);
// A submenu's items: the main output's destination picked directly.
void mainOutputItems(Studio& studio, const TrackRef& track, cydaw::base::Text& error);
// Its format, mono to 7.1, as a combo (an audio track's; the others say
// stereo).
void formatCombo(Studio& studio, const TrackRef& track, float width, cydaw::base::Text& error);
// The input bus an audio track records from, as a combo, with its format.
void recordInputCombo(Studio& studio, const TrackRef& track, float width);
// A hybrid instrument track's audio input (its audio part's): none at
// first (its transport recording takes its notes alone), or an input it
// then records with them.
void hybridAudioInputCombo(Studio& studio, const TrackRef& owner, const TrackRef& audioPart, float width);
// An instrument track's MIDI input: its name (every input, the computer
// keyboard, a device), what its light follows (its messages and the track's
// clips' notes, a counter), and the combo picking it, one choice, its light
// in it (`width` as SetNextItemWidth takes it).
[[nodiscard]] cydaw::base::Text midiInputName(const cydaw::base::Text& input);
[[nodiscard]] std::uint32_t midiInputActivity(const Studio& studio, const TrackRef& track, const cydaw::base::Text& input);
void midiInputCombo(Studio& studio, const TrackRef& track, float width);
// Its channels' levels side by side, each over its speaker's name.
void channelMeters(const Studio& studio, const TrackRef& track, ImVec2 size);

} // namespace cydaw::v2::routing_ui
