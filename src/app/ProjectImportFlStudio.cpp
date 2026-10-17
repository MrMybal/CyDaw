#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <map>
#include <optional>
#include <set>

// FL Studio's project (.flp), as it is publicly documented: a header chunk
// ("FLhd": format, channel count, pulses per quarter note) then a data chunk
// ("FLdt") of events, each an id and a value — ids 0..63 a byte, 64..127 a
// 16-bit word, 128..191 a 32-bit word, 192..255 data of a length given as a
// 7-bit variable-length number. An event belongs to the last channel,
// pattern or arrangement begun before it.
//
// CyDAW reads: the tempo and time signature; each channel (its name, its
// plug-in or FL's own instrument, a sampler's sound, its level and pan);
// each pattern's notes (by channel); the playlist of the current
// arrangement (pattern clips, audio clips). A track per channel: a pattern
// clip on the playlist becomes, on each channel it plays, a clip of that
// channel's notes.

namespace cydaw::app::project_import {
namespace {

using namespace shared;

namespace id {
constexpr int timeSignatureNumerator = 17;
constexpr int timeSignatureBeat = 18;
constexpr int channelType = 21;
constexpr int patternLooped = 26;
constexpr int channelNew = 64;
constexpr int patternNew = 65;
constexpr int tempoCoarse = 66;
constexpr int tempoFine = 93;
constexpr int slotIndex = 98;
constexpr int arrangementNew = 99;
constexpr int arrangementCurrent = 100;
constexpr int insertOutput = 147;
constexpr int patternColour = 150;
constexpr int insertInput = 154;
constexpr int tempo = 156;
constexpr int channelName = 192;
constexpr int patternName = 193;
constexpr int title = 194;
constexpr int samplePath = 196;
constexpr int version = 199;
constexpr int pluginInternalName = 201;
constexpr int pluginName = 203;
constexpr int insertName = 204;
constexpr int pluginData = 213;
constexpr int channelLevels = 219;
constexpr int patternNotes = 224;
constexpr int mixerParameters = 225;
constexpr int playlist = 233;
constexpr int insertRouting = 235;
constexpr int insertFlags = 236;
constexpr int trackData = 238;
constexpr int trackName = 239;
constexpr int arrangementName = 241;
} // namespace id

constexpr int patternBase = 20'480;

struct Channel {
    int index {};
    int type {};
    juce::String name;
    juce::String internalName;
    juce::String samplePath;
    juce::String pluginName;
    juce::String pluginVendor;
    juce::String pluginPath;
    float gainDb {};
    float pan {};
};

struct Note {
    double position {}; // ticks in the pattern
    double length {};
    int channel {};
    int key {};
    float velocity {};
};

struct Pattern {
    juce::String name;
    juce::uint32 colour {};
    std::vector<Note> notes;
};

struct Item {
    double position {};
    double length {};
    int pattern {};  // 1-based; 0: a channel's (an audio clip)
    int channel { -1 };
    double startOffset {};
    int track {};
};

struct Arrangement {
    int index {};
    juce::String name;
    std::vector<Item> items;
};

juce::uint32 readU32(const juce::uint8* at)
{
    return static_cast<juce::uint32>(at[0]) | (static_cast<juce::uint32>(at[1]) << 8) | (static_cast<juce::uint32>(at[2]) << 16)
           | (static_cast<juce::uint32>(at[3]) << 24);
}

int readU16(const juce::uint8* at)
{
    return static_cast<int>(at[0]) | (static_cast<int>(at[1]) << 8);
}

float readF32(const juce::uint8* at)
{
    const auto bits = readU32(at);
    float value {};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// FL 11.5 and later write their text in UTF-16 (little-endian), earlier
// ones in single bytes; told apart by the zero bytes.
juce::String textOf(const juce::uint8* data, const std::size_t size)
{
    auto zeros = 0;
    for (std::size_t index = 1; index < size; index += 2)
        zeros += data[index] == 0 ? 1 : 0;
    juce::String text;
    if (size >= 2 && zeros * 2 >= static_cast<int>(size / 2)) {
        std::vector<juce::juce_wchar> characters;
        for (std::size_t index = 0; index + 1 < size; index += 2) {
            const auto unit = static_cast<juce::juce_wchar>(readU16(data + index));
            if (unit == 0)
                break;
            characters.push_back(unit);
        }
        characters.push_back(0);
        text = juce::String(juce::CharPointer_UTF32(reinterpret_cast<const juce::CharPointer_UTF32::CharType*>(characters.data())));
    } else {
        text = juce::String::fromUTF8(reinterpret_cast<const char*>(data), static_cast<int>(size));
    }
    // (A juce::String ends at its first zero: the terminator goes with it.)
    return text.trim();
}

class FlStudioReader {
public:
    FlStudioReader(const juce::File& file, ImportedProject& out) : file_(file), out_(out), folder_(file.getParentDirectory()) {}

    juce::Result read()
    {
        juce::MemoryBlock bytes;
        if (!file_.loadFileAsData(bytes))
            return juce::Result::fail("The file could not be opened: " + file_.getFullPathName());
        const auto* data = static_cast<const juce::uint8*>(bytes.getData());
        const auto size = bytes.getSize();
        if (size < 22 || std::memcmp(data, "FLhd", 4) != 0)
            return juce::Result::fail("Not an FL Studio project: " + file_.getFileName());
        const auto headerLength = readU32(data + 4);
        if (headerLength < 6 || 8 + headerLength + 8 > size)
            return juce::Result::fail("The FL Studio project's header is damaged");
        ppq_ = std::max(1, readU16(data + 12));
        auto at = static_cast<std::size_t>(8 + headerLength);
        if (std::memcmp(data + at, "FLdt", 4) != 0)
            return juce::Result::fail("The FL Studio project holds no data chunk");
        const auto dataLength = readU32(data + at + 4);
        at += 8;
        const auto end = std::min<std::size_t>(size, at + dataLength);
        out_.format = "fl-studio";
        out_.application = "FL Studio";
        while (at < end) {
            const auto event = static_cast<int>(data[at++]);
            if (event < 64) {
                if (at + 1 > end)
                    break;
                onNumber(event, data[at]);
                at += 1;
            } else if (event < 128) {
                if (at + 2 > end)
                    break;
                onNumber(event, static_cast<juce::uint32>(readU16(data + at)));
                at += 2;
            } else if (event < 192) {
                if (at + 4 > end)
                    break;
                onNumber(event, readU32(data + at));
                at += 4;
            } else {
                std::size_t length = 0;
                auto shift = 0;
                while (at < end) {
                    const auto byte = data[at++];
                    length |= static_cast<std::size_t>(byte & 0x7f) << shift;
                    shift += 7;
                    if ((byte & 0x80) == 0 || shift > 28)
                        break;
                }
                if (at + length > end)
                    break;
                onData(event, data + at, length);
                at += length;
            }
        }
        build();
        return juce::Result::ok();
    }

private:
    enum class Context { project, channel, pattern, arrangement, mixer };

    void onNumber(const int event, const juce::uint32 value)
    {
        switch (event) {
        case id::tempo: out_.tempo = juce::jlimit(10.0, 999.0, value / 1'000.0); return;
        case id::tempoCoarse:
            if (!fineTempo_)
                out_.tempo = juce::jlimit(10.0, 999.0, static_cast<double>(value));
            return;
        case id::tempoFine:
            fineTempo_ = true;
            return;
        case id::timeSignatureNumerator: out_.numerator = juce::jlimit(1, 32, static_cast<int>(value)); return;
        case id::timeSignatureBeat: out_.denominator = juce::jlimit(1, 32, static_cast<int>(value)); return;
        case id::channelNew:
            context_ = Context::channel;
            channel_ = static_cast<int>(value);
            channels_[channel_].index = channel_;
            return;
        case id::patternNew:
            context_ = Context::pattern;
            pattern_ = static_cast<int>(value);
            (void) patterns_[pattern_];
            return;
        case id::arrangementNew:
            context_ = Context::arrangement;
            arrangements_.push_back({ static_cast<int>(value), {}, {} });
            return;
        case id::arrangementCurrent: currentArrangement_ = static_cast<int>(value); return;
        case id::slotIndex:
        case id::insertOutput:
        case id::insertInput: context_ = Context::mixer; return;
        case id::channelType:
            if (context_ == Context::channel)
                channels_[channel_].type = static_cast<int>(value);
            return;
        case id::patternColour:
            if (context_ == Context::pattern)
                patterns_[pattern_].colour = 0xff000000u | ((value & 0xff) << 16) | (value & 0xff00) | ((value >> 16) & 0xff);
            return;
        default: return;
        }
    }

    void onData(const int event, const juce::uint8* data, const std::size_t size)
    {
        switch (event) {
        case id::version:
            version_ = textOf(data, size);
            out_.application = "FL Studio " + version_;
            return;
        case id::title:
            out_.name = textOf(data, size);
            return;
        case id::mixerParameters:
        case id::insertName:
        case id::insertRouting:
        case id::insertFlags: context_ = Context::mixer; return;
        case id::patternName:
            if (context_ == Context::pattern)
                patterns_[pattern_].name = textOf(data, size);
            return;
        case id::patternNotes:
            if (context_ == Context::pattern)
                for (std::size_t at = 0; at + 24 <= size; at += 24)
                    patterns_[pattern_].notes.push_back({ static_cast<double>(readU32(data + at)),
                                                          static_cast<double>(readU32(data + at + 8)), readU16(data + at + 6),
                                                          std::clamp(readU16(data + at + 12), 0, 127),
                                                          std::clamp(data[at + 21] / 128.0f, 0.0f, 1.0f) });
            return;
        case id::playlist:
            readPlaylist(data, size);
            return;
        case id::arrangementName:
            if (context_ == Context::arrangement && !arrangements_.empty())
                arrangements_.back().name = textOf(data, size);
            return;
        case id::trackData:
        case id::trackName: return;
        default: break;
        }
        if (context_ != Context::channel)
            return;
        auto& channel = channels_[channel_];
        switch (event) {
        case id::channelName:
        case id::pluginName:
            if (const auto name = textOf(data, size); name.isNotEmpty())
                channel.name = name;
            return;
        case id::pluginInternalName: channel.internalName = textOf(data, size); return;
        case id::samplePath: channel.samplePath = textOf(data, size); return;
        case id::channelLevels:
            if (size >= 8) {
                const auto pan = static_cast<int>(readU32(data));
                const auto volume = static_cast<int>(readU32(data + 4));
                channel.pan = juce::jlimit(-1.0f, 1.0f, (static_cast<float>(pan) - 6'400.0f) / 6'400.0f);
                channel.gainDb = linearToDb(volume / 10'000.0);
            }
            return;
        case id::pluginData: readWrapper(channel, data, size); return;
        default: return;
        }
    }

    // A hosted plug-in's wrapper: a kind, then records (id, 64-bit length,
    // bytes): 54 its name, 55 its file, 56 its maker.
    static void readWrapper(Channel& channel, const juce::uint8* data, const std::size_t size)
    {
        if (size < 4)
            return;
        const auto kind = readU32(data);
        if (kind != 8 && kind != 10)
            return;
        std::size_t at = 4;
        while (at + 12 <= size) {
            const auto record = readU32(data + at);
            const auto length = static_cast<std::size_t>(readU32(data + at + 4));
            at += 12;
            if (at + length > size)
                break;
            const auto text = juce::String::fromUTF8(reinterpret_cast<const char*>(data + at), static_cast<int>(length)).trim();
            if (record == 54)
                channel.pluginName = text;
            else if (record == 55)
                channel.pluginPath = text;
            else if (record == 56)
                channel.pluginVendor = text;
            at += length;
        }
    }

    void readPlaylist(const juce::uint8* data, const std::size_t size)
    {
        // 32 bytes an item before FL 21, 60 since.
        const auto major = version_.upToFirstOccurrenceOf(".", false, false).getIntValue();
        const std::size_t stride = (major >= 21 && size % 60 == 0) || (size % 32 != 0 && size % 60 == 0) ? 60 : 32;
        if (arrangements_.empty())
            arrangements_.push_back({ 0, {}, {} });
        auto& items = arrangements_.back().items;
        for (std::size_t at = 0; at + stride <= size; at += stride) {
            Item item;
            item.position = readU32(data + at);
            const auto base = readU16(data + at + 4);
            const auto index = readU16(data + at + 6);
            item.length = readU32(data + at + 8);
            item.track = 499 - readU16(data + at + 12);
            item.startOffset = std::max(0.0f, readF32(data + at + 24));
            if (index > base && base == patternBase)
                item.pattern = index - base;
            else
                item.channel = index;
            items.push_back(item);
        }
    }

    [[nodiscard]] double beats(const double ticks) const { return ticks / static_cast<double>(ppq_); }

    // A pattern's length: its last note's end, to the bar.
    [[nodiscard]] double patternLength(const Pattern& pattern) const
    {
        auto last = 0.0;
        for (const auto& note : pattern.notes)
            last = std::max(last, note.position + note.length);
        const auto bar = static_cast<double>(ppq_) * out_.numerator * 4.0 / std::max(1, out_.denominator);
        return std::max(bar, std::ceil(last / bar - 1.0e-9) * bar);
    }

    // FL's own instruments: what CyDAW reads them as (ImportedInstruments).
    static ImportedDevice deviceOf(const Channel& channel, const juce::File& sample)
    {
        ImportedDevice device;
        device.instrument = true;
        if (channel.internalName.equalsIgnoreCase("Fruity Wrapper") || channel.pluginName.isNotEmpty()) {
            device.plugin = true;
            device.name = channel.pluginName.isNotEmpty() ? channel.pluginName : channel.name;
            device.vendor = channel.pluginVendor;
            device.format = channel.pluginPath.endsWithIgnoreCase(".vst3") ? "VST3" : "VST2";
            return device;
        }
        device.type = channel.internalName.isNotEmpty() ? channel.internalName : juce::String("Sampler");
        device.name = device.type;
        device.sample = sample;
        return device;
    }

    void build()
    {
        if (arrangements_.empty()) {
            out_.warnings.add("The project holds no playlist: its patterns come in at the start.");
            arrangements_.push_back({ 0, {}, {} });
            auto at = 0.0;
            for (const auto& [index, pattern] : patterns_) {
                const auto length = patternLength(pattern);
                arrangements_.back().items.push_back({ at, length, index, -1, 0.0, 0 });
                at += length;
            }
        }
        const Arrangement* arrangement = &arrangements_.front();
        for (const auto& candidate : arrangements_)
            if (candidate.index == currentArrangement_)
                arrangement = &candidate;
        if (arrangements_.size() > 1)
            out_.warnings.add("Arrangement \"" + arrangement->name + "\" was read; the others were left out.");

        std::map<int, ImportedTrack> tracks; // by channel
        const auto trackOf = [&](const int index) -> ImportedTrack& {
            auto [found, made] = tracks.try_emplace(index);
            if (made) {
                const auto& channel = channels_[index];
                found->second.name = channel.name.isNotEmpty() ? channel.name : juce::String("Channel ") + juce::String(index + 1);
                found->second.gainDb = channel.gainDb;
                found->second.pan = channel.pan;
            }
            return found->second;
        };
        auto automation = false;
        for (const auto& item : arrangement->items) {
            if (item.pattern > 0) {
                const auto found = patterns_.find(item.pattern);
                if (found == patterns_.end())
                    continue;
                const auto& pattern = found->second;
                const auto content = patternLength(pattern);
                std::set<int> playing;
                for (const auto& note : pattern.notes)
                    playing.insert(note.channel);
                for (const auto channelIndex : playing) {
                    auto& track = trackOf(channelIndex);
                    track.midi = true;
                    ImportedMidiClip clip;
                    clip.name = pattern.name.isNotEmpty() ? pattern.name : juce::String("Pattern ") + juce::String(item.pattern);
                    clip.startBeats = beats(item.position);
                    clip.lengthBeats = std::max(0.25, beats(item.length));
                    clip.colour = pattern.colour;
                    std::vector<ImportedNote> notes;
                    for (const auto& note : pattern.notes)
                        if (note.channel == channelIndex && note.position >= item.startOffset - 1.0e-6)
                            notes.push_back({ note.key, beats(note.position - item.startOffset), std::max(1.0e-3, beats(note.length)),
                                              note.velocity });
                    // A clip longer than its pattern plays it again.
                    repeatContent(beats(std::max(1.0, content - item.startOffset)), clip.lengthBeats, true,
                                  [&](const double from, const double to, const double at) { placeNotes(notes, from, to, at, clip.notes); });
                    track.midiClips.push_back(std::move(clip));
                }
                continue;
            }
            const auto found = channels_.find(item.channel);
            if (found == channels_.end())
                continue;
            const auto& channel = found->second;
            if (channel.type == 5 || channel.samplePath.isEmpty()) {
                automation = true;
                continue;
            }
            auto& track = trackOf(item.channel);
            ImportedAudioClip clip;
            clip.name = channel.name;
            clip.originalPath = channel.samplePath;
            clip.file = audioFile(folder_, channel.samplePath, out_);
            clip.startSeconds = out_.beatsToSeconds(beats(item.position));
            clip.durationSeconds = std::max(0.01, out_.beatsToSeconds(beats(item.length)));
            clip.sourceOffsetSeconds = out_.beatsToSeconds(beats(item.startOffset));
            track.audioClips.push_back(std::move(clip));
        }
        if (automation)
            out_.warnings.add("Automation clips were left out.");
        // Each played channel: its instrument (a hosted plug-in, FL's own, a
        // sampler and its sound).
        for (auto& [index, track] : tracks) {
            const auto& channel = channels_[index];
            if (track.midi) {
                const auto sample = channel.samplePath.isNotEmpty() ? audioFile(folder_, channel.samplePath, out_) : juce::File();
                auto device = deviceOf(channel, sample);
                track.devices.add(device.name);
                track.hadInstrument = true;
                track.chain.push_back(std::move(device));
            }
            out_.tracks.push_back(std::move(track));
        }
        if (channels_.size() > tracks.size())
            out_.warnings.add(juce::String(static_cast<int>(channels_.size() - tracks.size()))
                              + " channel(s) not played on the playlist were left out.");
        out_.warnings.add("The mixer's inserts and their effects were left out (FL routes channels to inserts).");
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
    int ppq_ { 96 };
    bool fineTempo_ {};
    juce::String version_;
    Context context_ { Context::project };
    int channel_ {};
    int pattern_ {};
    int currentArrangement_ {};
    std::map<int, Channel> channels_;
    std::map<int, Pattern> patterns_;
    std::vector<Arrangement> arrangements_;
};

} // namespace

juce::Result importFlStudioProject(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    FlStudioReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
