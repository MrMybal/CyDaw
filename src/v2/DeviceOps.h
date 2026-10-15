#pragma once

#include "NodeGraphComponent.h"

#include <cstdint>
#include <functional>

// What is done to a card through its graph alone, the same in the session
// and in a plug-in exported from CyDAW: its settings as a preset keeps them
// and a preset put back, a sound file made a Prisme oscillator's table, a
// place's or a cabinet's response loaded from a file.

namespace cydaw::v2::device_ops {

// A card's knob by its key: the binding without "node.<id>.".
[[nodiscard]] juce::String parameterKeyOf(const juce::String& binding, std::uint64_t nodeId);

// A card's settings as a preset holds them (its knobs by key, a sampler's
// map, a model card's model, a wavetable's table, Prisme's tables, a
// response); none for a card it does not have.
[[nodiscard]] juce::var presetState(const app::NodeGraphComponent& graph, std::uint64_t nodeId);

// A preset put back on a card: its assets, then its knobs. A Batterie's kit
// changes through `setBatterieKit` (none: a preset of another kit fails).
bool applyPreset(app::NodeGraphComponent& graph, std::uint64_t nodeId, const juce::var& state, juce::String& error,
                 const std::function<bool(int kit, juce::String& error)>& setBatterieKit);

// A sound file made the table of a Prisme card's oscillator (0 A, 1 B);
// back to the factory table its setting names.
bool importPrismTable(app::NodeGraphComponent& graph, std::uint64_t nodeId, int oscillator, const juce::File& file,
                      juce::String& error);
bool clearPrismTable(app::NodeGraphComponent& graph, std::uint64_t nodeId, int oscillator);

// A cabinet's (two seconds) or a place's (Empreinte, eight seconds)
// response read from a sound file, its silences trimmed.
bool loadImpulse(app::NodeGraphComponent& graph, std::uint64_t nodeId, const juce::File& file, juce::String& error);

} // namespace cydaw::v2::device_ops
