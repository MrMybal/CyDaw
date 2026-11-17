#include <cydaw/midi/LiveInput.h>
namespace cydaw::midi {
MessageCollector::MessageCollector() {
  if (collector_.prepare(
          {512, 4096, cyaudio::LateMidiPolicy::clampToFirstFrame}) !=
          cyaudio::Status::ok ||
      ump_.prepare(16) != cyaudio::Status::ok)
    throw std::bad_alloc();
}
void MessageCollector::reset(double rate) {
  std::lock_guard guard(producer_);
  collector_.reset();
  rate_ = std::isfinite(rate) && rate > 0 ? rate : 48000;
  frame_ = 0;
}
void MessageCollector::addMessageToQueue(const Message &message) {
  const auto stamp = message.getTimeStamp();
  const auto ns = std::isfinite(stamp) && stamp > 0 && stamp < 9.0e9
                      ? std::uint64_t(std::llround(stamp * 1e9))
                      : monotonicNanoseconds();
  std::lock_guard guard(producer_);
  if (collector_.pushMidi(
          ns, {message.getRawData(), std::size_t(message.getRawDataSize())}) !=
      cyaudio::Status::ok)
    ++rejected_;
}
void MessageCollector::removeNextBlockOfMessages(EventBuffer &output,
                                                 int frames) {
  output.clear();
  ump_.reset();
  if (frames <= 0)
    return;
  cyaudio::AudioClockAnchor anchor;
  if (const auto *device = AudioClockScope::current())
    anchor = *device;
  else {
    const auto now = monotonicNanoseconds();
    const auto duration = std::uint64_t(double(frames) / rate_ * 1e9);
    anchor = {now > duration ? now - duration : 0, frame_, rate_};
  }
  const auto status =
      collector_.collect(anchor, std::uint32_t(frames), output.native(), ump_);
  if (status != cyaudio::Status::ok && status != cyaudio::Status::would_block)
    ++rejected_;
  frame_ += std::uint64_t(frames);
}
void KeyboardState::apply(const Message &message) noexcept {
  const int channel = message.getChannel();
  if (channel < 1 || channel > 16)
    return;
  const int note = message.getNoteNumber();
  if (message.isNoteOnOrOff() && note >= 0 && note < 128) {
    auto &word = down_[std::size_t(channel - 1) * 2 + std::size_t(note / 64)];
    const auto mask = std::uint64_t{1} << unsigned(note % 64);
    if (message.isNoteOn())
      word.fetch_or(mask, std::memory_order_relaxed);
    else
      word.fetch_and(~mask, std::memory_order_relaxed);
  } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
    down_[std::size_t(channel - 1) * 2] = 0;
    down_[std::size_t(channel - 1) * 2 + 1] = 0;
  }
}
void KeyboardState::reset() {
  for (auto &word : down_)
    word = 0;
  pending_.reset(48000);
}
void KeyboardState::noteOn(int channel, int note, float velocity) {
  auto message = Message::noteOn(channel, note, velocity);
  apply(message);
  pending_.addMessageToQueue(message);
}
void KeyboardState::noteOff(int channel, int note, float velocity) {
  auto message = Message::noteOff(
      channel, note, int(std::lround(std::clamp(velocity, 0.f, 1.f) * 127)));
  apply(message);
  pending_.addMessageToQueue(message);
}
void KeyboardState::allNotesOff(int channel) {
  for (int ch = 1; ch <= 16; ++ch)
    if (channel == 0 || channel == ch)
      for (int note = 0; note < 128; ++note)
        if (isNoteOn(ch, note))
          noteOff(ch, note);
}
bool KeyboardState::isNoteOn(int channel, int note) const noexcept {
  return channel >= 1 && channel <= 16 && note >= 0 && note < 128 &&
         (down_[std::size_t(channel - 1) * 2 + std::size_t(note / 64)].load(
              std::memory_order_relaxed) &
          (std::uint64_t{1} << unsigned(note % 64))) != 0;
}
bool KeyboardState::isNoteOnForChannels(int mask, int note) const noexcept {
  for (int channel = 1; channel <= 16; ++channel)
    if ((mask & (1 << (channel - 1))) && isNoteOn(channel, note))
      return true;
  return false;
}
} // namespace cydaw::midi
