#pragma once

#include <cydaw/engine/MidiEffects.h>

#include <cydaw/midi/EventBuffer.h>

#include <algorithm>
#include <array>

// A track's MIDI effects run on a prepared event buffer: its notes and
// controllers through the chain (MidiEffects.h), what the chain does not
// handle (system messages) passed on as it was. The MIDI tracks' engine and
// the graph's renderer (the editor's preview, the exported plug-in) both use
// it. Audio thread; prepare() off it.

namespace cydaw::app {

class MidiEffectsHost final {
public:
  void prepare(const double sampleRate) {
    chain_.prepare(sampleRate);
    others_.ensureSize(1'024);
  }

  void reset() noexcept {
    chain_.reset();
    active_ = false;
  }

  // The events of `midi` from `start` for `frames` samples through the
  // plan's effects, in place. Nothing is done while there are none (and
  // were none: notes they left sounding are let go first).
  template <class Buffer>
  void apply(Buffer &midi, const int start, const int frames,
             const engine::MidiEffectsPlan *plan,
             const engine::MidiEffectClock &clock) noexcept {
    const auto effects = plan != nullptr && plan->count > 0;
    if (!effects && !active_)
      return;
    active_ = effects;
    std::size_t count = 0;
    others_.clear();
    for (const auto metadata : midi) {
      const auto *data = metadata.data;
      const auto size = metadata.numBytes;
      const auto at = metadata.samplePosition - start;
      if (size >= 1 && size <= 3 && data[0] >= 0x80 && data[0] < 0xF0 &&
          count < in_.size() && at >= 0 && at < frames)
        in_[count++] = {static_cast<std::uint32_t>(at), data[0],
                        static_cast<std::uint8_t>(size > 1 ? data[1] : 0),
                        static_cast<std::uint8_t>(size > 2 ? data[2] : 0)};
      else
        others_.addEvent(data, size, metadata.samplePosition);
    }
    const auto made =
        chain_.process(in_.data(), count, out_.data(), out_.size(),
                       static_cast<std::uint32_t>(std::max(0, frames)), clock,
                       effects ? plan : nullptr);
    midi.clear();
    for (std::size_t index = 0; index < made; ++index) {
      const auto &event = out_[index];
      const auto type = event.status & 0xF0;
      const std::uint8_t raw[3]{event.status, event.data1, event.data2};
      midi.addEvent(raw, type == 0xC0 || type == 0xD0 ? 2 : 3,
                    start + static_cast<int>(event.offset));
    }
    for (const auto event : others_)
      midi.addEvent(event.data, event.numBytes, event.samplePosition);
  }

private:
  engine::MidiEffectChain chain_;
  std::array<engine::MidiEffectEvent, engine::MidiEffectChain::capacity> in_{};
  std::array<engine::MidiEffectEvent, engine::MidiEffectChain::capacity> out_{};
  cydaw::midi::EventBuffer others_;
  bool active_{};
};

} // namespace cydaw::app
