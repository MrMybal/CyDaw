#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <map>
#include <memory>
#include <set>

// Reaper's project (.rpp): text, a tree of blocks — "<NAME arguments" opens
// one, ">" closes it, every other line is a field of the block it is in
// ("NAME value…"). Times are in seconds; a MIDI source's events count ticks
// ("HASDATA 1 960 QN": 960 a quarter note) from the one before ("E delta
// status data1 data2", bytes in hexadecimal).

namespace cydaw::app::project_import {
namespace {

using namespace shared;

struct Block {
    juce::StringArray head; // "<NAME a b": NAME, a, b
    // Its fields and inner blocks in their order (a take's fields follow its
    // TAKE line).
    struct Entry {
        juce::StringArray tokens;
        std::unique_ptr<Block> block;
    };
    std::vector<Entry> entries;

    [[nodiscard]] juce::String name() const { return head.isEmpty() ? juce::String() : head[0]; }
    [[nodiscard]] const juce::StringArray* field(const juce::String& key) const
    {
        for (const auto& entry : entries)
            if (entry.block == nullptr && !entry.tokens.isEmpty() && entry.tokens[0] == key)
                return &entry.tokens;
        return nullptr;
    }
    [[nodiscard]] juce::String text(const juce::String& key, const int index = 1, const juce::String& fallback = {}) const
    {
        const auto* tokens = field(key);
        return tokens != nullptr && index < tokens->size() ? (*tokens)[index] : fallback;
    }
    [[nodiscard]] double number(const juce::String& key, const int index, const double fallback) const
    {
        const auto* tokens = field(key);
        return tokens != nullptr && index < tokens->size() ? (*tokens)[index].getDoubleValue() : fallback;
    }
    [[nodiscard]] const Block* child(const juce::String& blockName) const
    {
        for (const auto& entry : entries)
            if (entry.block != nullptr && entry.block->name() == blockName)
                return entry.block.get();
        return nullptr;
    }
};

// A line as Reaper writes it: words, or strings in " ' or ` (the quote the
// string does not hold).
juce::StringArray tokenise(const juce::String& line)
{
    juce::StringArray tokens;
    auto pointer = line.getCharPointer();
    while (!pointer.isEmpty()) {
        while (!pointer.isEmpty() && juce::CharacterFunctions::isWhitespace(*pointer))
            ++pointer;
        if (pointer.isEmpty())
            break;
        const auto first = *pointer;
        juce::String token;
        if (first == '"' || first == '\'' || first == '`') {
            ++pointer;
            while (!pointer.isEmpty() && *pointer != first)
                token += *pointer++;
            if (!pointer.isEmpty())
                ++pointer;
        } else {
            while (!pointer.isEmpty() && !juce::CharacterFunctions::isWhitespace(*pointer))
                token += *pointer++;
        }
        tokens.add(token);
    }
    return tokens;
}

std::unique_ptr<Block> parse(const juce::String& content)
{
    auto root = std::make_unique<Block>();
    std::vector<Block*> open { root.get() };
    juce::StringArray lines;
    lines.addLines(content);
    for (const auto& raw : lines) {
        const auto line = raw.trim();
        if (line.isEmpty())
            continue;
        if (line.startsWithChar('<')) {
            auto block = std::make_unique<Block>();
            block->head = tokenise(line.substring(1));
            auto* made = block.get();
            open.back()->entries.push_back({ {}, std::move(block) });
            open.push_back(made);
        } else if (line == ">") {
            if (open.size() > 1)
                open.pop_back();
        } else {
            open.back()->entries.push_back({ tokenise(line), nullptr });
        }
    }
    return root;
}

// PEAKCOL: an OS colour with bit 24 set when it is one (Windows: 0x00BBGGRR).
juce::uint32 reaperColour(const juce::int64 value)
{
    if ((value & 0x1000000) == 0)
        return 0;
    return opaque(static_cast<juce::uint8>(value & 0xff), static_cast<juce::uint8>((value >> 8) & 0xff),
                  static_cast<juce::uint8>((value >> 16) & 0xff));
}

class ReaperReader {
public:
    ReaperReader(const juce::File& file, ImportedProject& out) : file_(file), out_(out), folder_(file.getParentDirectory()) {}

