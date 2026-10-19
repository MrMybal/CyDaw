#pragma once

#include "ProcessingCode.h"

#include <cydaw/engine/AudioRepair.h>

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <optional>
#include <vector>

// A processing (the Traitements mode): what to do to a sound, or to a clip's
// notes, kept in a .cyprocess file (factory ones in Factory/Processings, the
// person's in Options › Emplacements › Traitements).
//
//   { "format": "cydaw-processing", "version": 1, "kind": "audio" | "midi",
//     "name", "description",
//     "steps": [ { "type": "chain" | "code" | "denoise" | "hum" | "declick" | "highpass" | "lowpass"
//                  | "gain" | "normalise" | "trim",
//                  "enabled", "settings": { key: value },
//                  "code": JSFX source (code),
//                  "chain": a mixer's state (chain: an effect chain edited as a track),
//                  "published": [ { "binding", "label", "min", "max" } ] (chain: its knobs shown) } ],
//     "tail": { "allow", "thresholdDb", "holdSeconds", "maxSeconds", "fadeMs" },
//     "script": note script (midi) }
//
// An audio processing runs its steps in order over the sound (stereo); a
// step that rings (a chain, code) may run past the sound's end when the tail
// is allowed, the end then found where it falls quiet (engine/AudioRepair.h).
// Its parameters (the apply window's settings) are its steps' settings: a
// built-in step's, a JSFX's sliders, a chain's published knobs; a MIDI
// processing's, its script's param lines.

namespace cydaw::v2::processing {

enum class Kind { audio, midi };
enum class StepType { chain, code, denoise, hum, declick, highpass, lowpass, gain, normalise, trim };

[[nodiscard]] const char* keyOf(StepType type);
[[nodiscard]] std::optional<StepType> stepTypeOf(const juce::String& key);
// In the interface's language.
[[nodiscard]] juce::String nameOf(StepType type);
[[nodiscard]] juce::String hintOf(StepType type);
// Whether it may ring past the sound (its tail).
[[nodiscard]] bool rings(StepType type);

struct Published {
    juce::String binding;
    juce::String label;
    double minimum {};
    double maximum { 1.0 };
};

struct Step {
    StepType type { StepType::gain };
    bool enabled { true };
    std::map<juce::String, double> settings;
    juce::String code;
    juce::var chain;
    std::vector<Published> published;
};

struct Processing {
    Kind kind { Kind::audio };
    juce::String name;
    juce::String description;
    std::vector<Step> steps;
    juce::String script;
    engine::repair::TailSettings tail;
    // Where it was read from, and whether it is one of CyDAW's (read only).
    juce::File file;
    bool factory {};
};

[[nodiscard]] juce::var toJson(const Processing& processing);
[[nodiscard]] std::optional<Processing> fromJson(const juce::var& json, juce::String& error);
[[nodiscard]] std::optional<Processing> load(const juce::File& file, juce::String& error);
juce::Result save(const Processing& processing, const juce::File& file);

// The library: the factory's, then the person's, of a kind (or both).
[[nodiscard]] juce::File userFolder();
[[nodiscard]] juce::File factoryFolder();
[[nodiscard]] std::vector<Processing> library(std::optional<Kind> kind = std::nullopt);
[[nodiscard]] std::optional<Processing> find(const juce::String& name, std::optional<Kind> kind = std::nullopt);
inline constexpr const char* fileExtension = ".cyprocess";

// A setting a processing shows: "<step>/<key>" for a step's ("param<N>" for
// a note script's), its range and its value now.
struct Parameter {
    juce::String id;
    juce::String label;
    double minimum {};
    double maximum { 1.0 };
    double step {};
    double value {};
    juce::StringArray choices;
};
using Values = std::map<juce::String, double>;
[[nodiscard]] std::vector<Parameter> parametersOf(const Processing& processing);
// A built-in step's own settings (their defaults), for a new step.
[[nodiscard]] std::vector<Parameter> builtinSettings(StepType type);
// A step with its defaults (a code step with a starting script).
[[nodiscard]] Step newStep(StepType type);

// What a run needs from the message thread (a chain's effects made into a
// graph that renders anywhere), made before the run.
struct Prepared {
    Processing processing;
    Values values;
    std::vector<juce::var> chains; // per step: the graph's runtime state, else void
};
[[nodiscard]] Prepared prepare(const Processing& processing, const Values& values);

struct AudioResult {
    bool ok {};
    juce::String error;
    engine::repair::Channels channels;
    std::int64_t tailFrames {};   // how far past the sound it rings
    std::int64_t trimmedStart {}; // the frames a trim took off its start
    bool endless {};              // a tail that never fell quiet (cut)
    juce::StringArray notes;      // what the steps found ("hum: 50 Hz", "clicks: 12")
};
// The sound through it, on any thread (`progress` 0..1; `cancel` stops it).
[[nodiscard]] AudioResult renderAudio(const Prepared& prepared, engine::repair::Channels channels, double rate,
                                      const std::function<void(double)>& progress = {}, const std::atomic<bool>* cancel = nullptr);

// A MIDI processing over notes.
[[nodiscard]] NoteScriptResult renderNotes(const Processing& processing, const Values& values, std::vector<ScriptNote> notes,
                                           double tempo, double clipLength, std::uint32_t seed);

// A sound file read whole (30 minutes at most), one or two channels; written as 24-bit WAV.
juce::Result readAudioFile(const juce::File& file, engine::repair::Channels& channels, double& rate);
juce::Result writeAudioFile(const juce::File& file, const engine::repair::Channels& channels, double rate);

// The factory's processings written into a folder (the build's content).
juce::Result writeFactory(const juce::File& folder);

} // namespace cydaw::v2::processing
