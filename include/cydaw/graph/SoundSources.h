#pragma once

#include <string_view>

namespace cydaw::graph {

// Which primitives make sound, and which of them a polyphonic voice can be
// built around.
//
// This was written in four places at once: the engine's instrument compiler,
// the editor's plan builder, and two role inferences. Each carried its own
// hand-written list, and every one of them predated the wavetable and the
// string — so a graph built on either was classified as a plain effect,
// refused by both compilers, and never produced a voice. Adding a source now
// means editing this file, and nothing else has to be found.

// The synth family's models (SynthFamily.h) are sources as Prisme is.
//
// A node that produces audio from nothing but its own settings. Its presence
// is what makes a graph a generator rather than a processor.
[[nodiscard]] constexpr bool isSoundSourcePrimitive(
    const std::string_view processorKey) noexcept
{
    return processorKey == "core.oscillator"
           || processorKey == "core.fixed-oscillator"
           || processorKey == "core.noise"
           || processorKey == "core.wavetable"
           || processorKey == "core.string"
           || processorKey == "core.sound-model"
           || processorKey == "core.sampler"
           || processorKey == "core.prism-synth"
           || processorKey == "core.kick-synth"
           || processorKey == "core.batterie"
           || processorKey == "core.ambre-synth"
           || processorKey == "core.colosse-synth"
           || processorKey == "core.mosaique-synth"
           || processorKey == "core.nebuleuse-synth";
}

// Of those, the ones a voice allocator can drive per note: the allocator
// feeds them pitch and the voice renders one of them per key held down.
[[nodiscard]] constexpr bool isPolyphonicVoiceSource(
    const std::string_view processorKey) noexcept
{
    return processorKey == "core.oscillator"
           || processorKey == "core.wavetable"
           || processorKey == "core.string"
           || processorKey == "core.sound-model"
           || processorKey == "core.sampler"
           || processorKey == "core.prism-synth"
           || processorKey == "core.kick-synth"
           || processorKey == "core.batterie"
           || processorKey == "core.ambre-synth"
           || processorKey == "core.colosse-synth"
           || processorKey == "core.mosaique-synth"
           || processorKey == "core.nebuleuse-synth";
}

// Narrower still: a node whose presence on its own says the graph is meant to
// be an instrument. A wavetable or a string is equally at home in a drone
// that has no voice allocator at all, so neither of them qualifies — asking a
// drone graph for the rest of a polyphonic chain would only reject it.
[[nodiscard]] constexpr bool impliesPolyphonicVoice(
    const std::string_view processorKey) noexcept
{
    return processorKey == "core.oscillator";
}

} // namespace cydaw::graph
