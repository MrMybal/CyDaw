#include <cydaw/midi/StandardMidiFile.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool okay, const char *message) {
  if (!okay)
    throw std::runtime_error(message);
}
int main() {
  try {
    // Independent format-0 SMF bytes, 480 PPQN, 120 BPM, track name and running
    // status.
    const std::uint8_t fixture[] = {
        'M', 'T',  'h',  'd',  0,    0,    0,    6,  0,    0,    0,
        1,   1,    0xe0, 'M',  'T',  'r',  'k',  0,  0,    0,    39,
        0,   0xff, 0x51, 3,    7,    0xa1, 0x20, 0,  0xff, 0x58, 4,
        4,   2,    24,   8,    0,    0xff, 3,    4,  'T',  'e',  's',
        't', 0,    0x91, 60,   100,  0x81, 0x70, 60, 90,   0x81, 0x70,
        60,  0,    0,    0xff, 0x2f, 0};
    auto file = base::File::createTempFile(".mid");
    require(file.replaceWithData(fixture, sizeof(fixture)),
            "write independent SMF fixture");
    cyaudio::MidiFile midi;
    auto loaded = cydaw::midi::loadStandardFile(file, midi);
    require(loaded.wasOk(), "Core decodes running-status SMF fixture");
    require(midi.division == 480 && midi.tracks.size() == 1,
            "SMF time division and track count");
    require(cydaw::midi::trackName(midi.tracks[0]) == "Test", "SMF track name");
    require(cydaw::midi::tempo(midi.tracks[0][0]) == 120., "SMF tempo");
    require(cydaw::midi::signature(midi.tracks[0][1]) ==
                std::pair<unsigned, unsigned>{4, 4},
            "SMF signature");
    auto notes = cydaw::midi::pairedNotes(midi.tracks[0]);
    require(notes.size() == 2 && notes[0].tick == 0 && notes[0].end == 240 &&
                notes[1].tick == 240 && notes[1].end == 480 &&
                notes[0].channel == 2 && notes[0].velocity == 100,
            "overlap and zero velocity note-off pairing");
    auto saved = base::File::createTempFile(".mid");
    require(cydaw::midi::saveStandardFile(saved, midi).wasOk(),
            "Core serializes SMF");
    cyaudio::MidiFile roundtrip;
    require(cydaw::midi::loadStandardFile(saved, roundtrip).wasOk() &&
                roundtrip.tracks[0].size() == midi.tracks[0].size(),
            "Core SMF roundtrip");
    require(file.replaceWithText("invalid"), "invalid fixture");
    require(cydaw::midi::loadStandardFile(file, roundtrip).failed() &&
                roundtrip.division == 480 && roundtrip.tracks.size() == 1,
            "invalid SMF preserves previous document");
    file.deleteFile();
    saved.deleteFile();
    std::cout << "Native SMF import, metadata, overlap and export passed "
                 "without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
