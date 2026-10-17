#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <zstd.h>

#include <map>

// Zrythm's project (project.zpj, Zrythm 1.0): JSON compressed with zstd. Its
// tracklist holds tracks (their type, name, colour, channel with its fader,
// instrument and inserts), each with lanes of regions: a MIDI region's notes,
// an audio region's clip of the pool (the project's pool/<name>.wav or
// .flac). Positions are in ticks, 960 a quarter note; a note's from its
// region's start. The tempo track holds the tempo and the time signature.

namespace cydaw::app::project_import {
namespace {

using namespace shared;

constexpr double ticksPerBeat = 960.0;

// Zrythm 1.0's track types, in order.
enum TrackType { instrument, audio, master, chord, marker, tempo, modulator, audioBus, audioGroup, midi, midiBus, midiGroup, folder };

class ZrythmReader {
public:
    ZrythmReader(const juce::File& file, ImportedProject& out) : file_(file), out_(out), folder_(file.getParentDirectory()) {}

    juce::Result read()
    {
        juce::MemoryBlock bytes;
        if (!file_.loadFileAsData(bytes))
            return juce::Result::fail("The file could not be opened: " + file_.getFullPathName());
        juce::String text;
        const auto* data = static_cast<const juce::uint8*>(bytes.getData());
        if (bytes.getSize() >= 4 && data[0] == 0x28 && data[1] == 0xb5 && data[2] == 0x2f && data[3] == 0xfd) {
            // zstd: frame by frame into memory.
            juce::MemoryOutputStream plain;
            auto* stream = ZSTD_createDStream();
            if (stream == nullptr)
                return juce::Result::fail("No zstd decoder");
            (void) ZSTD_initDStream(stream);
            std::vector<char> chunk(ZSTD_DStreamOutSize());
            ZSTD_inBuffer input { bytes.getData(), bytes.getSize(), 0 };
            auto failed = false;
            while (input.pos < input.size) {
                ZSTD_outBuffer output { chunk.data(), chunk.size(), 0 };
                const auto result = ZSTD_decompressStream(stream, &output, &input);
                if (ZSTD_isError(result) != 0) {
                    failed = true;
                    break;
                }
                plain.write(chunk.data(), output.pos);
                if (plain.getDataSize() > 512u * 1024u * 1024u) {
                    failed = true;
                    break;
                }
            }
            ZSTD_freeDStream(stream);
            if (failed)
                return juce::Result::fail("The Zrythm project could not be decompressed: " + file_.getFileName());
            text = plain.toUTF8();
        } else {
            text = bytes.toString();
        }
        if (text.trimStart().startsWith("---"))
            return juce::Result::fail("An older Zrythm project (YAML): open it in Zrythm 1.0 or later and save it, then import it.");
        const auto root = juce::JSON::parse(text);
        if (root.getProperty("type", {}).toString() != "ZrythmProject")
            return juce::Result::fail("Not a Zrythm project: " + file_.getFileName());
        out_.format = "zrythm";
        out_.application = "Zrythm " + root.getProperty("version", {}).toString();
        out_.name = root.getProperty("title", {}).toString();
        // Its pool: clips by their id.
        if (const auto* clips = root.getProperty("audioEngine", {}).getProperty("pool", {}).getProperty("clips", {}).getArray())
            for (const auto& clip : *clips)
                if (clip.isObject())
                    pool_[static_cast<int>(clip.getProperty("poolId", -1))] = clip;
        const auto* tracks = root.getProperty("tracklist", {}).getProperty("tracks", {}).getArray();
        if (tracks == nullptr)
            return juce::Result::fail("The Zrythm project holds no tracks");
        // The tempo first: every position depends on it.
        for (const auto& track : *tracks)
            if (static_cast<int>(track.getProperty("type", -1)) == tempo) {
                out_.tempo = juce::jlimit(20.0, 999.0, control(track.getProperty("bpmPort", {}), 120.0));
                out_.numerator = juce::jlimit(1, 32, static_cast<int>(control(track.getProperty("beatsPerBarPort", {}), 4.0)));
                const auto unit = static_cast<int>(control(track.getProperty("beatUnitPort", {}), 1.0));
                out_.denominator = 2 << juce::jlimit(0, 3, unit);
            }
        for (const auto& track : *tracks)
            readTrack(track);
        return juce::Result::ok();
    }

private:
    static double control(const juce::var& port, const double fallback)
    {
        return port.isObject() && port.hasProperty("control") ? static_cast<double>(port.getProperty("control", fallback)) : fallback;
    }