    juce::Result read()
    {
        const auto content = file_.loadFileAsString();
        if (!content.trimStart().startsWith("<REAPER_PROJECT"))
            return juce::Result::fail("Not a Reaper project: " + file_.getFileName());
        const auto root = parse(content);
        const Block* project = nullptr;
        for (const auto& entry : root->entries)
            if (entry.block != nullptr && entry.block->name() == "REAPER_PROJECT")
                project = entry.block.get();
        if (project == nullptr)
            return juce::Result::fail("The Reaper project holds no REAPER_PROJECT block");
        out_.format = "reaper";
        out_.application = "Reaper " + (project->head.size() > 2 ? project->head[2] : juce::String());
        out_.tempo = juce::jlimit(20.0, 960.0, project->number("TEMPO", 1, 120.0));
        out_.numerator = juce::jlimit(1, 32, static_cast<int>(project->number("TEMPO", 2, 4.0)));
        out_.denominator = juce::jlimit(1, 32, static_cast<int>(project->number("TEMPO", 3, 4.0)));
        out_.masterGainDb = linearToDb(project->number("MASTER_VOLUME", 1, 1.0));
        if (const auto* envelope = project->child("TEMPOENVEX"))
            for (const auto& entry : envelope->entries)
                if (entry.block == nullptr && !entry.tokens.isEmpty() && entry.tokens[0] == "PT" && entry.tokens.size() > 2
                    && std::abs(entry.tokens[2].getDoubleValue() - out_.tempo) > 0.01) {
                    out_.warnings.add("The tempo changes along the project: CyDAW keeps " + juce::String(out_.tempo, 2) + " BPM.");
                    break;
                }
        for (const auto& entry : project->entries)
            if (entry.block != nullptr && entry.block->name() == "TRACK")
                readTrack(*entry.block);
        return juce::Result::ok();
    }

private:
    [[nodiscard]] double beats(const double seconds) const { return out_.secondsToBeats(seconds); }

    void readTrack(const Block& block)
    {
        ImportedTrack track;
        track.name = block.text("NAME");
        track.gainDb = linearToDb(block.number("VOLPAN", 1, 1.0));
        track.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, block.number("VOLPAN", 2, 0.0)));
        track.muted = block.number("MUTESOLO", 1, 0.0) != 0.0;
        track.solo = block.number("MUTESOLO", 2, 0.0) != 0.0;
        track.colour = reaperColour(static_cast<juce::int64>(block.number("PEAKCOL", 1, 0.0)));
        if (block.number("ISBUS", 1, 0.0) == 1.0)
            out_.warnings.add("Folder \"" + track.name + "\" was flattened: its tracks come in on their own.");
        if (const auto* chain = block.child("FXCHAIN"))
            readChain(*chain, track);
        for (const auto& entry : block.entries)
            if (entry.block != nullptr && entry.block->name() == "ITEM")
                readItem(*entry.block, track);
        track.midi = track.midi || track.hadInstrument || !track.midiClips.empty();
        if (block.child("FXCHAIN_REC") != nullptr)
            out_.warnings.add("Track \"" + track.name + "\": its input effects were left out.");
        out_.tracks.push_back(std::move(track));
    }

