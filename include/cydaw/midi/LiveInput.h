#pragma once
#include "EventBuffer.h"
#include <atomic>
#include <chrono>
#include <cyaudio/midi/MidiEventCollector.h>
#include <mutex>
namespace cydaw::midi {
inline std::uint64_t monotonicNanoseconds() noexcept {
  return std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count());
}
// A callback installs the actual device anchor for every collector it renders.
class AudioClockScope {
  inline static thread_local const cyaudio::AudioClockAnchor *current_ =
      nullptr;
  const cyaudio::AudioClockAnchor *before_{};

public:
  explicit AudioClockScope(const cyaudio::AudioClockAnchor &anchor)
      : before_(current_) {
    current_ = &anchor;
  }
  ~AudioClockScope() { current_ = before_; }
  static const cyaudio::AudioClockAnchor *current() noexcept {
    return current_;
  }
};
class MessageCollector {
  cyaudio::MidiEventCollector collector_;
  cyaudio::UmpBuffer ump_;
  std::mutex producer_;
  double rate_ = 48000;
  std::uint64_t frame_{};
  std::atomic<std::uint64_t> rejected_{};

public:
  MessageCollector();
  void reset(double rate);
  void addMessageToQueue(const Message &);
  template <class Foreign>
    requires(!std::is_same_v<Foreign, Message>)
  void addMessageToQueue(const Foreign &message) {
    Message owned(message.getRawData(), message.getRawDataSize(),
                  double(monotonicNanoseconds()) * 1e-9);
    addMessageToQueue(owned);
  }
  void removeNextBlockOfMessages(EventBuffer &, int frames);
  std::uint64_t rejectedMessages() const noexcept { return rejected_.load(); }
};
class KeyboardState {
  std::array<std::atomic<std::uint64_t>, 32> down_{};
  MessageCollector pending_;
  EventBuffer injected_;
  void apply(const Message &) noexcept;

public:
  void reset();
  void noteOn(int channel, int note, float velocity);
  void noteOff(int channel, int note, float velocity = 0);
  void allNotesOff(int channel);
  bool isNoteOn(int channel, int note) const noexcept;
  bool isNoteOnForChannels(int mask, int note) const noexcept;
  template <class Buffer>
  void processNextMidiBuffer(Buffer &buffer, int start, int frames,
                             bool inject) {
    for (const auto event : buffer)
      if (event.numBytes <= 3 && event.samplePosition >= start &&
          event.samplePosition < start + frames)
        apply(Message(event.data, event.numBytes));
    if (inject) {
      pending_.removeNextBlockOfMessages(injected_, frames);
      for (const auto event : injected_)
        buffer.addEvent(event.data, event.numBytes,
                        start + event.samplePosition);
    }
  }
};
} // namespace cydaw::midi
