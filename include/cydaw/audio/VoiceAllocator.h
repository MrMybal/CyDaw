#pragma once
#include "SampleBuffer.h"
#include <cyaudio/midi/Midi.h>
#include <cydaw/base/Thread.h>
#include <memory>
#include <vector>
namespace cydaw::audio {
class VoiceSound {
public:
  virtual ~VoiceSound() = default;
  virtual bool appliesToNote(int) = 0;
  virtual bool appliesToChannel(int) = 0;
};
class VoiceAllocator;
class AllocatedVoice {
  friend class VoiceAllocator;
  int note_ = -1, channel_{};
  VoiceSound *sound_{};
  double rate_ = 48000;
  std::uint64_t order_{};
  bool down_{}, sustain_{}, sostenuto_{};

public:
  virtual ~AllocatedVoice() = default;
  virtual bool canPlaySound(VoiceSound *) = 0;
  virtual void startNote(int, float, VoiceSound *, int) = 0;
  virtual void stopNote(float, bool) = 0;
  virtual void pitchWheelMoved(int) = 0;
  virtual void controllerMoved(int, int) = 0;
  virtual void aftertouchChanged(int) {}
  virtual void channelPressureChanged(int) {}
  virtual void renderNextBlock(SampleBuffer<float> &, int, int) = 0;
  virtual void setCurrentPlaybackSampleRate(double rate) { rate_ = rate; }
  double getSampleRate() const noexcept { return rate_; }
  int getCurrentlyPlayingNote() const noexcept { return note_; }
  bool isPlayingChannel(int channel) const noexcept {
    return channel_ == channel;
  }
  bool isVoiceActive() const noexcept { return note_ >= 0; }
  bool isKeyDown() const noexcept { return down_; }
  void setKeyDown(bool down) noexcept { down_ = down; }
  bool isPlayingButReleased() const noexcept {
    return isVoiceActive() && !down_ && !sustain_ && !sostenuto_;
  }
  bool wasStartedBefore(const AllocatedVoice &other) const noexcept {
    return order_ < other.order_;
  }

protected:
  void clearCurrentNote() noexcept {
    note_ = -1;
    channel_ = 0;
    sound_ = nullptr;
    down_ = sustain_ = sostenuto_ = false;
  }
};
// Application voice allocation around the stereo PolyVoice renderer. Core's
// generic Synthesizer custom renderer is mono, so this owns scheduling only;
// all synthesis remains in the original CyDSP-backed instrument processors.
class VoiceAllocator {
  std::vector<std::unique_ptr<AllocatedVoice>> ownedVoices_;
  double rate_ = 48000;
  std::uint64_t nextOrder_{};
  bool steal_ = true;
  std::array<int, 16> pitch_{8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192,
                             8192, 8192, 8192, 8192, 8192, 8192, 8192, 8192};
  std::array<bool, 16> sustain_{}, sostenuto_{};
  void renderVoices(SampleBuffer<float> &output, int first, int count) {
    if (count > 0)
      for (auto *voice : voices)
        if (voice->isVoiceActive())
          voice->renderNextBlock(output, first, count);
  }
  void pedal(int channel, bool down, bool sostenuto) {
    if (channel < 1 || channel > 16)
      return;
    auto &value = (sostenuto ? sostenuto_ : sustain_)[std::size_t(channel - 1)];
    const bool changed = value != down;
    value = down;
    if (!changed)
      return;
    for (auto *voice : voices)
      if (voice->isVoiceActive() && voice->isPlayingChannel(channel)) {
        if (sostenuto) {
          if (down && voice->down_)
            voice->sostenuto_ = true;
          if (!down)
            voice->sostenuto_ = false;
        } else
          voice->sustain_ = down;
        if (!down && !voice->down_ && !voice->sustain_ && !voice->sostenuto_)
          stopVoice(voice, 0, true);
      }
  }

protected:
  base::CriticalSection lock;
  std::vector<AllocatedVoice *> voices;
  std::vector<std::unique_ptr<VoiceSound>> sounds;
  AllocatedVoice *findFreeVoice(VoiceSound *sound, int, int, bool steal) const {
    for (auto *voice : voices)
      if (!voice->isVoiceActive() && voice->canPlaySound(sound))
        return voice;
    if (!steal)
      return nullptr;
    AllocatedVoice *oldest{}, *released{};
    for (auto *voice : voices)
      if (voice->canPlaySound(sound)) {
        auto *&pick = voice->isPlayingButReleased() ? released : oldest;
        if (!pick || voice->wasStartedBefore(*pick))
          pick = voice;
      }
    return released ? released : oldest;
  }
  void stopVoice(AllocatedVoice *voice, float velocity, bool tail) {
    if (!voice)
      return;
    voice->stopNote(velocity, tail);
    if (!tail)
      voice->clearCurrentNote();
  }
  void startVoice(AllocatedVoice *voice, VoiceSound *sound, int channel,
                  int note, float velocity) {
    if (!voice || !sound || channel < 1 || channel > 16)
      return;
    if (voice->isVoiceActive())
      voice->stopNote(0, false);
    voice->note_ = note;
    voice->channel_ = channel;
    voice->sound_ = sound;
    voice->order_ = ++nextOrder_;
    voice->down_ = true;
    voice->sustain_ = sustain_[std::size_t(channel - 1)];
    voice->sostenuto_ = false;
    voice->startNote(note, velocity, sound, pitch_[std::size_t(channel - 1)]);
  }

public:
  virtual ~VoiceAllocator() = default;
  void addVoice(AllocatedVoice *voice) {
    if (!voice)
      return;
    base::ScopedLock guard(lock);
    voice->setCurrentPlaybackSampleRate(rate_);
    ownedVoices_.emplace_back(voice);
    voices.push_back(voice);
  }
  void addSound(VoiceSound *sound) {
    if (sound)
      sounds.emplace_back(sound);
  }
  void clearVoices() {
    base::ScopedLock guard(lock);
    voices.clear();
    ownedVoices_.clear();
  }
  void clearSounds() {
    base::ScopedLock guard(lock);
    allNotesOff(0, false);
    sounds.clear();
  }
  int getNumVoices() const noexcept { return int(voices.size()); }
  AllocatedVoice *getVoice(int index) const noexcept {
    return index >= 0 && std::size_t(index) < voices.size()
               ? voices[std::size_t(index)]
               : nullptr;
  }
  double getSampleRate() const noexcept { return rate_; }
  void setCurrentPlaybackSampleRate(double rate) {
    base::ScopedLock guard(lock);
    allNotesOff(0, false);
    rate_ = rate;
    for (auto *voice : voices)
      voice->setCurrentPlaybackSampleRate(rate);
  }
  bool isNoteStealingEnabled() const noexcept { return steal_; }
  void setNoteStealingEnabled(bool value) noexcept { steal_ = value; }
  virtual void noteOn(int channel, int note, float velocity) {
    if (channel < 1 || channel > 16 || note < 0 || note > 127)
      return;
    if (velocity <= 0) {
      noteOff(channel, note, 0, true);
      return;
    }
    base::ScopedLock guard(lock);
    for (const auto &sound : sounds)
      if (sound->appliesToNote(note) && sound->appliesToChannel(channel)) {
        for (auto *active : voices)
          if (active->isVoiceActive() &&
              active->getCurrentlyPlayingNote() == note &&
              active->isPlayingChannel(channel)) {
            active->down_ = false;
            stopVoice(active, 0, true);
          }
        if (auto *voice = findFreeVoice(sound.get(), channel, note, steal_))
          startVoice(voice, sound.get(), channel, note, velocity);
      }
  }
  virtual void noteOff(int channel, int note, float velocity, bool tail) {
    base::ScopedLock guard(lock);
    for (auto *voice : voices)
      if (voice->isVoiceActive() && voice->getCurrentlyPlayingNote() == note &&
          voice->isPlayingChannel(channel)) {
        voice->down_ = false;
        if (!tail || (!voice->sustain_ && !voice->sostenuto_))
          stopVoice(voice, velocity, tail);
      }
  }
  virtual void allNotesOff(int channel, bool tail) {
    base::ScopedLock guard(lock);
    for (auto *voice : voices)
      if (voice->isVoiceActive() &&
          (channel == 0 || voice->isPlayingChannel(channel))) {
        voice->down_ = voice->sustain_ = voice->sostenuto_ = false;
        stopVoice(voice, 0, tail);
      }
    if (channel == 0) {
      sustain_.fill(false);
      sostenuto_.fill(false);
    } else if (channel >= 1 && channel <= 16) {
      sustain_[channel - 1] = sostenuto_[channel - 1] = false;
    }
  }
  virtual void handlePitchWheel(int channel, int value) {
    if (channel < 1 || channel > 16)
      return;
    pitch_[std::size_t(channel - 1)] = value;
    for (auto *voice : voices)
      if (voice->isVoiceActive() && voice->isPlayingChannel(channel))
        voice->pitchWheelMoved(value);
  }
  virtual void handleController(int channel, int controller, int value) {
    if (controller == 64)
      pedal(channel, value >= 64, false);
    else if (controller == 66)
      pedal(channel, value >= 64, true);
    else if (controller == 120)
      allNotesOff(channel, false);
    else if (controller == 123)
      allNotesOff(channel, true);
    else if (controller == 121) {
      pedal(channel, false, false);
      pedal(channel, false, true);
      handlePitchWheel(channel, 8192);
    }
    for (auto *voice : voices)
      if (voice->isVoiceActive() && voice->isPlayingChannel(channel))
        voice->controllerMoved(controller, value);
  }
  virtual void handleChannelPressure(int channel, int value) {
    for (auto *voice : voices)
      if (voice->isVoiceActive() && voice->isPlayingChannel(channel))
        voice->channelPressureChanged(value);
  }
  virtual void handleAftertouch(int channel, int note, int value) {
    for (auto *voice : voices)
      if (voice->isVoiceActive() && voice->isPlayingChannel(channel) &&
          voice->getCurrentlyPlayingNote() == note)
        voice->aftertouchChanged(value);
  }
  void handleMidi(std::span<const std::uint8_t> bytes) {
    const cyaudio::MidiMessageView view(bytes);
    if (!view.valid() || !view.channel())
      return;
    const int channel = int(view.channel());
    if (view.noteOn())
      noteOn(channel, int(view.note()), float(bytes[2]) / 127.f);
    else if (view.noteOff())
      noteOff(channel, int(view.note()), float(bytes[2]) / 127.f, true);
    else
      switch (bytes[0] & 0xf0) {
      case 0xb0:
        handleController(channel, bytes[1], bytes[2]);
        break;
      case 0xe0:
        handlePitchWheel(channel, bytes[1] | (int(bytes[2]) << 7));
        break;
      case 0xd0:
        handleChannelPressure(channel, bytes[1]);
        break;
      case 0xa0:
        handleAftertouch(channel, bytes[1], bytes[2]);
        break;
      default:
        break;
      }
  }
  template <class Midi>
  void renderNextBlock(SampleBuffer<float> &output, const Midi &midi, int first,
                       int count) {
    base::ScopedLock guard(lock);
    int cursor = first;
    const int end = first + count;
    for (const auto event : midi) {
      if (event.samplePosition < first)
        continue;
      if (event.samplePosition >= end)
        break;
      renderVoices(output, cursor, event.samplePosition - cursor);
      cursor = event.samplePosition;
      handleMidi({event.data, std::size_t(event.numBytes)});
    }
    renderVoices(output, cursor, end - cursor);
  }
};
} // namespace cydaw::audio