    void readChain(const Block& chain, ImportedTrack& track)
    {
        auto bypassed = false;
        for (const auto& entry : chain.entries) {
            if (entry.block == nullptr) {
                if (!entry.tokens.isEmpty() && entry.tokens[0] == "BYPASS")
                    bypassed = entry.tokens.size() > 1 && entry.tokens[1] == "1";
                continue;
            }
            const auto kind = entry.block->name();
            if (kind != "VST" && kind != "CLAP" && kind != "JS" && kind != "AU" && kind != "LV2" && kind != "DX")
                continue;
            ImportedDevice device;
            auto written = entry.block->head.size() > 1 ? entry.block->head[1] : juce::String();
            auto prefix = written.upToFirstOccurrenceOf(":", false, false).trim();
            if (kind == "JS") {
                prefix = "JS";
                device.name = written;
                device.format = "JSFX";
            } else {
                splitVendor(written.fromFirstOccurrenceOf(":", false, false), device.name, device.vendor);
                device.format = prefix.startsWithIgnoreCase("VST3") ? "VST3"
                                : prefix.startsWithIgnoreCase("VST") ? "VST2"
                                : prefix.startsWithIgnoreCase("CLAP") ? "CLAP"
                                : prefix.startsWithIgnoreCase("AU")   ? "AU"
                                : prefix.startsWithIgnoreCase("LV2")  ? "LV2"
                                                                      : prefix;
            }
            device.instrument = prefix.endsWithIgnoreCase("i");
            // Reaper's own (Cockos) devices are the application's; the rest
            // are plug-ins it hosts.
            device.plugin = !(device.vendor == "Cockos" && device.name.startsWith("Rea"));
            device.type = device.plugin ? juce::String() : device.name;
            if (device.name.isEmpty())
                device.name = written;
            track.devices.add(device.name + (bypassed ? " (bypassed)" : juce::String()));
            track.hadInstrument = track.hadInstrument || device.instrument;
            track.chain.push_back(std::move(device));
            bypassed = false;
        }
    }

    // An item: its active take (the one marked SEL, else the first).
    void readItem(const Block& item, ImportedTrack& track)
    {
        struct Take {
            juce::String name;
            double sourceOffset {};
            double rate { 1.0 };
            const Block* source {};
            bool selected {};
        };
        std::vector<Take> takes(1);
        for (const auto& entry : item.entries) {
            if (entry.block != nullptr) {
                if (entry.block->name() == "SOURCE")
                    takes.back().source = entry.block.get();
                continue;
            }
            const auto& tokens = entry.tokens;
            if (tokens.isEmpty())
                continue;
            if (tokens[0] == "TAKE") {
                takes.push_back({});
                takes.back().selected = tokens.size() > 1 && tokens[1] == "SEL";
            } else if (tokens[0] == "NAME" && tokens.size() > 1) {
                takes.back().name = tokens[1];
            } else if (tokens[0] == "SOFFS" && tokens.size() > 1) {
                takes.back().sourceOffset = tokens[1].getDoubleValue();
            } else if (tokens[0] == "PLAYRATE" && tokens.size() > 1) {
                takes.back().rate = std::max(0.01, tokens[1].getDoubleValue());
            }
        }
        const auto* take = &takes.front();
        for (const auto& candidate : takes)
            if (candidate.selected)
                take = &candidate;
        if (take->source == nullptr) {
            for (const auto& candidate : takes)
                if (candidate.source != nullptr) {
                    take = &candidate;
                    break;
                }
        }
        if (take->source == nullptr)
            return;
        if (takes.size() > 2)
            out_.warnings.add("Track \"" + track.name + "\": an item's other takes were left out.");
        const auto position = std::max(0.0, item.number("POSITION", 1, 0.0));
        const auto length = std::max(0.001, item.number("LENGTH", 1, 1.0));
        const auto looping = item.number("LOOP", 1, 0.0) != 0.0;
        const auto muted = item.number("MUTE", 1, 0.0) != 0.0;
        auto sourceOffset = take->sourceOffset;
        const auto* source = take->source;
        // A section of a source: where it starts, and the source within.
        if (source->head.size() > 1 && source->head[1] == "SECTION") {
            sourceOffset += source->number("STARTPOS", 1, 0.0);
            if (source->number("MODE", 1, 0.0) != 0.0)
                out_.warnings.add("Track \"" + track.name + "\": a reversed section plays forward.");
            if (const auto* inner = source->child("SOURCE"))
                source = inner;
        }
        const auto type = source->head.size() > 1 ? source->head[1] : juce::String();
        const auto name = take->name.isNotEmpty() ? take->name : item.text("NAME");
        if (type == "MIDI") {
            readMidi(*source, track, name, position, length, looping, muted, sourceOffset, take->rate);
            return;
        }
        if (type == "EMPTY" || type == "CLICK" || type == "VIDEO")
            return;
        ImportedAudioClip clip;
        clip.name = name;
        clip.originalPath = source->text("FILE");
        clip.file = audioFile(folder_, clip.originalPath, out_);
        clip.startSeconds = position;
        clip.durationSeconds = length;
        clip.sourceOffsetSeconds = std::max(0.0, sourceOffset);
        if (std::abs(take->rate - 1.0) > 1.0e-6)
            clip.sourceDurationSeconds = length * take->rate;
        clip.fadeInSeconds = std::max(0.0, item.number("FADEIN", 2, 0.0));
        clip.fadeOutSeconds = std::max(0.0, item.number("FADEOUT", 2, 0.0));
        clip.gainDb = linearToDb(item.number("VOLPAN", 1, 1.0));
        clip.looping = looping;
        clip.muted = muted;
        clip.colour = reaperColour(static_cast<juce::int64>(item.number("COLOR", 1, 0.0)));
        track.audioClips.push_back(std::move(clip));
    }

