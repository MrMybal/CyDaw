#pragma once
#include <cyaudio/midi/Midi.h>
#include <cydaw/base/File.h>
#include <optional>
namespace cydaw::midi {
struct FileNote {
  std::uint64_t tick{};
  std::optional<std::uint64_t> end;
  unsigned channel{}, key{}, velocity{};
};
base::Result loadStandardFile(const base::File &, cyaudio::MidiFile &);
base::Result saveStandardFile(const base::File &, cyaudio::MidiFile);
base::Text trackName(std::span<const cyaudio::MidiFileEvent>);
std::optional<double> tempo(const cyaudio::MidiFileEvent &); // BPM
std::optional<std::pair<unsigned, unsigned>>
signature(const cyaudio::MidiFileEvent &);
std::vector<FileNote> pairedNotes(std::span<const cyaudio::MidiFileEvent>);
} // namespace cydaw::midi
