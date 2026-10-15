#pragma once

#include <cydaw/engine/EffectCatalogue.h>
#include <cydaw/engine/MidiEffectsDesign.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

// The MIDI effects of an instrument track: the notes it is given pass through
// them, in the order of their cards, before its instrument plays them — an
// arpeggiator, chords, a scale, note echoes, randomness, note lengths, a step
// sequencer, note repeats. Real-time: nothing allocated once prepared, and
// every note it sends out it lets go of (a card removed, the transport
// stopped).

namespace cydaw::engine {

// A note or a controller as the effects pass it on: where in the block, and
// its three bytes.
struct MidiEffectEvent {
    std::uint32_t offset {};
    std::uint8_t status {};
    std::uint8_t data1 {};
    std::uint8_t data2 {};

    [[nodiscard]] constexpr int channel() const noexcept { return status & 0x0F; }
    [[nodiscard]] constexpr bool isNoteOn() const noexcept { return (status & 0xF0) == 0x90 && data2 > 0; }
    [[nodiscard]] constexpr bool isNoteOff() const noexcept
    {
        return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0);
    }
    // All notes off, all sound off: what a stop, a loop or a jump sends.
    [[nodiscard]] constexpr bool isAllNotesOff() const noexcept
    {
        return (status & 0xF0) == 0xB0 && (data1 == 123 || data1 == 120);
    }
};

// One effect of the chain: its kind and its values, in its catalogue
// entry's order.
inline constexpr std::size_t midiEffectValueCount = 96;
struct MidiEffectSettings {
    NativeDspNodeKind kind { NativeDspNodeKind::passthrough };
    std::array<float, midiEffectValueCount> values {};

    [[nodiscard]] bool operator==(const MidiEffectSettings&) const = default;
};

// A track's chain: up to eight, in the order the notes go through them.
inline constexpr std::size_t midiEffectMost = 8;
struct MidiEffectsPlan {
    std::array<MidiEffectSettings, midiEffectMost> effects {};
    std::size_t count {};

    [[nodiscard]] bool operator==(const MidiEffectsPlan&) const = default;
};
using MidiEffectsRef = std::shared_ptr<const MidiEffectsPlan>;

// An effect's settings read from a card's payload, each value clamped to its
// range.
[[nodiscard]] MidiEffectSettings midiEffectSettingsOf(const EffectDescriptor& effect, const NativeDspNodeParameters& parameters) noexcept;
// The old arpeggiator card ("midi-arp": its step in beats, its octaves) as
// the arpeggiator's settings.
[[nodiscard]] MidiEffectSettings legacyArpeggiatorSettings(double stepBeats, double octaves) noexcept;
// Whether a card's type is one of the MIDI effects (or the old arpeggiator).
[[nodiscard]] bool isMidiEffectType(std::string_view typeKey) noexcept;

// What the cards draw, from the effects' own rules: the notes of a chord on
// `root` (low to high, how many); the notes an arpeggio of `held` plays, in
// order (how many); where a note goes into a scale (-1: removed).
int midiChordNotes(const MidiEffectSettings& settings, int root, std::array<int, 8>& notes) noexcept;
int midiArpeggioNotes(const MidiEffectSettings& settings, const int* held, int count, std::array<int, 160>& notes) noexcept;
[[nodiscard]] int midiScaleNote(int note, int key, int scale, int mode, int degrees, int transpose) noexcept;

// Where a block is in time: the tempo, and where the transport is in
// quarter notes at the block's first sample when it plays.
struct MidiEffectClock {
    double sampleRate { 48'000.0 };
    double beatsPerMinute { 120.0 };
    bool playing {};
    double quarterNotes {};
};

class MidiEffectChain final {
public:
    // Events a block may carry, and future ones each effect may hold.
    static constexpr std::size_t capacity = 1'024;

    MidiEffectChain();
    ~MidiEffectChain();
    MidiEffectChain(const MidiEffectChain&) = delete;
    MidiEffectChain& operator=(const MidiEffectChain&) = delete;

    void prepare(double sampleRate);
    // Forgets every note (the voices are let go by whoever calls it).
    void reset() noexcept;
    // A block's events (in the order of their offsets) through the plan's
    // effects; what comes out is written to `out` in order. How many.
    // Audio thread.
    std::size_t process(const MidiEffectEvent* in, std::size_t count, MidiEffectEvent* out, std::size_t room, std::uint32_t frames,
                        const MidiEffectClock& clock, const MidiEffectsPlan* plan) noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace cydaw::engine
