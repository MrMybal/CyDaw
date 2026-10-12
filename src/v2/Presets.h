#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// Presets: the state of something that has settings, kept under a name,
// listed and put back. Three kinds:
//
//   "device"  a card of a track's chain: its knobs (by their key), and a
//             sampler's map, a sound-model card's model, a wavetable's table;
//             the target is the card's type;
//   "plugin"  a hosted plug-in's whole state; the target is the plug-in's name;
//   "chain"   a track's whole graph (an instrument and its effects, an audio
//             track's effects); the target is "instrument", "audio", "master"
//             or "bus".
//
// Files: <Presets>/<kind>/<target>/<name>.cypreset, the person's (the folder
// chosen in Options › Emplacements), and Factory/Presets/<kind>/<target>/,
// CyDAW's, read only:
//
//   { "format": "cydaw-preset", "version": 1, "kind": "device",
//     "target": "drive", "name": "Warm", "created": "2026-10-04T...",
//     "state": { ... } }

namespace cydaw::v2::presets {

inline constexpr auto fileExtension = ".cypreset";

struct Entry {
    juce::String name;
    juce::File file;
    bool factory {};
};

// A target as a folder name (what a file name may hold).
[[nodiscard]] juce::String folderName(const juce::String& target);
[[nodiscard]] juce::File userFolder(const juce::String& kind, const juce::String& target);
[[nodiscard]] juce::File factoryFolder(const juce::String& kind, const juce::String& target);
// CyDAW's first, then the person's, each by name.
[[nodiscard]] std::vector<Entry> list(const juce::String& kind, const juce::String& target);
[[nodiscard]] const Entry* find(const std::vector<Entry>& entries, const juce::String& name);
// Kept as the person's (one of that name replaced). False with `error`.
bool save(const juce::String& kind, const juce::String& target, const juce::String& name, const juce::var& state,
          juce::String& error);
// The preset's state; void with `error` when the file is not one of that kind.
[[nodiscard]] juce::var read(const Entry& entry, const juce::String& kind, juce::String& error);
// The person's only: to the recycle bin; under another name.
bool remove(const Entry& entry, juce::String& error);
bool rename(const Entry& entry, const juce::String& name, juce::String& error);

} // namespace cydaw::v2::presets
