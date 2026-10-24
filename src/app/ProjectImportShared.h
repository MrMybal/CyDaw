#pragma once

#include "ProjectImport.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <functional>

// What every project reader shares (ProjectImport*.cpp): levels, paths,
// colours, a clip's content repeated over its length.

namespace cydaw::app::project_import::shared {

// A document read whole from a compressed stream (a gzip'd set, a zip
// entry), up to a size: a small file can unpack to gigabytes. Empty when
// it is larger.
[[nodiscard]] inline juce::String readBounded(juce::InputStream& input, const juce::int64 most = 512LL * 1024 * 1024)
{
    juce::MemoryOutputStream read;
    if (read.writeFromInputStream(input, most + 1) > most)
        return {};
    return read.toUTF8();
}

// A linear gain (1 = 0 dB) in decibels, -96 for silence.
[[nodiscard]] inline float linearToDb(const double gain)
{
    return gain > 0.0 ? juce::jlimit(-96.0f, 24.0f, static_cast<float>(20.0 * std::log10(gain))) : -96.0f;
}

// The last part of a path, whichever its separators.
[[nodiscard]] inline juce::String fileNameOf(juce::String path)
{
    path = path.replaceCharacter('\\', '/');
    return path.contains("/") ? path.fromLastOccurrenceOf("/", false, false) : path;
}

[[nodiscard]] inline juce::uint32 opaque(const juce::uint8 red, const juce::uint8 green, const juce::uint8 blue)
{
    return 0xff000000u | (static_cast<juce::uint32>(red) << 16) | (static_cast<juce::uint32>(green) << 8) | blue;
}

// The audio file a project names: found where it says, or by its name
// under the project's folder; reported missing otherwise.
[[nodiscard]] inline juce::File audioFile(const juce::File& projectFolder, const juce::String& path, ImportedProject& out)
{
    if (path.trim().isEmpty())
        return {};
    auto file = resolveProjectAudioFile(projectFolder, path);
    if (file == juce::File())
        out.missingFiles.addIfNotAlreadyThere(path);
    return file;
}

// A clip's content of `contentLength` beats played over `length` beats,
// again and again when it loops: each pass (from, to in the content, and the
// beat it starts at in the clip).
inline void repeatContent(const double contentLength, const double length, const bool looping,
                          const std::function<void(double from, double to, double at)>& place)
{
    if (!looping || contentLength <= 1.0e-6 || contentLength >= length - 1.0e-6) {
        place(0.0, std::min(length, contentLength > 1.0e-6 ? contentLength : length), 0.0);
        return;
    }
    auto at = 0.0;
    for (auto pass = 0; at < length - 1.0e-6 && pass < 4'096; ++pass) {
        const auto to = std::min(contentLength, length - at);
        place(0.0, to, at);
        at += contentLength;
    }
}

// The notes of a stretch of content (from, to), moved to start at `at`;
// a note that runs past the stretch ends there.
inline void placeNotes(const std::vector<ImportedNote>& notes, const double from, const double to, const double at,
                       std::vector<ImportedNote>& into)
{
    // A clip's notes are bounded: a hostile file repeating a dense pattern
    // thousands of times would otherwise ask for gigabytes.
    constexpr std::size_t mostNotes = 1'000'000;
    for (const auto& note : notes) {
        if (into.size() >= mostNotes)
            return;
        if (note.startBeats < from - 1.0e-9 || note.startBeats >= to - 1.0e-9)
            continue;
        auto placed = note;
        placed.startBeats = note.startBeats - from + at;
        placed.lengthBeats = std::max(1.0e-3, std::min(note.lengthBeats, to - note.startBeats));
        into.push_back(placed);
    }
}

// Two mono files (a recording's left and right, as Ardour or an AAF keeps
// them) made into one stereo file in the folder for imported audio
// (importedAudioFolder()/<project>/<name>.wav); the left one when they cannot be.
[[nodiscard]] juce::File joinChannels(const juce::File& left, const juce::File& right, const juce::String& project,
                                      const juce::String& name, ImportedProject& out);

// A plug-in's name as hosts write it ("Serum (Xfer Records)"): its name and
// its maker.
inline void splitVendor(const juce::String& written, juce::String& name, juce::String& vendor)
{
    auto text = written.trim();
    if (text.endsWithChar(')') && text.containsChar('(')) {
        const auto open = text.lastIndexOfChar('(');
        vendor = text.substring(open + 1, text.length() - 1).trim();
        name = text.substring(0, open).trim();
        // "(x64)", "(32 out)": not a maker.
        if (vendor.containsOnly("0123456789 xoutchanelsXOUT-") || vendor.isEmpty()) {
            name = text;
            vendor = {};
        }
        return;
    }
    name = text;
    vendor = {};
}

} // namespace cydaw::app::project_import::shared