    static double ticks(const juce::var& position) { return static_cast<double>(position.getProperty("ticks", 0.0)); }

    static juce::uint32 colourOf(const juce::var& colour)
    {
        if (!colour.isObject())
            return 0;
        const auto channel = [&](const char* name) {
            return static_cast<juce::uint8>(juce::jlimit(0.0, 255.0, static_cast<double>(colour.getProperty(name, 0.0)) * 255.0));
        };
        return opaque(channel("red"), channel("green"), channel("blue"));
    }

    static ImportedDevice deviceOf(const juce::var& plugin, const bool isInstrument)
    {
        ImportedDevice device;
        const auto descriptor = plugin.getProperty("setting", {}).getProperty("descriptor", {});
        device.name = descriptor.getProperty("name", {}).toString();
        device.vendor = descriptor.getProperty("author", {}).toString();
        static const char* protocols[] { "", "LV2", "DSSI", "LADSPA", "VST2", "VST3", "AU", "SFZ", "SF2", "CLAP", "JSFX" };
        const auto protocol = static_cast<int>(descriptor.getProperty("protocol", 0));
        device.format = protocol >= 0 && protocol < 11 ? protocols[protocol] : "";
        device.plugin = true;
        device.instrument = isInstrument;
        return device;
    }

    void readTrack(const juce::var& element)
    {
        const auto type = static_cast<int>(element.getProperty("type", -1));
        if (type == master) {
            out_.masterGainDb = linearToDb(control(element.getProperty("channel", {}).getProperty("fader", {}).getProperty("amp", {}), 1.0));
            return;
        }
        if (type == chord || type == marker || type == tempo || type == modulator || type == folder)
            return;
        ImportedTrack track;
        track.name = element.getProperty("name", {}).toString();
        track.midi = type == instrument || type == midi || type == midiBus || type == midiGroup;
        track.colour = colourOf(element.getProperty("color", {}));
        const auto channel = element.getProperty("channel", {});
        const auto fader = channel.getProperty("fader", {});
        track.gainDb = linearToDb(control(fader.getProperty("amp", {}), 1.0));
        track.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, control(fader.getProperty("balance", {}), 0.5) * 2.0 - 1.0));
        track.muted = control(fader.getProperty("mute", {}), 0.0) > 0.5;
        track.solo = control(fader.getProperty("solo", {}), 0.0) > 0.5;
        if (const auto instrumentPlugin = channel.getProperty("instrument", {}); instrumentPlugin.isObject()) {
            track.chain.push_back(deviceOf(instrumentPlugin, true));
            track.hadInstrument = true;
        }
        if (const auto* inserts = channel.getProperty("inserts", {}).getArray())
            for (const auto& insert : *inserts)
                if (insert.isObject())
                    track.chain.push_back(deviceOf(insert, false));
        for (const auto& device : track.chain)
            if (device.name.isNotEmpty())
                track.devices.add(device.name);
        if (type == audioBus || type == audioGroup || type == midiBus || type == midiGroup)
            out_.warnings.add("Bus \"" + track.name + "\" came in as a track without clips; what is routed into it was left out.");
        if (const auto* lanes = element.getProperty("lanes", {}).getArray())
            for (const auto& lane : *lanes) {
                const auto laneMuted = static_cast<bool>(lane.getProperty("mute", false));
                if (const auto* regions = lane.getProperty("regions", {}).getArray())
                    for (const auto& region : *regions)
                        readRegion(region, laneMuted, track);
            }
        out_.tracks.push_back(std::move(track));
    }

