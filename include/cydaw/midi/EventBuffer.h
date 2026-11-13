#pragma once
#include <algorithm>
#include <cyaudio/midi/Midi.h>
#include <stdexcept>
namespace cydaw::midi {
// The host's prepared MIDI arena, implemented by CyAudioCore. Event iteration
// borrows bytes until the next mutation. Capacity changes are control-thread
// only.
class EventBuffer {
  cyaudio::MidiBuffer buffer_;
  std::size_t bytes_ = 4096, events_ = 1024;
  std::uint64_t rejected_{};

public:
  struct Event {
    const std::uint8_t *data{};
    int numBytes{}, samplePosition{};
  };
  EventBuffer() {
    if (buffer_.prepare(events_, bytes_) != cyaudio::Status::ok)
      throw std::bad_alloc();
  }
  void ensureSize(std::size_t bytes) {
    if (bytes <= bytes_)
      return;
    cyaudio::MidiBuffer next;
    const auto events = std::max(events_, bytes / 8);
    if (next.prepare(events, bytes) != cyaudio::Status::ok)
      throw std::bad_alloc();
    for (std::size_t i = 0; i < buffer_.size(); ++i) {
      auto event = buffer_.event(i);
      if (next.add(event.frame, event.bytes) != cyaudio::Status::ok)
        throw std::runtime_error("MIDI capacity preservation failed");
    }
    buffer_ = std::move(next);
    bytes_ = bytes;
    events_ = events;
  }
  void clear() noexcept { buffer_.reset(); }
  bool isEmpty() const noexcept { return buffer_.size() == 0; }
  int getNumEvents() const noexcept { return int(buffer_.size()); }
  bool addEvent(const void *bytes, int size, int frame) noexcept {
    if (!bytes || size <= 0 || frame < 0) {
      ++rejected_;
      return false;
    }
    const bool okay = buffer_.add(std::uint32_t(frame),
                                  {static_cast<const std::uint8_t *>(bytes),
                                   std::size_t(size)}) == cyaudio::Status::ok;
    if (!okay)
      ++rejected_;
    return okay;
  }
  bool addEvent(cyaudio::ShortMidiMessage message, int frame) noexcept {
    return addEvent(message.data.data(), int(message.size), frame);
  }
  template <class Message>
    requires requires(const Message &m) {
      m.getRawData();
      m.getRawDataSize();
    }
  bool addEvent(const Message &message, int frame) noexcept {
    return addEvent(message.getRawData(), message.getRawDataSize(), frame);
  }
  template <class Buffer>
  void addEvents(const Buffer &other, int first, int count,
                 int delta) noexcept {
    for (const auto event : other)
      if (event.samplePosition >= first &&
          (count < 0 ||
           std::int64_t(event.samplePosition) < std::int64_t(first) + count))
        addEvent(event.data, event.numBytes, event.samplePosition + delta);
  }
  class Iterator {
    const EventBuffer *owner_{};
    std::size_t index_{};

  public:
    Iterator(const EventBuffer *owner, std::size_t index)
        : owner_(owner), index_(index) {}
    Event operator*() const noexcept {
      auto event = owner_->buffer_.event(index_);
      return {event.bytes.data(), int(event.bytes.size()), int(event.frame)};
    }
    Iterator &operator++() {
      ++index_;
      return *this;
    }
    friend bool operator==(const Iterator &, const Iterator &) = default;
  };
  Iterator begin() const noexcept { return {this, 0}; }
  Iterator end() const noexcept { return {this, buffer_.size()}; }
  std::uint64_t rejectedEvents() const noexcept { return rejected_; }
  cyaudio::MidiBuffer &native() noexcept { return buffer_; }
};
} // namespace cydaw::midi
