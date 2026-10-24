#pragma once

#include <cydaw/engine/Convolver.h>
#include <cydaw/engine/NativeDspGraph.h>
#include <cydaw/engine/PrismSynth.h>
#include <cydaw/engine/Sampler.h>
#include <cydaw/engine/SoundModel.h>
#include <cydaw/engine/WavetableShape.h>

#include <juce_core/juce_core.h>

#include <map>

// "cydaw-sound-model" files (.cysound), as the companion writes them
// (tools/companion/cydaw_companion/model.py), read into the engine's model.

namespace cydaw::app {

// The JSON as parsed; null with `error` set when it is not a model this
// version can play.
[[nodiscard]] cydaw::engine::SoundModelRef soundModelFromVar(const juce::var& value, juce::String& error);
[[nodiscard]] cydaw::engine::SoundModelRef loadSoundModelFile(const juce::File& file, juce::var& json,
                                                              juce::String& error);

// ---- Factory models named, not copied, in a project ---------------------------------------
// A sound-model card holds its whole model, and so does every state made of
// it (a preset, an exported plug-in). A project file names a factory model
// instead ("soundModelFile": "factory:Sound Models/….cysound"): a factory
// file is read once and its JSON shared (loadSoundModelFile), so a card still
// holding that very JSON has its model as it came from the file.
[[nodiscard]] juce::String factoryModelReference(const juce::var& json);
// The document with each such model named instead (copied where it changes,
// the rest shared with it).
[[nodiscard]] juce::var withFactoryModelReferences(const juce::var& document);
// The named models read back into it (in place); the names not found listed.
// A model not found stays named: its card keeps {"soundModelFile": name} as
// its model (silent), written back as the name, so a save never loses it.
void expandFactoryModelReferences(juce::var& document, juce::StringArray& missing);
// That stand-in for a factory model not found.
[[nodiscard]] bool isMissingFactoryModel(const juce::var& model);
// A short line about it: "brass-c4 · 1 zone · C4 · 82 partials".
[[nodiscard]] juce::String describeSoundModel(const cydaw::engine::SoundModel& model);

// The instrument's voice data in a saved graph (NodeGraphComponent's JSON, as
// a project and an exported plug-in's state carry it): the first sound-model
// card's model, the first sampler card's map and the first wavetable card's
// table, inside devices too, in the order the instrument compiler meets
// them. What the plan cannot carry as numbers, read without a graph
// component (the plug-in, before its window).
struct VoiceAssets {
    cydaw::engine::SoundModelRef soundModel;
    cydaw::engine::SamplerMapRef sampler;
    cydaw::engine::WavetableBankRef wavetable;
    // The first Prisme card's own tables (imported waves), when it has any.
    cydaw::engine::prism::TablesRef prismTables;
};
[[nodiscard]] VoiceAssets voiceAssetsFromGraphState(const juce::var& state);

// ---- A Prisme card's own tables ---------------------------------------------------------
// Waves imported into its oscillators, saved on the card (and so in the
// project, a preset, an exported plug-in): {"a": table, "b": table}, a table
// being {name, source (the file's name), frameLength, frames, pcm: base64 of
// little-endian 16-bit samples, frame after frame}.
[[nodiscard]] juce::var prismTableVar(const juce::String& name, const juce::String& source, int frameLength,
                                      const std::vector<std::vector<float>>& frames);
// The card's tables built (null when it has none).
[[nodiscard]] cydaw::engine::prism::TablesRef prismTablesFromVar(const juce::var& value);
// The card's JSON with oscillator `oscillator`'s table set (a void `table`
// takes it out); void when no table is left.
[[nodiscard]] juce::var withPrismTable(const juce::var& tables, int oscillator, const juce::var& table);
// One table's name and frame count, as the JSON says.
[[nodiscard]] juce::String prismTableName(const juce::var& tables, int oscillator);

// ---- An impulse response on an effect's card (Baffle et micro) --------------------------
// {name, source (the file's name), sampleRate, channels, frames, pcm: base64
// of little-endian 32-bit floats, channel after channel}.
[[nodiscard]] juce::var impulseVar(const cydaw::engine::ImpulseResponse& response, const juce::String& source);
[[nodiscard]] cydaw::engine::ImpulseResponseRef impulseFromVar(const juce::var& value);
// The cards of a saved graph (top level) that hold a response, by id: what
// an exported plug-in puts back into its effects.
[[nodiscard]] std::map<std::uint64_t, cydaw::engine::ImpulseResponseRef> impulsesFromGraphState(const juce::var& state);
// A state's native graph made whole again from the graph it carries: the
// cabinets' responses and the wavetables' tables, onto their cards' nodes
// (of the same ids).
void attachImpulses(cydaw::engine::NativeDspGraph& graph, const juce::var& editorState);

} // namespace cydaw::app
