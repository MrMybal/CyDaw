#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cyaudio/midi/Midi.h>
#include <cydaw/base/Text.h>
namespace cydaw::midi {
class Message {
  std::array<std::uint8_t, 3> short_{};
  std::vector<std::uint8_t> large_;
  int size_{};
  double timestamp_{};
  int byte(int at) const noexcept {
    return at >= 0 && at < size_ ? getRawData()[at] : 0;
  }

public:
  Message() = default;
  Message(const void *data, int size, double stamp = 0)
      : size_(std::max(0, size)), timestamp_(stamp) {
    if (size_ && !data) {
      size_ = 0;
      return;
    }
    if (size_ <= 3) {
      if (size_)
        std::memcpy(short_.data(), data, std::size_t(size_));
    } else
      large_.assign(static_cast<const std::uint8_t *>(data),
                    static_cast<const std::uint8_t *>(data) + size_);
  }
  explicit Message(cyaudio::ShortMidiMessage message)
      : Message(message.data.data(), int(message.size)) {}
  const std::uint8_t *getRawData() const noexcept {
    return size_ <= 3 ? short_.data() : large_.data();
  }
  int getRawDataSize() const noexcept { return size_; }
  double getTimeStamp() const noexcept { return timestamp_; }
  void setTimeStamp(double value) noexcept { timestamp_ = value; }
  int getChannel() const noexcept {
    return int(
        cyaudio::MidiMessageView({getRawData(), std::size_t(size_)}).channel());
  }
  bool isNoteOn(bool includeZero = false) const noexcept {
    return size_ == 3 && (byte(0) & 0xf0) == 0x90 &&
           (includeZero || byte(2) > 0);
  }
  bool isNoteOff(bool includeZeroOn = true) const noexcept {
    return size_ == 3 &&
           ((byte(0) & 0xf0) == 0x80 ||
            (includeZeroOn && (byte(0) & 0xf0) == 0x90 && byte(2) == 0));
  }
  bool isNoteOnOrOff() const noexcept { return isNoteOn() || isNoteOff(); }
  int getNoteNumber() const noexcept { return byte(1); }
  int getVelocity() const noexcept { return byte(2); }
  float getFloatVelocity() const noexcept { return float(byte(2)) / 127.f; }
  bool isController() const noexcept {
    return size_ == 3 && (byte(0) & 0xf0) == 0xb0;
  }
  int getControllerNumber() const noexcept { return byte(1); }
  int getControllerValue() const noexcept { return byte(2); }
  bool isPitchWheel() const noexcept {
    return size_ == 3 && (byte(0) & 0xf0) == 0xe0;
  }
  int getPitchWheelValue() const noexcept { return byte(1) | (byte(2) << 7); }
  bool isChannelPressure() const noexcept {
    return size_ == 2 && (byte(0) & 0xf0) == 0xd0;
  }
  int getChannelPressureValue() const noexcept { return byte(1); }
  bool isAftertouch() const noexcept {
    return size_ == 3 && (byte(0) & 0xf0) == 0xa0;
  }
  int getAfterTouchValue() const noexcept { return byte(2); }
  bool isAllNotesOff() const noexcept {
    return isController() && byte(1) == 123;
  }
  bool isAllSoundOff() const noexcept {
    return isController() && byte(1) == 120;
  }
  bool isSustainPedalOn() const noexcept {
    return isController() && byte(1) == 64 && byte(2) >= 64;
  }
  bool isSustainPedalOff() const noexcept {
    return isController() && byte(1) == 64 && byte(2) < 64;
  }
  bool isSysEx() const noexcept {
    return size_ >= 2 && byte(0) == 0xf0 && byte(size_ - 1) == 0xf7;
  }
  const std::uint8_t *getSysExData() const noexcept {
    return isSysEx() ? getRawData() + 1 : nullptr;
  }
  int getSysExDataSize() const noexcept { return isSysEx() ? size_ - 2 : 0; }
  static Message noteOn(int channel, int note, int velocity) {
    return Message(cyaudio::midiNoteOn(unsigned(channel), unsigned(note),
                                       unsigned(std::clamp(velocity, 0, 127))));
  }
  static Message noteOn(int channel, int note, float velocity) {
    return noteOn(channel, note,
                  int(std::lround(std::clamp(velocity, 0.f, 1.f) * 127.f)));
  }
  static Message noteOff(int channel, int note, int velocity = 0) {
    return Message(
        cyaudio::midiNoteOff(unsigned(channel), unsigned(note),
                             unsigned(std::clamp(velocity, 0, 127))));
  }
  static Message controllerEvent(int channel, int number, int value) {
    return Message(cyaudio::midiController(unsigned(channel), unsigned(number),
                                           unsigned(value)));
  }
  static Message pitchWheel(int channel, int value) {
    return Message(cyaudio::midiPitchBend(unsigned(channel), unsigned(value)));
  }
  static Message channelPressureChange(int channel, int value) {
    const std::uint8_t data[] = {
        std::uint8_t(0xd0 | std::clamp(channel - 1, 0, 15)),
        std::uint8_t(std::clamp(value, 0, 127))};
    return {data, 2};
  }
  static Message allNotesOff(int channel) {
    return controllerEvent(channel, 123, 0);
  }
  static Message allSoundOff(int channel) {
    return controllerEvent(channel, 120, 0);
  }
  static Message midiStart() {
    const std::uint8_t value = 0xfa;
    return {&value, 1};
  }
  static Message midiContinue() {
    const std::uint8_t value = 0xfb;
    return {&value, 1};
  }
  static Message midiStop() {
    const std::uint8_t value = 0xfc;
    return {&value, 1};
  }
  static Message midiClock() {
    const std::uint8_t value = 0xf8;
    return {&value, 1};
  }
  static Message songPositionPointer(int value) {
    const std::uint8_t data[] = {0xf2, std::uint8_t(value & 127),
                                 std::uint8_t((value >> 7) & 127)};
    return {data, 3};
  }
  static Message createSysExMessage(const void *data, int size) {
    if (size < 0 || (!data && size > 0))
      return {};
    std::vector<std::uint8_t> bytes(std::size_t(std::max(0, size)) + 2);
    bytes.front() = 0xf0;
    bytes.back() = 0xf7;
    if (size > 0)
      std::memcpy(bytes.data() + 1, data, std::size_t(size));
    return {bytes.data(), int(bytes.size())};
  }
  static base::Text getMidiNoteName(int note, bool sharps = true,
                                    bool octave = true, int middleC = 3) {
    static constexpr const char *sharp[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                            "F#", "G",  "G#", "A",  "A#", "B"};
    static constexpr const char *flat[] = {"C",  "Db", "D",  "Eb", "E",  "F",
                                           "Gb", "G",  "Ab", "A",  "Bb", "B"};
    if (note < 0 || note > 127)
      return {};
    base::Text name = (sharps ? sharp : flat)[note % 12];
    if (octave)
      name += base::Text(note / 12 + middleC - 5);
    return name;
  }
};
} // namespace cydaw::midi
