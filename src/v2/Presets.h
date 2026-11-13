#include <cydaw/base/File.h>
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
    cydaw::base::Text name;
    cydaw::base::File file;
    bool factory {};
};

// A target as a folder name (what a file name may hold).
[[nodiscard]] cydaw::base::Text folderName(const cydaw::base::Text& target);
[[nodiscard]] cydaw::base::File userFolder(const cydaw::base::Text& kind, const cydaw::base::Text& target);
[[nodiscard]] cydaw::base::File factoryFolder(const cydaw::base::Text& kind, const cydaw::base::Text& target);
// CyDAW's first, then the person's, each by name.
[[nodiscard]] std::vector<Entry> list(const cydaw::base::Text& kind, const cydaw::base::Text& target);
[[nodiscard]] const Entry* find(const std::vector<Entry>& entries, const cydaw::base::Text& name);
// Kept as the person's (one of that name replaced). False with `error`.
bool save(const cydaw::base::Text& kind, const cydaw::base::Text& target, const cydaw::base::Text& name, const cydaw::base::Value& state,
          cydaw::base::Text& error);
// The preset's state; void with `error` when the file is not one of that kind.
[[nodiscard]] cydaw::base::Value read(const Entry& entry, const cydaw::base::Text& kind, cydaw::base::Text& error);
// The person's only: to the recycle bin; under another name.
bool remove(const Entry& entry, cydaw::base::Text& error);
bool rename(const Entry& entry, const cydaw::base::Text& name, cydaw::base::Text& error);

} // namespace cydaw::v2::presets