    void readRegion(const juce::var& region, const bool laneMuted, ImportedTrack& track)
    {
        const auto base = region.getProperty("base", {});
        const auto start = ticks(base.getProperty("pos", {}));
        const auto end = ticks(base.getProperty("endPos", {}));
        const auto clipStart = ticks(base.getProperty("clipStartPos", {}));
        const auto loopStart = ticks(base.getProperty("loopStartPos", {}));
        const auto loopEnd = ticks(base.getProperty("loopEndPos", {}));
        const auto muted = laneMuted || static_cast<bool>(base.getProperty("muted", false));
        const auto name = region.getProperty("name", {}).toString();
        if (const auto* notes = region.getProperty("midiNotes", {}).getArray(); notes != nullptr || track.midi) {
            ImportedMidiClip clip;
            clip.name = name;
            clip.startBeats = start / ticksPerBeat;
            clip.lengthBeats = std::max(0.25, (end - start) / ticksPerBeat);
            clip.muted = muted;
            clip.colour = static_cast<bool>(region.getProperty("useColor", false)) ? colourOf(region.getProperty("color", {})) : 0;
            std::vector<ImportedNote> content;
            if (notes != nullptr)
                for (const auto& note : *notes) {
                    const auto noteBase = note.getProperty("base", {});
                    if (static_cast<bool>(noteBase.getProperty("muted", false)) || static_cast<bool>(note.getProperty("muted", false)))
                        continue;
                    const auto from = ticks(noteBase.getProperty("pos", {}));
                    const auto to = ticks(noteBase.getProperty("endPos", {}));
                    const auto velocity = static_cast<int>(note.getProperty("velocity", {}).getProperty("velocity", 90));
                    content.push_back({ juce::jlimit(0, 127, static_cast<int>(note.getProperty("value", 60))), from / ticksPerBeat,
                                        std::max(1.0e-3, (to - from) / ticksPerBeat), juce::jlimit(1, 127, velocity) / 127.0f });
                }
            // From the clip's start, its loop (loop start to loop end)
            // repeated over the region.
            const auto first = (loopEnd > clipStart ? loopEnd - clipStart : clip.lengthBeats * ticksPerBeat) / ticksPerBeat;
            const auto loop = std::max(0.0, (loopEnd - loopStart) / ticksPerBeat);
            const auto offset = clipStart / ticksPerBeat;
            const auto loopFrom = loopStart / ticksPerBeat;
            auto at = 0.0;
            for (auto pass = 0; at < clip.lengthBeats - 1.0e-6 && pass < 4'096; ++pass) {
                const auto from = pass == 0 ? offset : loopFrom;
                const auto span = std::min(pass == 0 ? first : loop, clip.lengthBeats - at);
                if (span <= 1.0e-6)
                    break;
                placeNotes(content, from, from + span, at, clip.notes);
                at += span;
            }
            track.midi = true;
            track.midiClips.push_back(std::move(clip));
            return;
        }
        // An audio region: its clip of the pool.
        const auto found = pool_.find(static_cast<int>(region.getProperty("poolId", -1)));
        if (found == pool_.end())
            return;
        const auto clipName = found->second.getProperty("name", {}).toString();
        const auto flac = static_cast<bool>(found->second.getProperty("useFlac", false));
        ImportedAudioClip clip;
        clip.name = name.isNotEmpty() ? name : clipName;
        clip.originalPath = "pool/" + clipName + (flac ? ".flac" : ".wav");
        clip.file = folder_.getChildFile(clip.originalPath);
        if (!clip.file.existsAsFile())
            clip.file = audioFile(folder_, clipName + (flac ? ".flac" : ".wav"), out_);
        clip.startSeconds = out_.beatsToSeconds(start / ticksPerBeat);
        clip.durationSeconds = std::max(0.01, out_.beatsToSeconds((end - start) / ticksPerBeat));
        clip.sourceOffsetSeconds = std::max(0.0, out_.beatsToSeconds(clipStart / ticksPerBeat));
        clip.gainDb = linearToDb(static_cast<double>(region.getProperty("gain", 1.0)));
        clip.looping = loopEnd - loopStart < end - start - 1.0;
        clip.muted = muted;
        clip.fadeInSeconds = out_.beatsToSeconds(ticks(base.getProperty("fadeInPos", {})) / ticksPerBeat);
        const auto fadeOut = ticks(base.getProperty("fadeOutPos", {}));
        if (fadeOut > 0.0 && fadeOut < end - start)
            clip.fadeOutSeconds = out_.beatsToSeconds((end - start - fadeOut) / ticksPerBeat);
        track.audioClips.push_back(std::move(clip));
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
    std::map<int, juce::var> pool_;
};

} // namespace

juce::Result importZrythmProject(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    ZrythmReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
