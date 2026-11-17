#include <algorithm>
#include <array>
#include <cydaw/midi/StandardMidiFile.h>
namespace cydaw::midi {
base::Result loadStandardFile(const base::File &file,
                              cyaudio::MidiFile &target) {
  if (file.getSize() <= 0 || file.getSize() > 256 * 1024 * 1024)
    return base::Result::fail("Invalid MIDI file size");
  base::MemoryBlock bytes;
  if (!file.loadFileAsData(bytes))
    return base::Result::fail("Cannot read MIDI file");
  cyaudio::MidiFile next;
  const auto status = next.parse(
      {static_cast<const std::uint8_t *>(bytes.getData()), bytes.getSize()});
  if (status != cyaudio::Status::ok)
    return base::Result::fail("Invalid standard MIDI file");
  target = std::move(next);
  return base::Result::ok();
}
base::Result saveStandardFile(const base::File &file, cyaudio::MidiFile data) {
  for (auto &track : data.tracks)
    std::stable_sort(
        track.begin(), track.end(),
        [](const auto &a, const auto &b) { return a.tick < b.tick; });
  std::vector<std::uint8_t> bytes;
  if (data.serialize(bytes) != cyaudio::Status::ok)
    return base::Result::fail("Cannot encode standard MIDI file");
  return file.replaceWithData(bytes.data(), bytes.size())
             ? base::Result::ok()
             : base::Result::fail("Cannot write MIDI file");
}
base::Text trackName(std::span<const cyaudio::MidiFileEvent> track) {
  for (const auto &event : track)
    if (event.kind == cyaudio::MidiFileEventKind::meta && event.type == 3) {
      auto name = base::Text::fromUTF8(
                      reinterpret_cast<const char *>(event.data.data()),
                      int(event.data.size()))
                      .trim();
      if (name.isNotEmpty())
        return name;
    }
  return {};
}
std::optional<double> tempo(const cyaudio::MidiFileEvent &event) {
  if (event.kind != cyaudio::MidiFileEventKind::meta || event.type != 0x51 ||
      event.data.size() != 3)
    return {};
  const unsigned micros = (unsigned(event.data[0]) << 16) |
                          (unsigned(event.data[1]) << 8) | event.data[2];
  return micros ? std::optional<double>(60000000. / micros) : std::nullopt;
}
std::optional<std::pair<unsigned, unsigned>>
signature(const cyaudio::MidiFileEvent &event) {
  if (event.kind != cyaudio::MidiFileEventKind::meta || event.type != 0x58 ||
      event.data.size() < 2 || event.data[1] > 7 || !event.data[0])
    return {};
  return std::pair<unsigned, unsigned>{event.data[0], 1u << event.data[1]};
}
std::vector<FileNote>
pairedNotes(std::span<const cyaudio::MidiFileEvent> track) {
  std::array<std::array<std::optional<std::size_t>, 128>, 16> active{};
  std::vector<FileNote> result;
  for (const auto &event : track) {
    if (event.kind != cyaudio::MidiFileEventKind::message)
      continue;
    const cyaudio::MidiMessageView message(event.data);
    if (!message.valid() || (!message.noteOn() && !message.noteOff()))
      continue;
    auto &current = active[message.channel() - 1][message.note()];
    if (current) {
      result[*current].end = event.tick;
      current.reset();
    }
    if (message.noteOn()) {
      current = result.size();
      result.push_back(
          {event.tick, {}, message.channel(), message.note(), event.data[2]});
    }
  }
  return result;
}
} // namespace cydaw::midi