    void readMidi(const Block& source, ImportedTrack& track, const juce::String& name, const double position, const double length,
                  const bool looping, const bool muted, const double sourceOffset, const double rate)
    {
        const auto ticksPerQuarter = std::max(1.0, source.number("HASDATA", 2, 960.0));
        if (source.text("HASDATA", 3, "QN") != "QN")
            out_.warnings.add("Track \"" + track.name + "\": a MIDI item counts in seconds, not beats; its timing may move.");
        std::vector<ImportedNote> notes;
        std::map<std::pair<int, int>, std::vector<std::pair<double, float>>> held; // (channel, key): starts
        auto ticks = 0.0;
        for (const auto& entry : source.entries) {
            if (entry.block != nullptr) {
                // "<X delta …>" (a long message): its delta still counts.
                if (entry.block->head.size() > 1 && entry.block->head[0].equalsIgnoreCase("X"))
                    ticks += entry.block->head[1].getDoubleValue();
                continue;
            }
            const auto& tokens = entry.tokens;
            if (tokens.size() < 2)
                continue;
            const auto kind = tokens[0];
            if (!kind.startsWithIgnoreCase("E") && !kind.startsWithIgnoreCase("X"))
                continue;
            ticks += tokens[1].getDoubleValue();
            if (!kind.startsWithIgnoreCase("E") || tokens.size() < 5)
                continue;
            const auto status = tokens[2].getHexValue32();
            const auto key = tokens[3].getHexValue32() & 0x7f;
            const auto value = tokens[4].getHexValue32() & 0x7f;
            const auto channel = status & 0x0f;
            const auto beat = ticks / ticksPerQuarter;
            const auto type = status & 0xf0;
            if (type == 0x90 && value > 0) {
                held[{ channel, key }].push_back({ beat, static_cast<float>(value) / 127.0f });
            } else if (type == 0x80 || (type == 0x90 && value == 0)) {
                auto& starts = held[{ channel, key }];
                if (starts.empty())
                    continue;
                const auto [start, velocity] = starts.front();
                starts.erase(starts.begin());
                notes.push_back({ key, start, std::max(1.0e-3, beat - start), velocity });
            }
        }
        // Notes still held at the end end there.
        const auto contentBeats = ticks / ticksPerQuarter;
        for (auto& [key, starts] : held)
            for (const auto& [start, velocity] : starts)
                notes.push_back({ key.second, start, std::max(1.0e-3, contentBeats - start), velocity });
        ImportedMidiClip clip;
        clip.name = name;
        clip.startBeats = beats(position);
        clip.lengthBeats = std::max(0.25, beats(length) * rate);
        clip.looping = looping;
        clip.muted = muted;
        // The source from its offset (in beats at the project's tempo), as
        // the item plays it, repeated when it loops.
        const auto offsetBeats = std::max(0.0, beats(sourceOffset));
        std::vector<ImportedNote> shifted;
        for (const auto& note : notes)
            if (note.startBeats >= offsetBeats - 1.0e-9) {
                auto moved = note;
                moved.startBeats -= offsetBeats;
                shifted.push_back(moved);
            }
        const auto content = contentBeats > offsetBeats ? contentBeats - offsetBeats : clip.lengthBeats;
        repeatContent(content, clip.lengthBeats, looping, [&](const double from, const double to, const double at) {
            placeNotes(shifted, from, to, at, clip.notes);
        });
        track.midiClips.push_back(std::move(clip));
        track.midi = true;
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
};

} // namespace

juce::Result importReaperProject(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    ReaperReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
