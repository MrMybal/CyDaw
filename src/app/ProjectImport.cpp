#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace cydaw::app::project_import {
namespace {

constexpr double kLauncherSceneBeats = 16.0;
constexpr int kMaximumLoopRepeats = 64;

// ---------------------------------------------------------------------------
// Small helpers shared by the XML readers.

juce::String attributeText(const juce::XmlElement* element, const juce::String& name,
                           const juce::String& fallback = {})
{
    return element != nullptr && element->hasAttribute(name)
               ? element->getStringAttribute(name)
               : fallback;
}

double attributeNumber(const juce::XmlElement* element, const juce::String& name,
                       const double fallback)
{
    if (element == nullptr || !element->hasAttribute(name))
        return fallback;
    const auto text = element->getStringAttribute(name).trim();
    if (text.isEmpty() || !(juce::CharacterFunctions::isDigit(text[0]) || text[0] == '-'
                            || text[0] == '.' || text[0] == '+'))
        return fallback;
    return text.getDoubleValue();
}

bool attributeBool(const juce::XmlElement* element, const juce::String& name,
                   const bool fallback)
{
    const auto text = attributeText(element, name).trim().toLowerCase();
    if (text.isEmpty())
        return fallback;
    return text == "true" || text == "1" || text == "yes";
}

// Ableton keeps its values in <Name Value="..."/> children; a slash path
// walks down to one.
const juce::XmlElement* childPath(const juce::XmlElement* element, const juce::String& path)
{
    const auto* current = element;
    for (const auto& part : juce::StringArray::fromTokens(path, "/", {})) {
        if (current == nullptr)
            return nullptr;
        current = current->getChildByName(part);
    }
    return current;
}

juce::String valueAt(const juce::XmlElement* element, const juce::String& path,
                     const juce::String& fallback = {})
{
    const auto* found = childPath(element, path);
    return found != nullptr && found->hasAttribute("Value") ? found->getStringAttribute("Value")
                                                             : fallback;
}

double numberAt(const juce::XmlElement* element, const juce::String& path, const double fallback)
{
    const auto text = valueAt(element, path).trim();
    return text.isEmpty() ? fallback : text.getDoubleValue();
}

bool boolAt(const juce::XmlElement* element, const juce::String& path, const bool fallback)
{
    const auto text = valueAt(element, path).trim().toLowerCase();
    return text.isEmpty() ? fallback : (text == "true" || text == "1");
}

float linearToDb(const double gain)
{
    if (gain <= 1.0e-4)
        return -60.0f;
    return static_cast<float>(juce::jlimit(-60.0, 12.0, 20.0 * std::log10(gain)));
}

// "#rrggbb", "#rrggbbaa", "rrggbb": an ARGB value, 0 when unreadable.
juce::uint32 colourFromHex(juce::String text)
{
    text = text.trim().removeCharacters("#");
    if (text.length() == 6)
        return 0xff000000u | static_cast<juce::uint32>(text.getHexValue32());
    if (text.length() == 8) {
        const auto value = static_cast<juce::uint32>(text.getHexValue64());
        return (value << 24) | (value >> 8);
    }
    return 0;
}

juce::uint32 colourFromFloats(const float r, const float g, const float b, const float a)
{
    if (a <= 0.001f)
        return 0;
    const auto channel = [](const float v) {
        return static_cast<juce::uint32>(juce::jlimit(0, 255, juce::roundToInt(v * 255.0f)));
    };
    return (channel(a) << 24) | (channel(r) << 16) | (channel(g) << 8) | channel(b);
}

// Ableton Live's colour palette, by index, the way Live 10 and later lay it
// out: five rows of fourteen.
juce::uint32 abletonColour(const int index)
{
    static constexpr juce::uint32 palette[] = {
        0xff94a6, 0xffa529, 0xcc9927, 0xf7f47c, 0xbffb00, 0x1aff2f, 0x25ffa8,
        0x5cffe8, 0x8bc5ff, 0x5480e4, 0x92a7ff, 0xd86ce4, 0xe553a0, 0xffffff,
        0xff3636, 0xf66c03, 0x99724b, 0xfff034, 0x87ff67, 0x3dc300, 0x00bfaf,
        0x19e9ff, 0x10a4ee, 0x007dc0, 0x886ce4, 0xb677c6, 0xff39d4, 0xd0d0d0,
        0xe2675a, 0xffa374, 0xd3ad71, 0xedffae, 0xd2e498, 0xbad074, 0x9bc48d,
        0xd4fde1, 0xcdf1f8, 0xb9c1e3, 0xcdbbe4, 0xae98e5, 0xe5dce1, 0xa9a9a9,
        0xc6928b, 0xb78256, 0x99836a, 0xbfba69, 0xa6be00, 0x7db04d, 0x88c2ba,
        0x9bb3c4, 0x85a5c2, 0x8393cc, 0xa595b5, 0xbf9fbe, 0xbc7196, 0x7b7b7b,
        0xaf3333, 0xa95131, 0x724f41, 0xdbc300, 0x85961f, 0x539f31, 0x0a9c8e,
        0x236384, 0x1a2f96, 0x2f52a2, 0x624bad, 0xa34bad, 0xcc2e6e, 0x3c3c3c
    };
    constexpr auto count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    if (index < 0 || index >= count)
        return 0;
    return 0xff000000u | palette[index];
}

juce::String uniqueName(std::set<juce::String>& used, juce::String name, const juce::String& fallback)
{
    name = name.trim();
    if (name.isEmpty())
        name = fallback;
    auto candidate = name;
    for (auto suffix = 2; used.count(candidate.toLowerCase()) != 0; ++suffix)
        candidate = name + " " + juce::String(suffix);
    used.insert(candidate.toLowerCase());
    return candidate;
}

juce::String fileNameOf(juce::String path)
{
    path = path.replaceCharacter('\\', '/');
    return path.contains("/") ? path.fromLastOccurrenceOf("/", false, false) : path;
}

bool looksLikeAudio(const juce::String& path)
{
    const auto lower = path.toLowerCase();
    for (const auto* extension : { ".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".wma",
                                   ".m4a", ".aac", ".caf", ".wv", ".opus" })
        if (lower.endsWith(extension))
            return true;
    return false;
}

// Beats to seconds through a piecewise-linear map, the way warp markers
// describe an audio clip: past the last point, the last slope continues.
struct TimeMap {
    std::vector<std::pair<double, double>> points; // (beats, seconds)
    double fallbackSecondsPerBeat { 0.5 };

    [[nodiscard]] bool usable() const { return points.size() >= 2; }

    [[nodiscard]] double secondsAt(const double beats) const
    {
        if (points.empty())
            return beats * fallbackSecondsPerBeat;
        if (points.size() == 1)
            return points[0].second + (beats - points[0].first) * fallbackSecondsPerBeat;
        auto sorted = points;
        std::sort(sorted.begin(), sorted.end());
        if (beats <= sorted.front().first) {
            const auto& a = sorted[0];
            const auto& b = sorted[1];
            const auto slope = b.first > a.first ? (b.second - a.second) / (b.first - a.first)
                                                 : fallbackSecondsPerBeat;
            return a.second + (beats - a.first) * slope;
        }
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            if (beats <= sorted[i].first) {
                const auto& a = sorted[i - 1];
                const auto& b = sorted[i];
                const auto span = b.first - a.first;
                const auto t = span > 0.0 ? (beats - a.first) / span : 0.0;
                return a.second + t * (b.second - a.second);
            }
        }
        const auto& a = sorted[sorted.size() - 2];
        const auto& b = sorted.back();
        const auto slope = b.first > a.first ? (b.second - a.second) / (b.first - a.first)
                                             : fallbackSecondsPerBeat;
        return b.second + (beats - b.first) * slope;
    }
};

// A clip window on the timeline, cut from a content region that may loop:
// the callback receives each stretch of content (from, to) with the beat it
// starts at.
void unrollContent(const double contentStart, const double contentLength,
                   const bool looping, const double loopStart, const double loopEnd,
                   const std::function<void(double contentFrom, double contentTo,
                                            double timelineOffsetBeats)>& place)
{
    const auto loopLength = loopEnd - loopStart;
    if (!looping || loopLength <= 1.0e-6 || loopLength >= contentLength - 1.0e-6) {
        place(contentStart, contentStart + contentLength, 0.0);
        return;
    }
    // The first pass runs from the play start to the loop end, then the
    // loop repeats until the clip is full.
    auto offset = 0.0;
    auto from = contentStart;
    auto repeats = 0;
    while (offset < contentLength - 1.0e-6 && repeats < kMaximumLoopRepeats) {
        const auto to = std::min(loopEnd, from + (contentLength - offset));
        if (to <= from + 1.0e-6)
            break;
        place(from, to, offset);
        offset += to - from;
        from = loopStart;
        ++repeats;
    }
}

// ---------------------------------------------------------------------------
// DAWproject: a zip with project.xml and the embedded audio.

class DawProjectReader {
public:
    DawProjectReader(const juce::File& file, ImportedProject& out)
        : file_(file), out_(out), zip_(file), folder_(file.getParentDirectory())
    {
    }

    juce::Result read()
    {
        if (zip_.getNumEntries() == 0)
            return juce::Result::fail("Not a DAWproject file (no zip entries): "
                                      + file_.getFullPathName());
        const auto index = zip_.getIndexOfFileName("project.xml", true);
        if (index < 0)
            return juce::Result::fail("No project.xml inside " + file_.getFileName());
        std::unique_ptr<juce::InputStream> stream(zip_.createStreamForEntry(index));
        if (stream == nullptr)
            return juce::Result::fail("project.xml could not be read");
        const auto document = juce::XmlDocument::parse(shared::readBounded(*stream));
        if (document == nullptr || !document->hasTagName("Project"))
            return juce::Result::fail("project.xml is not a DAWproject document");

        out_.format = "dawproject";
        if (const auto* application = document->getChildByName("Application"))
            out_.application = (attributeText(application, "name") + " "
                                + attributeText(application, "version")).trim();
        if (const auto* transport = document->getChildByName("Transport")) {
            if (const auto* tempo = transport->getChildByName("Tempo"))
                out_.tempo = juce::jlimit(20.0, 300.0, attributeNumber(tempo, "value", 120.0));
            if (const auto* signature = transport->getChildByName("TimeSignature")) {
                out_.numerator = juce::jlimit(1, 32, static_cast<int>(attributeNumber(signature, "numerator", 4)));
                out_.denominator = juce::jlimit(1, 32, static_cast<int>(attributeNumber(signature, "denominator", 4)));
            }
        }
        if (const auto* structure = document->getChildByName("Structure"))
            for (const auto* track : structure->getChildIterator())
                readTrack(track, {});
        if (const auto* arrangement = document->getChildByName("Arrangement"))
            for (const auto* lanes : arrangement->getChildIterator())
                if (lanes->hasTagName("Lanes"))
                    readLanes(lanes, -1, "beats", -1);
        if (const auto* scenes = document->getChildByName("Scenes")) {
            auto sceneIndex = 0;
            for (const auto* scene : scenes->getChildIterator())
                if (scene->hasTagName("Scene"))
                    readScene(scene, sceneIndex++);
        }
        settleKinds();
        return juce::Result::ok();
    }

private:
    void readTrack(const juce::XmlElement* track, const juce::String& groupName)
    {
        if (track == nullptr || !track->hasTagName("Track"))
            return;
        const auto id = attributeText(track, "id");
        const auto name = attributeText(track, "name");
        const auto contentType = attributeText(track, "contentType").toLowerCase();
        const auto* channel = track->getChildByName("Channel");
        const auto role = attributeText(channel, "role").toLowerCase();
        const auto hasChildren = track->getChildByName("Track") != nullptr;

        if (role == "master") {
            out_.masterGainDb = channelGain(channel);
            return;
        }
        if (role == "effect") {
            out_.warnings.add("Effect track \"" + name + "\" was left out: CyDAW has no return tracks.");
            return;
        }
        if (hasChildren) {
            // A group: its children come in flat, named after it.
            for (const auto* child : track->getChildIterator())
                if (child->hasTagName("Track"))
                    readTrack(child, name);
            if (attributeText(track, "loaded") == "false" || contentType.isEmpty())
                return;
        }

        ImportedTrack imported;
        imported.name = groupName.isNotEmpty() && name.isNotEmpty() ? groupName + " / " + name
                        : name.isNotEmpty() ? name : groupName;
        imported.colour = colourFromHex(attributeText(track, "color"));
        imported.midi = contentType.contains("notes");
        kindDecided_.push_back(contentType.isNotEmpty());
        if (channel != nullptr) {
            imported.gainDb = channelGain(channel);
            if (const auto* pan = channel->getChildByName("Pan")) {
                const auto unit = attributeText(pan, "unit").toLowerCase();
                const auto value = attributeNumber(pan, "value", 0.5);
                imported.pan = static_cast<float>(juce::jlimit(-1.0, 1.0,
                    unit == "normalized" ? value * 2.0 - 1.0 : value));
            }
            if (const auto* mute = channel->getChildByName("Mute"))
                imported.muted = attributeBool(mute, "value", false);
            imported.solo = attributeBool(channel, "solo", false);
            if (const auto* devices = channel->getChildByName("Devices"))
                for (const auto* device : devices->getChildIterator()) {
                    auto deviceName = attributeText(device, "deviceName");
                    if (deviceName.isEmpty())
                        deviceName = attributeText(device, "name", device->getTagName());
                    imported.devices.add(deviceName);
                    if (attributeText(device, "deviceRole").equalsIgnoreCase("instrument"))
                        imported.hadInstrument = true;
                    ImportedDevice entry;
                    entry.name = deviceName;
                    entry.vendor = attributeText(device, "deviceVendor");
                    const auto tag = device->getTagName();
                    entry.plugin = tag.endsWithIgnoreCase("Plugin");
                    entry.format = tag.startsWith("Vst3") ? "VST3" : tag.startsWith("Vst2") ? "VST2" : tag.startsWith("Clap") ? "CLAP"
                                   : tag.startsWith("Au") ? "AU" : juce::String();
                    entry.type = entry.plugin ? juce::String() : tag;
                    const auto role = attributeText(device, "deviceRole");
                    entry.instrument = role.equalsIgnoreCase("instrument");
                    entry.noteEffect = role.equalsIgnoreCase("noteFX");
                    imported.chain.push_back(std::move(entry));
                }
        }
        const auto index = static_cast<int>(out_.tracks.size());
        out_.tracks.push_back(std::move(imported));
        if (id.isNotEmpty())
            trackIndexById_[id] = index;
        if (const auto channelId = attributeText(channel, "id"); channelId.isNotEmpty())
            trackIndexById_[channelId] = index;
    }

    static float channelGain(const juce::XmlElement* channel)
    {
        const auto* volume = channel != nullptr ? channel->getChildByName("Volume") : nullptr;
        if (volume == nullptr)
            return 0.0f;
        const auto unit = attributeText(volume, "unit").toLowerCase();
        const auto value = attributeNumber(volume, "value", 1.0);
        if (unit == "decibel" || unit == "db")
            return static_cast<float>(juce::jlimit(-60.0, 12.0, value));
        return linearToDb(value);
    }

    int trackIndexFor(const juce::XmlElement* element, const int inherited) const
    {
        const auto reference = attributeText(element, "track");
        if (reference.isEmpty())
            return inherited;
        const auto found = trackIndexById_.find(reference);
        return found != trackIndexById_.end() ? found->second : -2;
    }

    void readLanes(const juce::XmlElement* lanes, const int inheritedTrack,
                   const juce::String& inheritedUnit, const int scene)
    {
        const auto track = trackIndexFor(lanes, inheritedTrack);
        const auto unit = attributeText(lanes, "timeUnit", inheritedUnit).toLowerCase();
        for (const auto* child : lanes->getChildIterator()) {
            if (child->hasTagName("Lanes")) {
                readLanes(child, track, unit, scene);
            } else if (child->hasTagName("Clips")) {
                for (const auto* clip : child->getChildIterator())
                    if (clip->hasTagName("Clip"))
                        readClip(clip, trackIndexFor(child, track), unit, 0.0, scene, -1.0);
            } else if (child->hasTagName("ClipSlot")) {
                const auto slotTrack = trackIndexFor(child, track);
                for (const auto* clip : child->getChildIterator())
                    if (clip->hasTagName("Clip"))
                        readClip(clip, slotTrack, unit, 0.0, scene, -1.0);
            } else if (child->hasTagName("Clip")) {
                readClip(child, track, unit, 0.0, scene, -1.0);
            }
        }
    }

    void readScene(const juce::XmlElement* scene, const int sceneIndex)
    {
        for (const auto* child : scene->getChildIterator()) {
            if (child->hasTagName("Lanes"))
                readLanes(child, -1, "beats", sceneIndex);
            else if (child->hasTagName("ClipSlot")) {
                const auto track = trackIndexFor(child, -1);
                for (const auto* clip : child->getChildIterator())
                    if (clip->hasTagName("Clip"))
                        readClip(clip, track, "beats", 0.0, sceneIndex, -1.0);
            }
        }
    }

    double toBeats(const double value, const juce::String& unit) const
    {
        return unit == "seconds" ? out_.secondsToBeats(value) : value;
    }

    // A clip: notes, audio through warps, plain audio, or clips inside it.
    // offsetBeats places nested content; limitBeats caps it to the outer
    // clip, -1 for none.
    void readClip(const juce::XmlElement* clip, const int track, const juce::String& unit,
                  const double offsetBeats, const int scene, const double limitBeats)
    {
        if (track < 0) {
            if (track == -2)
                out_.warnings.add("A clip refers to a track that was not imported: \""
                                  + attributeText(clip, "name") + "\".");
            return;
        }
        auto& imported = out_.tracks[static_cast<std::size_t>(track)];
        const auto contentUnit = attributeText(clip, "contentTimeUnit", unit).toLowerCase();
        auto startBeats = toBeats(attributeNumber(clip, "time", 0.0), unit) + offsetBeats;
        auto durationBeats = toBeats(attributeNumber(clip, "duration", 0.0), unit);
        if (scene >= 0)
            startBeats = scene * kLauncherSceneBeats;
        if (limitBeats >= 0.0)
            durationBeats = std::min(durationBeats, limitBeats - (startBeats - offsetBeats));
        if (durationBeats <= 1.0e-6 && clip->getChildByName("Notes") == nullptr)
            return;
        const auto playStart = attributeNumber(clip, "playStart", 0.0);
        const auto loopStart = attributeNumber(clip, "loopStart", -1.0);
        const auto loopEnd = attributeNumber(clip, "loopEnd", -1.0);
        const auto looping = loopStart >= 0.0 && loopEnd > loopStart;
        const auto name = attributeText(clip, "name");
        const auto colour = colourFromHex(attributeText(clip, "color"));
        const auto muted = !attributeBool(clip, "enable", true);
        const auto fadeIn = attributeNumber(clip, "fadeInTime", 0.0);
        const auto fadeOut = attributeNumber(clip, "fadeOutTime", 0.0);

        if (const auto* notes = clip->getChildByName("Notes")) {
            ImportedMidiClip midiClip;
            midiClip.name = name;
            midiClip.startBeats = startBeats;
            midiClip.lengthBeats = std::max(0.25, durationBeats > 0.0 ? durationBeats : 16.0);
            midiClip.looping = looping && scene >= 0;
            midiClip.muted = muted;
            midiClip.colour = colour;
            midiClip.launcherScene = scene;
            const auto noteUnit = attributeText(notes, "timeUnit", contentUnit).toLowerCase();
            std::vector<ImportedNote> source;
            for (const auto* note : notes->getChildIterator()) {
                if (!note->hasTagName("Note"))
                    continue;
                ImportedNote parsed;
                parsed.note = juce::jlimit(0, 127, static_cast<int>(attributeNumber(note, "key", 60)));
                parsed.startBeats = toBeats(attributeNumber(note, "time", 0.0), noteUnit);
                parsed.lengthBeats = std::max(0.01, toBeats(attributeNumber(note, "duration", 1.0), noteUnit));
                parsed.velocity = static_cast<float>(juce::jlimit(0.01, 1.0, attributeNumber(note, "vel", 0.8)));
                source.push_back(parsed);
            }
            const auto contentStart = toBeats(playStart, contentUnit);
            unrollContent(contentStart, midiClip.lengthBeats, looping,
                          toBeats(loopStart, contentUnit), toBeats(loopEnd, contentUnit),
                          [&](const double from, const double to, const double offset) {
                              for (const auto& note : source)
                                  if (note.startBeats >= from - 1.0e-6 && note.startBeats < to - 1.0e-6) {
                                      auto placed = note;
                                      placed.startBeats = note.startBeats - from + offset;
                                      placed.lengthBeats = std::min(note.lengthBeats,
                                                                    midiClip.lengthBeats - placed.startBeats);
                                      if (placed.lengthBeats > 0.01)
                                          midiClip.notes.push_back(placed);
                                  }
                          });
            imported.midiClips.push_back(std::move(midiClip));
            if (!imported.midi && !kindDecided_[static_cast<std::size_t>(track)])
                imported.midi = true;
        }

        const auto* warps = clip->getChildByName("Warps");
        const auto* audio = warps != nullptr ? warps->getChildByName("Audio") : clip->getChildByName("Audio");
        if (audio != nullptr) {
            const auto file = resolveAudio(audio->getChildByName("File"));
            ImportedAudioClip audioClip;
            audioClip.name = name;
            audioClip.file = file;
            audioClip.originalPath = attributeText(audio->getChildByName("File"), "path");
            audioClip.colour = colour;
            audioClip.muted = muted;
            audioClip.looping = looping;
            audioClip.launcherScene = scene;
            audioClip.fadeInSeconds = contentUnit == "seconds" ? fadeIn : out_.beatsToSeconds(fadeIn);
            audioClip.fadeOutSeconds = contentUnit == "seconds" ? fadeOut : out_.beatsToSeconds(fadeOut);
            const auto sampleSeconds = attributeNumber(audio, "duration", 0.0);
            if (warps != nullptr) {
                // Content in beats, mapped to seconds of audio by the warps.
                TimeMap map;
                map.fallbackSecondsPerBeat = 60.0 / out_.tempo;
                for (const auto* warp : warps->getChildIterator())
                    if (warp->hasTagName("Warp"))
                        map.points.emplace_back(attributeNumber(warp, "time", 0.0),
                                                attributeNumber(warp, "contentTime", 0.0));
                const auto contentStart = toBeats(playStart, contentUnit);
                const auto contentLength = durationBeats;
                unrollContent(contentStart, contentLength, looping && scene < 0,
                              toBeats(loopStart, contentUnit), toBeats(loopEnd, contentUnit),
                              [&](const double from, const double to, const double offset) {
                                  auto piece = audioClip;
                                  piece.startSeconds = out_.beatsToSeconds(startBeats + offset);
                                  piece.durationSeconds = out_.beatsToSeconds(to - from);
                                  piece.sourceOffsetSeconds = std::max(0.0, map.secondsAt(from));
                                  const auto sourceEnd = map.secondsAt(to);
                                  const auto sourceLength = sourceEnd - piece.sourceOffsetSeconds;
                                  if (sourceLength > 0.0
                                      && std::abs(sourceLength - piece.durationSeconds) > 0.01 * piece.durationSeconds)
                                      piece.sourceDurationSeconds = sourceLength;
                                  if (sampleSeconds > 0.0 && piece.sourceOffsetSeconds >= sampleSeconds)
                                      return;
                                  imported.audioClips.push_back(piece);
                              });
            } else {
                // Content in seconds: the clip plays the file from playStart.
                audioClip.startSeconds = out_.beatsToSeconds(startBeats);
                audioClip.durationSeconds = contentUnit == "seconds" && unit == "seconds"
                                                ? attributeNumber(clip, "duration", 0.0)
                                                : out_.beatsToSeconds(durationBeats);
                audioClip.sourceOffsetSeconds = std::max(0.0, contentUnit == "seconds"
                                                                  ? playStart
                                                                  : out_.beatsToSeconds(playStart));
                if (sampleSeconds > 0.0) {
                    const auto available = sampleSeconds - audioClip.sourceOffsetSeconds;
                    if (available <= 0.0)
                        return;
                    if (!looping)
                        audioClip.durationSeconds = std::min(audioClip.durationSeconds, available);
                }
                imported.audioClips.push_back(audioClip);
            }
            if (imported.midi && !kindDecided_[static_cast<std::size_t>(track)]
                && imported.midiClips.empty())
                imported.midi = false;
        }

        if (const auto* inner = clip->getChildByName("Clips")) {
            const auto innerOffset = startBeats - toBeats(playStart, contentUnit);
            for (const auto* nested : inner->getChildIterator())
                if (nested->hasTagName("Clip"))
                    readClip(nested, track, contentUnit, innerOffset, scene,
                             durationBeats > 0.0 ? startBeats + durationBeats : -1.0);
        }
    }

    juce::File resolveAudio(const juce::XmlElement* fileElement)
    {
        if (fileElement == nullptr)
            return {};
        const auto path = attributeText(fileElement, "path");
        if (path.isEmpty())
            return {};
        const auto external = attributeBool(fileElement, "external", false);
        if (!external) {
            const auto found = extracted_.find(path);
            if (found != extracted_.end())
                return found->second;
            const auto index = zip_.getIndexOfFileName(path, true);
            if (index >= 0) {
                if (mediaFolder_ == juce::File {}) {
                    mediaFolder_ = folder_.getChildFile(file_.getFileNameWithoutExtension() + "-media");
                    (void) mediaFolder_.createDirectory();
                }
                // Entries of one name in different folders get files of their own.
                auto target = mediaFolder_.getChildFile(fileNameOf(path));
                for (auto n = 2; claimedMedia_.count(target.getFullPathName()) > 0; ++n)
                    target = mediaFolder_.getChildFile(juce::File(fileNameOf(path)).getFileNameWithoutExtension() + "-"
                                                       + juce::String(n) + juce::File(fileNameOf(path)).getFileExtension());
                claimedMedia_.insert(target.getFullPathName());
                std::unique_ptr<juce::InputStream> stream(zip_.createStreamForEntry(index));
                if (stream != nullptr) {
                    // Extracted before from this very entry (same size and
                    // date): kept. A stem rendered again has another date.
                    const auto* entry = zip_.getEntry(index);
                    const auto same = target.existsAsFile() && entry != nullptr
                                      && target.getSize() == entry->uncompressedSize
                                      && target.getLastModificationTime() == entry->fileTime;
                    if (!same) {
                        (void) target.deleteFile();
                        {
                            juce::FileOutputStream output(target);
                            if (output.openedOk())
                                output.writeFromInputStream(*stream, -1);
                        }
                        if (entry != nullptr)
                            (void) target.setLastModificationTime(entry->fileTime);
                    }
                    if (target.existsAsFile()) {
                        extracted_[path] = target;
                        return target;
                    }
                }
            }
        }
        const auto resolved = resolveProjectAudioFile(folder_, path);
        if (resolved == juce::File {})
            out_.missingFiles.addIfNotAlreadyThere(path);
        return resolved;
    }

    void settleKinds()
    {
        for (std::size_t i = 0; i < out_.tracks.size(); ++i) {
            auto& track = out_.tracks[i];
            if (!kindDecided_[i])
                track.midi = !track.midiClips.empty() || (track.audioClips.empty() && track.hadInstrument);
        }
    }

    juce::File file_;
    ImportedProject& out_;
    juce::ZipFile zip_;
    juce::File folder_;
    juce::File mediaFolder_;
    std::map<juce::String, int> trackIndexById_;
    std::map<juce::String, juce::File> extracted_;
    std::set<juce::String> claimedMedia_;
    std::vector<bool> kindDecided_;
};

// ---------------------------------------------------------------------------
// Ableton Live: a gzip-compressed XML document.

class AbletonReader {
public:
    AbletonReader(const juce::File& file, ImportedProject& out)
        : file_(file), out_(out), folder_(file.getParentDirectory())
    {
    }

    juce::Result read()
    {
        std::unique_ptr<juce::XmlElement> document;
        {
            juce::FileInputStream input(file_);
            if (!input.openedOk())
                return juce::Result::fail("The file could not be opened: " + file_.getFullPathName());
            juce::GZIPDecompressorInputStream unzipped(
                &input, false, juce::GZIPDecompressorInputStream::gzipFormat);
            document = juce::XmlDocument::parse(shared::readBounded(unzipped));
        }
        if (document == nullptr)
            document = juce::XmlDocument::parse(file_);
        if (document == nullptr || !document->hasTagName("Ableton"))
            return juce::Result::fail("Not an Ableton Live set: " + file_.getFileName());
        const auto* liveSet = document->getChildByName("LiveSet");
        if (liveSet == nullptr)
            return juce::Result::fail("The Live set holds no LiveSet element");

        out_.format = "ableton";
        out_.application = attributeText(document.get(), "Creator", "Ableton Live");
        const auto* master = liveSet->getChildByName("MasterTrack");
        if (master == nullptr)
            master = liveSet->getChildByName("MainTrack");
        if (master != nullptr) {
            out_.tempo = juce::jlimit(20.0, 300.0,
                                      numberAt(master, "DeviceChain/Mixer/Tempo/Manual", 120.0));
            out_.masterGainDb = linearToDb(numberAt(master, "DeviceChain/Mixer/Volume/Manual", 1.0));
            const auto* signature = childPath(master, "DeviceChain/Mixer/TimeSignature/TimeSignatures/RemoteableTimeSignature");
            if (signature != nullptr) {
                out_.numerator = juce::jlimit(1, 32, static_cast<int>(numberAt(signature, "Numerator", 4)));
                out_.denominator = juce::jlimit(1, 32, static_cast<int>(numberAt(signature, "Denominator", 4)));
            }
        }
        if (const auto* tracks = liveSet->getChildByName("Tracks"))
            for (const auto* track : tracks->getChildIterator())
                readTrack(track);
        return juce::Result::ok();
    }

private:
    void readTrack(const juce::XmlElement* track)
    {
        const auto tag = track->getTagName();
        auto name = valueAt(track, "Name/EffectiveName");
        if (name.isEmpty())
            name = valueAt(track, "Name/UserName");
        if (tag == "ReturnTrack") {
            out_.warnings.add("Return track \"" + name + "\" was left out: CyDAW has no return tracks.");
            return;
        }
        if (tag == "GroupTrack") {
            out_.warnings.add("Group \"" + name + "\" was flattened: its tracks come in on their own.");
            return;
        }
        if (tag != "AudioTrack" && tag != "MidiTrack")
            return;

        ImportedTrack imported;
        imported.name = name;
        imported.midi = tag == "MidiTrack";
        const auto colourIndex = static_cast<int>(numberAt(track, "Color", numberAt(track, "ColorIndex", -1)));
        imported.colour = abletonColour(colourIndex);
        const auto* mixer = childPath(track, "DeviceChain/Mixer");
        imported.gainDb = linearToDb(numberAt(mixer, "Volume/Manual", 1.0));
        imported.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, numberAt(mixer, "Pan/Manual", 0.0)));
        imported.muted = !boolAt(mixer, "Speaker/Manual", true);
        if (const auto* devices = childPath(track, "DeviceChain/DeviceChain/Devices"))
            for (const auto* device : devices->getChildIterator()) {
                auto deviceName = device->getTagName();
                for (const auto* path : { "PluginDesc/VstPluginInfo/PlugName", "PluginDesc/Vst3PluginInfo/Name",
                                          "PluginDesc/AuPluginInfo/Name", "PluginDesc/ClapPluginInfo/Name",
                                          "UserName" })
                    if (const auto plugin = valueAt(device, path); plugin.isNotEmpty()) {
                        deviceName = plugin;
                        break;
                    }
                imported.devices.add(deviceName);
                ImportedDevice entry;
                entry.name = deviceName;
                const auto tag = device->getTagName();
                entry.plugin = tag == "PluginDevice" || tag == "AuPluginDevice";
                entry.format = valueAt(device, "PluginDesc/Vst3PluginInfo/Name").isNotEmpty()   ? "VST3"
                               : valueAt(device, "PluginDesc/VstPluginInfo/PlugName").isNotEmpty() ? "VST2"
                               : valueAt(device, "PluginDesc/ClapPluginInfo/Name").isNotEmpty()    ? "CLAP"
                               : tag == "AuPluginDevice"                                            ? "AU"
                                                                                                    : juce::String();
                entry.vendor = valueAt(device, "PluginDesc/Vst3PluginInfo/DeviceCreator");
                entry.type = entry.plugin ? juce::String() : tag;
                // Live's own instruments, by their tag; a plug-in on a MIDI
                // track before any effect is its instrument.
                static const juce::StringArray instruments { "Operator", "UltraAnalog", "Collision", "Drift", "OriginalSimpler",
                                                              "MultiSampler", "InstrumentVector", "InstrumentMeld", "LoungeLizard",
                                                              "StringStudio", "Tension", "Electric", "InstrumentImpulse",
                                                              "DrumGroupDevice", "InstrumentGroupDevice", "Sampler", "Simpler" };
                static const juce::StringArray noteEffects { "MidiArpeggiator", "MidiChord", "MidiNoteLength", "MidiPitcher",
                                                              "MidiRandom", "MidiScale", "MidiVelocity" };
                entry.noteEffect = noteEffects.contains(tag);
                const auto hasInstrument = std::any_of(imported.chain.begin(), imported.chain.end(),
                                                       [](const ImportedDevice& earlier) { return earlier.instrument; });
                entry.instrument = imported.midi && !hasInstrument && !entry.noteEffect
                                   && (instruments.contains(tag) || (entry.plugin && imported.chain.empty())
                                       || (entry.plugin && std::all_of(imported.chain.begin(), imported.chain.end(),
                                                                       [](const ImportedDevice& earlier) { return earlier.noteEffect; })));
                // Its settings: each element with a <Manual Value="…"/>, by
                // its path in the device ("Operator.0/Envelope/AttackTime").
                if (!entry.plugin) {
                    std::function<void(const juce::XmlElement*, const juce::String&, int)> walk
                        = [&](const juce::XmlElement* element, const juce::String& path, const int depth) {
                              if (depth > 8 || entry.settings.size() > 600)
                                  return;
                              for (const auto* item : element->getChildIterator()) {
                                  const auto tagName = item->getTagName();
                                  const auto itemPath = path.isEmpty() ? tagName : path + "/" + tagName;
                                  if (const auto* manual = item->getChildByName("Manual"); manual != nullptr && manual->hasAttribute("Value")) {
                                      const auto text = manual->getStringAttribute("Value");
                                      entry.settings.emplace_back(itemPath, text == "true" ? 1.0 : text == "false" ? 0.0 : text.getDoubleValue());
                                  } else if (tagName != "Manual" && tagName != "AutomationTarget" && tagName != "ModulationTarget") {
                                      walk(item, itemPath, depth + 1);
                                  }
                              }
                          };
                    walk(device, {}, 0);
                }
                imported.chain.push_back(std::move(entry));
            }
        imported.hadInstrument = imported.midi && !imported.devices.isEmpty();

        if (const auto* events = childPath(track, "DeviceChain/MainSequencer/ClipTimeable/ArrangerAutomation/Events"))
            for (const auto* clip : events->getChildIterator())
                readClip(clip, imported, -1);
        if (const auto* slots = childPath(track, "DeviceChain/MainSequencer/ClipSlotList")) {
            auto scene = 0;
            for (const auto* slot : slots->getChildIterator()) {
                if (!slot->hasTagName("ClipSlot"))
                    continue;
                if (const auto* clip = findClip(slot, 4))
                    readClip(clip, imported, scene);
                ++scene;
            }
        }
        out_.tracks.push_back(std::move(imported));
    }

    static const juce::XmlElement* findClip(const juce::XmlElement* element, const int depth)
    {
        if (element == nullptr || depth < 0)
            return nullptr;
        for (const auto* child : element->getChildIterator()) {
            if (child->hasTagName("AudioClip") || child->hasTagName("MidiClip"))
                return child;
            if (const auto* found = findClip(child, depth - 1))
                return found;
        }
        return nullptr;
    }

    void readClip(const juce::XmlElement* clip, ImportedTrack& track, const int scene)
    {
        const auto isMidi = clip->hasTagName("MidiClip");
        const auto isAudio = clip->hasTagName("AudioClip");
        if (!isMidi && !isAudio)
            return;
        const auto currentStart = numberAt(clip, "CurrentStart", 0.0);
        const auto currentEnd = numberAt(clip, "CurrentEnd", currentStart + 16.0);
        const auto lengthBeats = std::max(0.25, currentEnd - currentStart);
        const auto startBeats = scene >= 0 ? scene * kLauncherSceneBeats
                                           : std::max(0.0, attributeNumber(clip, "Time", 0.0));
        const auto name = valueAt(clip, "Name");
        const auto muted = boolAt(clip, "Disabled", false);
        const auto colour = abletonColour(static_cast<int>(numberAt(clip, "Color", numberAt(clip, "ColorIndex", -1))));
        const auto loopOn = boolAt(clip, "Loop/LoopOn", false);
        const auto loopStart = numberAt(clip, "Loop/LoopStart", currentStart);
        const auto loopEnd = numberAt(clip, "Loop/LoopEnd", currentEnd);

        if (isMidi) {
            ImportedMidiClip midiClip;
            midiClip.name = name;
            midiClip.startBeats = startBeats;
            midiClip.lengthBeats = lengthBeats;
            midiClip.looping = loopOn && scene >= 0;
            midiClip.muted = muted;
            midiClip.colour = colour;
            midiClip.launcherScene = scene;
            std::vector<ImportedNote> source;
            if (const auto* keyTracks = childPath(clip, "Notes/KeyTracks"))
                for (const auto* keyTrack : keyTracks->getChildIterator()) {
                    const auto key = juce::jlimit(0, 127, static_cast<int>(numberAt(keyTrack, "MidiKey", 60)));
                    const auto* notes = keyTrack->getChildByName("Notes");
                    if (notes == nullptr)
                        continue;
                    for (const auto* event : notes->getChildIterator()) {
                        if (!event->hasTagName("MidiNoteEvent") || !attributeBool(event, "IsEnabled", true))
                            continue;
                        ImportedNote note;
                        note.note = key;
                        note.startBeats = attributeNumber(event, "Time", 0.0);
                        note.lengthBeats = std::max(0.01, attributeNumber(event, "Duration", 0.25));
                        note.velocity = static_cast<float>(juce::jlimit(0.01, 1.0, attributeNumber(event, "Velocity", 100.0) / 127.0));
                        source.push_back(note);
                    }
                }
            unrollContent(currentStart, lengthBeats, loopOn, loopStart, loopEnd,
                          [&](const double from, const double to, const double offset) {
                              for (const auto& note : source)
                                  if (note.startBeats >= from - 1.0e-6 && note.startBeats < to - 1.0e-6) {
                                      auto placed = note;
                                      placed.startBeats = note.startBeats - from + offset;
                                      placed.lengthBeats = std::min(note.lengthBeats, lengthBeats - placed.startBeats);
                                      if (placed.lengthBeats > 0.01)
                                          midiClip.notes.push_back(placed);
                                  }
                          });
            track.midiClips.push_back(std::move(midiClip));
            return;
        }

        // Audio: the file, then the part of it the clip plays.
        const auto* fileRef = childPath(clip, "SampleRef/FileRef");
        auto absolute = valueAt(fileRef, "Path");
        auto relative = valueAt(fileRef, "RelativePath");
        const auto fileName = valueAt(fileRef, "Name");
        if (relative.isEmpty() && fileRef != nullptr)
            if (const auto* elements = fileRef->getChildByName("RelativePath")) {
                juce::StringArray parts;
                for (const auto* element : elements->getChildIterator())
                    if (element->hasTagName("RelativePathElement"))
                        parts.add(attributeText(element, "Dir"));
                parts.removeEmptyStrings();
                if (fileName.isNotEmpty())
                    parts.add(fileName);
                relative = parts.joinIntoString("/");
            }
        juce::File file;
        if (absolute.isNotEmpty())
            file = resolveProjectAudioFile(folder_, absolute);
        if (file == juce::File {} && relative.isNotEmpty())
            file = resolveProjectAudioFile(folder_, relative);
        if (file == juce::File {} && fileName.isNotEmpty())
            file = resolveProjectAudioFile(folder_, fileName);
        const auto original = absolute.isNotEmpty() ? absolute : relative.isNotEmpty() ? relative : fileName;
        if (file == juce::File {}) {
            out_.missingFiles.addIfNotAlreadyThere(original.isNotEmpty() ? original : name);
            return;
        }

        TimeMap map;
        map.fallbackSecondsPerBeat = 60.0 / out_.tempo;
        const auto warped = boolAt(clip, "IsWarped", true);
        if (const auto* markers = clip->getChildByName("WarpMarkers"))
            for (const auto* marker : markers->getChildIterator())
                if (marker->hasTagName("WarpMarker"))
                    map.points.emplace_back(attributeNumber(marker, "BeatTime", 0.0),
                                            attributeNumber(marker, "SecTime", 0.0));
        ImportedAudioClip audioClip;
        audioClip.name = name;
        audioClip.file = file;
        audioClip.originalPath = original;
        audioClip.colour = colour;
        audioClip.muted = muted;
        audioClip.looping = loopOn;
        audioClip.launcherScene = scene;
        audioClip.fadeInSeconds = std::max(0.0, numberAt(clip, "Fades/FadeInLength", 0.0));
        audioClip.fadeOutSeconds = std::max(0.0, numberAt(clip, "Fades/FadeOutLength", 0.0));
        unrollContent(currentStart, lengthBeats, loopOn && scene < 0, loopStart, loopEnd,
                      [&](const double from, const double to, const double offset) {
                          auto piece = audioClip;
                          piece.startSeconds = out_.beatsToSeconds(startBeats + offset);
                          piece.durationSeconds = out_.beatsToSeconds(to - from);
                          if (warped && map.usable()) {
                              piece.sourceOffsetSeconds = std::max(0.0, map.secondsAt(from));
                              const auto sourceLength = map.secondsAt(to) - piece.sourceOffsetSeconds;
                              if (sourceLength > 0.0
                                  && std::abs(sourceLength - piece.durationSeconds) > 0.01 * piece.durationSeconds)
                                  piece.sourceDurationSeconds = sourceLength;
                          } else {
                              piece.sourceOffsetSeconds = std::max(0.0, out_.beatsToSeconds(from));
                          }
                          piece.fadeInSeconds = std::min(piece.fadeInSeconds, piece.durationSeconds * 0.5);
                          piece.fadeOutSeconds = std::min(piece.fadeOutSeconds, piece.durationSeconds * 0.5);
                          track.audioClips.push_back(piece);
                      });
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
};

// ---------------------------------------------------------------------------
// Studio One: a zip of XML documents. The layout is not published, so the
// reader looks for what a song is made of by name — tracks, audio events,
// music parts, notes — and says so.

class StudioOneReader {
public:
    StudioOneReader(const juce::File& file, ImportedProject& out)
        : file_(file), out_(out), folder_(file.getParentDirectory())
    {
    }

    juce::Result read()
    {
        juce::ZipFile zip(file_);
        if (zip.getNumEntries() == 0)
            return juce::Result::fail("Not a Studio One song (no zip entries): " + file_.getFullPathName());
        std::vector<std::pair<juce::String, std::unique_ptr<juce::XmlElement>>> documents;
        for (auto index = 0; index < zip.getNumEntries(); ++index) {
            const auto* entry = zip.getEntry(index);
            if (entry == nullptr || !entry->filename.endsWithIgnoreCase(".xml"))
                continue;
            std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
            if (stream == nullptr)
                continue;
            if (auto parsed = juce::XmlDocument::parse(shared::readBounded(*stream)))
                documents.emplace_back(entry->filename, std::move(parsed));
        }
        if (documents.empty())
            return juce::Result::fail("No XML document inside " + file_.getFileName());

        out_.format = "studio-one";
        out_.application = "Studio One";
        const juce::XmlElement* song = nullptr;
        for (const auto& [name, document] : documents) {
            if (name.endsWithIgnoreCase("metainfo.xml"))
                readMetaInfo(document.get());
            if (document->hasTagName("Song") || name.endsWithIgnoreCase("song.xml"))
                song = document.get();
            collectMedia(document.get());
        }
        if (song == nullptr)
            song = documents.front().second.get();
        readTempo(song);
        readTracks(song);
        if (out_.tracks.empty())
            for (const auto& [name, document] : documents)
                if (document.get() != song)
                    readTracks(document.get());
        out_.warnings.add("Studio One songs are read by best effort; the DAWproject export "
                          "of Studio One is the reliable path.");
        return juce::Result::ok();
    }

private:
    void readMetaInfo(const juce::XmlElement* document)
    {
        for (const auto* attribute : document->getChildIterator()) {
            const auto id = attributeText(attribute, "id");
            if (id.equalsIgnoreCase("Document:Generator") || id.equalsIgnoreCase("Generator"))
                out_.application = attributeText(attribute, "value", out_.application).replaceCharacter('/', ' ');
            if (id.equalsIgnoreCase("Media:Title") || id.equalsIgnoreCase("Title"))
                out_.name = attributeText(attribute, "value");
        }
    }

    // Every element with an audio path becomes a media entry, by its id and
    // by its file name.
    void collectMedia(const juce::XmlElement* element, const juce::XmlElement* parent = nullptr)
    {
        for (const auto* attributeName : { "url", "path", "Url", "Path", "file", "File" }) {
            const auto value = attributeText(element, attributeName);
            if (value.isNotEmpty() && looksLikeAudio(value)) {
                for (const auto* idName : { "x:id", "id", "mediaID", "clipID", "uid" })
                    if (const auto id = attributeText(element, idName); id.isNotEmpty())
                        mediaById_[id] = value;
                mediaByName_[fileNameOf(value).toLowerCase()] = value;
                for (const auto* idName : { "x:id", "id", "mediaID", "uid" })
                    if (const auto id = attributeText(parent, idName); id.isNotEmpty())
                        mediaById_.emplace(id, value);
            }
        }
        for (const auto* child : element->getChildIterator())
            collectMedia(child, element);
    }

    void readTempo(const juce::XmlElement* element)
    {
        if (element == nullptr)
            return;
        if (element->hasAttribute("tempo")) {
            const auto tempo = attributeNumber(element, "tempo", 0.0);
            if (tempo >= 20.0 && tempo <= 300.0) {
                out_.tempo = tempo;
                tempoFound_ = true;
            }
        }
        const auto id = attributeText(element, "x:id", attributeText(element, "id"));
        if (!tempoFound_ && id.equalsIgnoreCase("Tempo")) {
            const auto value = attributeNumber(element, "value", attributeNumber(element, "bpm", 0.0));
            if (value >= 20.0 && value <= 300.0) {
                out_.tempo = value;
                tempoFound_ = true;
            }
        }
        if (element->hasAttribute("timeSigNumerator") || element->hasAttribute("numerator")) {
            out_.numerator = juce::jlimit(1, 32, static_cast<int>(attributeNumber(
                element, "timeSigNumerator", attributeNumber(element, "numerator", 4))));
            out_.denominator = juce::jlimit(1, 32, static_cast<int>(attributeNumber(
                element, "timeSigDenominator", attributeNumber(element, "denominator", 4))));
        }
        for (const auto* child : element->getChildIterator())
            if (!tempoFound_)
                readTempo(child);
    }

    static bool isTrackElement(const juce::XmlElement* element)
    {
        const auto tag = element->getTagName();
        return (tag == "MediaTrack" || tag == "AudioTrack" || tag == "MusicTrack"
                || tag == "InstrumentTrack" || tag == "Track")
               && element->hasAttribute("name");
    }

    void readTracks(const juce::XmlElement* element)
    {
        if (element == nullptr)
            return;
        if (isTrackElement(element)) {
            readTrack(element);
            return;
        }
        for (const auto* child : element->getChildIterator())
            readTracks(child);
    }

    void readTrack(const juce::XmlElement* element)
    {
        ImportedTrack track;
        track.name = attributeText(element, "name");
        const auto tag = element->getTagName();
        const auto mediaType = attributeText(element, "mediaType", attributeText(element, "type")).toLowerCase();
        track.midi = tag == "MusicTrack" || tag == "InstrumentTrack" || mediaType.contains("music")
                     || mediaType.contains("instrument") || mediaType.contains("midi");
        track.colour = colourFromHex(attributeText(element, "color"));
        track.muted = attributeBool(element, "mute", false);
        track.solo = attributeBool(element, "solo", false);
        if (element->hasAttribute("volume"))
            track.gainDb = linearToDb(attributeNumber(element, "volume", 1.0));
        if (element->hasAttribute("pan"))
            track.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, attributeNumber(element, "pan", 0.0)));
        readEvents(element, track);
        if (!track.midiClips.empty() && track.audioClips.empty())
            track.midi = true;
        if (track.midiClips.empty() && !track.audioClips.empty())
            track.midi = false;
        out_.tracks.push_back(std::move(track));
    }

    void readEvents(const juce::XmlElement* element, ImportedTrack& track)
    {
        for (const auto* child : element->getChildIterator()) {
            const auto tag = child->getTagName();
            if (tag == "AudioEvent" || tag == "AudioClipEvent") {
                readAudioEvent(child, track);
            } else if (tag == "MusicPart" || tag == "MusicEvent" || tag == "NotePart") {
                readMusicPart(child, track);
            } else if (!isTrackElement(child)) {
                readEvents(child, track);
            }
        }
    }

    void readAudioEvent(const juce::XmlElement* event, ImportedTrack& track)
    {
        ImportedAudioClip clip;
        clip.name = attributeText(event, "name");
        clip.startSeconds = std::max(0.0, attributeNumber(event, "start", 0.0));
        clip.durationSeconds = std::max(0.01, attributeNumber(event, "length", attributeNumber(event, "duration", 1.0)));
        clip.sourceOffsetSeconds = std::max(0.0, attributeNumber(event, "offset", 0.0));
        clip.muted = attributeBool(event, "mute", false);
        clip.gainDb = static_cast<float>(juce::jlimit(-60.0, 24.0, attributeNumber(event, "gain", 0.0)));
        juce::String path;
        for (const auto* idName : { "clipID", "mediaID", "clip", "media", "audioClip", "file" })
            if (const auto id = attributeText(event, idName); id.isNotEmpty()) {
                const auto found = mediaById_.find(id);
                if (found != mediaById_.end()) {
                    path = found->second;
                    break;
                }
                if (looksLikeAudio(id))
                    path = id;
            }
        if (path.isEmpty() && clip.name.isNotEmpty()) {
            for (const auto& [name, candidate] : mediaByName_)
                if (name.startsWith(clip.name.toLowerCase())) {
                    path = candidate;
                    break;
                }
        }
        clip.originalPath = path;
        clip.file = path.isNotEmpty() ? resolveProjectAudioFile(folder_, path) : juce::File {};
        if (clip.file == juce::File {}) {
            out_.missingFiles.addIfNotAlreadyThere(path.isNotEmpty() ? path : clip.name);
            return;
        }
        track.audioClips.push_back(std::move(clip));
    }

    void readMusicPart(const juce::XmlElement* part, ImportedTrack& track)
    {
        ImportedMidiClip clip;
        clip.name = attributeText(part, "name");
        const auto startSeconds = std::max(0.0, attributeNumber(part, "start", 0.0));
        const auto lengthSeconds = std::max(0.01, attributeNumber(part, "length", attributeNumber(part, "duration", 4.0)));
        clip.startBeats = out_.secondsToBeats(startSeconds);
        clip.lengthBeats = std::max(0.25, out_.secondsToBeats(lengthSeconds));
        clip.muted = attributeBool(part, "mute", false);
        std::function<void(const juce::XmlElement*)> collect = [&](const juce::XmlElement* element) {
            for (const auto* child : element->getChildIterator()) {
                if (child->hasTagName("Note")) {
                    ImportedNote note;
                    note.note = juce::jlimit(0, 127, static_cast<int>(attributeNumber(child, "pitch", attributeNumber(child, "key", 60))));
                    note.startBeats = std::max(0.0, attributeNumber(child, "start", 0.0));
                    note.lengthBeats = std::max(0.01, attributeNumber(child, "length", attributeNumber(child, "duration", 1.0)));
                    auto velocity = attributeNumber(child, "velocity", 0.8);
                    if (velocity > 1.0)
                        velocity /= 127.0;
                    note.velocity = static_cast<float>(juce::jlimit(0.01, 1.0, velocity));
                    if (note.startBeats < clip.lengthBeats)
                        clip.notes.push_back(note);
                } else {
                    collect(child);
                }
            }
        };
        collect(part);
        track.midiClips.push_back(std::move(clip));
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
    std::map<juce::String, juce::String> mediaById_;
    std::map<juce::String, juce::String> mediaByName_;
    bool tempoFound_ {};
};

// ---------------------------------------------------------------------------
// Bitwig Studio: "BtWg", a hex header, a meta section, then a tree of
// objects — [int32 class][fields...][int32 0], each field [int32 id][byte
// type][value]. Objects are numbered as they are met, and a later mention
// is a reference to that number.

namespace bitwig {

struct Node;
using NodePtr = std::shared_ptr<Node>;

struct Value {
    enum class Kind { null, integer, number, boolean, text, object, reference, list, map,
                      route, colour, bytes, floats, strings };
    Kind kind { Kind::null };
    juce::int64 integer {};
    double number {};
    bool boolean {};
    juce::String text;
    NodePtr object;
    int reference { -1 };
    std::vector<Value> list;
    std::vector<std::pair<juce::String, Value>> map;
    juce::uint32 colour {};
    juce::StringArray strings;
};

struct Field {
    int id {};
    Value value;
};

struct Node {
    int classId {};
    int number {};
    std::vector<Field> fields;

    [[nodiscard]] const Value* field(const int id) const
    {
        for (const auto& entry : fields)
            if (entry.id == id)
                return &entry.value;
        return nullptr;
    }
};

class Parser {
public:
    Parser(const juce::MemoryBlock& data, juce::StringArray& warnings)
        : data_(static_cast<const std::uint8_t*>(data.getData())), size_(data.getSize()),
          warnings_(warnings)
    {
    }

    // Back to the start of another block of bytes, the warnings kept.
    void reset(const juce::MemoryBlock& data)
    {
        data_ = static_cast<const std::uint8_t*>(data.getData());
        size_ = data.getSize();
        position_ = 0;
        ok_ = true;
        unknownReported_ = 0;
        numbered_.clear();
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] std::size_t position() const { return position_; }
    void seek(const std::size_t position) { position_ = std::min(position, size_); }
    [[nodiscard]] bool atEnd() const { return position_ >= size_; }

    std::uint8_t u8()
    {
        if (position_ + 1 > size_) {
            ok_ = false;
            return 0;
        }
        return data_[position_++];
    }

    std::int32_t i32()
    {
        if (position_ + 4 > size_) {
            ok_ = false;
            position_ = size_;
            return 0;
        }
        const auto value = (static_cast<std::uint32_t>(data_[position_]) << 24)
                           | (static_cast<std::uint32_t>(data_[position_ + 1]) << 16)
                           | (static_cast<std::uint32_t>(data_[position_ + 2]) << 8)
                           | static_cast<std::uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return static_cast<std::int32_t>(value);
    }

    juce::int64 i64()
    {
        const auto high = static_cast<juce::int64>(static_cast<std::uint32_t>(i32()));
        const auto low = static_cast<juce::int64>(static_cast<std::uint32_t>(i32()));
        return (high << 32) | low;
    }

    double f64()
    {
        const auto bits = static_cast<std::uint64_t>(i64());
        double value {};
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    float f32()
    {
        const auto bits = static_cast<std::uint32_t>(i32());
        float value {};
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    juce::String string()
    {
        const auto raw = static_cast<std::uint32_t>(i32());
        if (!ok_ || raw == 0xffffffffu)
            return {};
        if ((raw & 0x80000000u) != 0) {
            const auto count = static_cast<std::size_t>(raw & 0x7fffffffu);
            if (position_ + count * 2 > size_) {
                ok_ = false;
                return {};
            }
            std::vector<juce::CharPointer_UTF16::CharType> buffer(count + 1, 0);
            for (std::size_t i = 0; i < count; ++i)
                buffer[i] = static_cast<juce::CharPointer_UTF16::CharType>(
                    (static_cast<std::uint16_t>(data_[position_ + i * 2]) << 8)
                    | data_[position_ + i * 2 + 1]);
            position_ += count * 2;
            return juce::String(juce::CharPointer_UTF16(buffer.data()));
        }
        const auto count = static_cast<std::size_t>(raw);
        if (position_ + count > size_) {
            ok_ = false;
            return {};
        }
        const auto text = juce::String::fromUTF8(reinterpret_cast<const char*>(data_ + position_),
                                                 static_cast<int>(count));
        position_ += count;
        return text;
    }

    // An object in the stream: a class and its fields, or a reference to
    // one already read (class 1).
    Value object(const int depth)
    {
        Value value;
        const auto classId = i32();
        if (!ok_)
            return value;
        if (classId == 1) {
            value.kind = Value::Kind::reference;
            value.reference = i32();
            return value;
        }
        if (depth > 400) {
            ok_ = false;
            return value;
        }
        auto node = std::make_shared<Node>();
        node->classId = classId;
        node->number = static_cast<int>(numbered_.size());
        numbered_.push_back(node.get());
        value.kind = Value::Kind::object;
        value.object = node;
        while (ok_) {
            const auto fieldId = i32();
            if (fieldId == 0 || !ok_)
                break;
            const auto type = u8();
            Field field;
            field.id = fieldId;
            field.value = read(type, depth + 1);
            node->fields.push_back(std::move(field));
        }
        return value;
    }

    Value read(const std::uint8_t type, const int depth)
    {
        Value value;
        switch (type) {
            case 0x01: {
                value.kind = Value::Kind::integer;
                const auto raw = u8();
                value.integer = raw & 0x80 ? static_cast<int>(raw) - 256 : raw;
                break;
            }
            case 0x02: {
                value.kind = Value::Kind::integer;
                const auto high = u8();
                const auto low = u8();
                value.integer = static_cast<std::int16_t>((high << 8) | low);
                break;
            }
            case 0x03: value.kind = Value::Kind::integer; value.integer = i32(); break;
            case 0x04: value.kind = Value::Kind::integer; value.integer = i64(); break;
            case 0x05: value.kind = Value::Kind::boolean; value.boolean = u8() != 0; break;
            case 0x06: value.kind = Value::Kind::number; value.number = f32(); break;
            case 0x07: value.kind = Value::Kind::number; value.number = f64(); break;
            case 0x08: value.kind = Value::Kind::text; value.text = string(); break;
            case 0x09: value = object(depth); break;
            case 0x0a: break; // null
            case 0x0b: value.kind = Value::Kind::reference; value.reference = i32(); break;
            case 0x0d: {
                value.kind = Value::Kind::bytes;
                const auto count = static_cast<std::uint32_t>(i32());
                if (position_ + count > size_) {
                    ok_ = false;
                    break;
                }
                position_ += count;
                break;
            }
            case 0x12: {
                value.kind = Value::Kind::list;
                while (ok_ && position_ + 4 <= size_) {
                    const auto next = peek32();
                    if (next == 3) {
                        position_ += 4;
                        break;
                    }
                    value.list.push_back(object(depth + 1));
                }
                break;
            }
            case 0x14: {
                value.kind = Value::Kind::map;
                while (ok_) {
                    const auto marker = u8();
                    if (marker == 0 || !ok_)
                        break;
                    if (marker != 1) {
                        ok_ = false;
                        break;
                    }
                    auto key = string();
                    value.map.emplace_back(std::move(key), object(depth + 1));
                }
                break;
            }
            case 0x15: {
                value.kind = Value::Kind::bytes;
                if (position_ + 16 > size_)
                    ok_ = false;
                else
                    position_ += 16;
                break;
            }
            case 0x16: {
                value.kind = Value::Kind::colour;
                const auto r = f32();
                const auto g = f32();
                const auto b = f32();
                const auto a = f32();
                value.colour = colourFromFloats(r, g, b, a);
                break;
            }
            case 0x17: {
                value.kind = Value::Kind::floats;
                const auto count = static_cast<std::uint32_t>(i32());
                if (position_ + static_cast<std::size_t>(count) * 4 > size_) {
                    ok_ = false;
                    break;
                }
                position_ += static_cast<std::size_t>(count) * 4;
                break;
            }
            case 0x19: {
                value.kind = Value::Kind::strings;
                const auto count = i32();
                for (auto i = 0; i < count && ok_; ++i)
                    value.strings.add(string());
                break;
            }
            case 0x1a: {
                value.kind = Value::Kind::route;
                auto target = object(depth + 1);
                value.object = target.object;
                value.reference = target.reference;
                value.text = string();
                break;
            }
            default:
                if (unknownReported_ < 4) {
                    warnings_.add("Bitwig file: unknown value type 0x" + juce::String::toHexString(type)
                                  + " at offset " + juce::String(static_cast<juce::int64>(position_ - 1))
                                  + "; the rest of the file was skipped.");
                    ++unknownReported_;
                }
                ok_ = false;
                break;
        }
        return value;
    }

    [[nodiscard]] const Node* resolve(const Value& value) const
    {
        if (value.kind == Value::Kind::object)
            return value.object.get();
        if (value.kind == Value::Kind::reference || value.kind == Value::Kind::route) {
            const auto index = value.reference - 1; // references count from one
            if (index >= 0 && static_cast<std::size_t>(index) < numbered_.size())
                return numbered_[static_cast<std::size_t>(index)];
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<Node*>& numbered() const { return numbered_; }

private:
    std::int32_t peek32() const
    {
        return static_cast<std::int32_t>((static_cast<std::uint32_t>(data_[position_]) << 24)
                                         | (static_cast<std::uint32_t>(data_[position_ + 1]) << 16)
                                         | (static_cast<std::uint32_t>(data_[position_ + 2]) << 8)
                                         | static_cast<std::uint32_t>(data_[position_ + 3]));
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ {};
    bool ok_ { true };
    int unknownReported_ {};
    std::vector<Node*> numbered_;
    juce::StringArray& warnings_;
};

// The field ids Bitwig 4 and 5 use for what the importer reads.
namespace id {
constexpr int name = 68;
constexpr int transport = 3033;
constexpr int tempo = 529;
constexpr int timeSignature = 530;
constexpr int value = 712;
constexpr int boolValue = 210;
constexpr int packedSignature = 7358;
constexpr int signatureNumerator = 622;
constexpr int signatureDenominator = 623;
constexpr int trackGroup = 1245;
constexpr int trackList = 1246;
constexpr int effectTrackList = 1247;
constexpr int masterTrack = 1248;
constexpr int title = 347;
constexpr int colour = 1356;
constexpr int trackType = 2343;
constexpr int timeline = 349;
constexpr int clipTimeline = 568;
constexpr int events = 543;
constexpr int matrixSlots = 350;
constexpr int slots = 561;
constexpr int slotClip = 576;
constexpr int deviceChain = 356;
constexpr int devices = 324;
constexpr int devicePreset = 407;
constexpr int deviceName = 154;
constexpr int deviceCreator = 155;
constexpr int deviceType = 157;
constexpr int userDeviceName = 5466;
constexpr int mixer = 357;
constexpr int volume = 420;
constexpr int pan = 421;
constexpr int mute = 422;
constexpr int solo = 424;
constexpr int clipName = 646;
constexpr int clipColour = 640;
constexpr int time = 687;
constexpr int duration = 38;
constexpr int isMuted = 4344;
constexpr int clipFadeIn = 6471;
constexpr int clipFadeOut = 6472;
constexpr int content = 648;
constexpr int cueMarkers = 2447;
constexpr int loopEnabled = 2449;
constexpr int loopRegion = 2450;
constexpr int startMarker = 2444;
constexpr int noteTimeline = 1180;
constexpr int laneList = 6347;
constexpr int laneKey = 238;
constexpr int noteOnVelocity = 239;
constexpr int contentStartTime = 3076;
constexpr int sampleStopTime = 3077;
constexpr int eventFadeIn = 4697;
constexpr int eventFadeOut = 4698;
constexpr int sampleHandle = 525;
constexpr int sampleReference = 1204;
constexpr int sampleName = 4767;
constexpr int fileReference = 4766;
constexpr int fileName = 1298;
constexpr int pathInProject = 3386;
constexpr int packagedPath = 3284;
constexpr int warpSampleLength = 335;
constexpr int initialLocation = 3387;
constexpr int absolutePath = 59;
constexpr int sampleDuration = 149;
constexpr int sampleLoopEnabled = 2237;
constexpr int sampleLoopStart = 2238;
constexpr int sampleLoopEnd = 2239;
constexpr int warpMode = 1200;
constexpr int warpTimeline = 402;
constexpr int warpSampleTime = 166;
} // namespace id

class Reader {
public:
    Reader(const juce::File& file, ImportedProject& out)
        : file_(file), out_(out), folder_(file.getParentDirectory()), parser_(data_, out.warnings)
    {
    }

    juce::Result read()
    {
        if (!file_.loadFileAsData(data_) || data_.getSize() < 48)
            return juce::Result::fail("The file could not be read: " + file_.getFullPathName());
        parser_.reset(data_);
        const auto* bytes = static_cast<const char*>(data_.getData());
        if (std::memcmp(bytes, "BtWg", 4) != 0)
            return juce::Result::fail("Not a Bitwig project: " + file_.getFileName());
        const auto header = juce::String::fromUTF8(bytes, 42);
        const auto contentOffset = static_cast<std::size_t>(header.substring(16, 24).getHexValue32());
        if (contentOffset < 42 || contentOffset >= data_.getSize())
            return juce::Result::fail("The Bitwig file header points outside the file (packaged or"
                                      " encrypted content is not readable).");
        out_.format = "bitwig";
        out_.application = "Bitwig Studio";
        readMeta();
        parser_.seek(contentOffset);
        // The project is one tree; what follows it (where the header's next
        // offset points: Bitwig 4 keeps other data there) is not objects.
        std::vector<Value> roots;
        if (parser_.ok() && !parser_.atEnd())
            roots.push_back(parser_.object(0));
        if (roots.empty() || roots.front().kind != Value::Kind::object)
            return juce::Result::fail("The Bitwig project holds no readable object tree.");
        if (!parser_.ok())
            out_.warnings.add("The Bitwig file could not be read to its end; what came before is imported.");
        const auto* root = roots.front().object.get();
        readRoot(root);
        return juce::Result::ok();
    }

private:
    // [int32][string "meta"] then named fields until an id of 0.
    void readMeta()
    {
        parser_.seek(42);
        (void) parser_.i32();
        const auto label = parser_.string();
        if (label != "meta")
            return;
        while (parser_.ok()) {
            const auto marker = parser_.i32();
            if (marker == 0 || !parser_.ok())
                break;
            juce::String name;
            if (marker == 1)
                name = parser_.string();
            const auto type = parser_.u8();
            const auto value = parser_.read(type, 1);
            if (name == "application_version_name" && value.kind == Value::Kind::text)
                out_.application = "Bitwig Studio " + value.text;
        }
        // The meta section may hold types the object reader does not know;
        // that must not fail the content read.
        parser_.reset(data_);
    }

    [[nodiscard]] const Node* child(const Node* node, const int fieldId) const
    {
        if (node == nullptr)
            return nullptr;
        const auto* value = node->field(fieldId);
        return value != nullptr ? parser_.resolve(*value) : nullptr;
    }

    static double number(const Node* node, const int fieldId, const double fallback)
    {
        const auto* value = node != nullptr ? node->field(fieldId) : nullptr;
        if (value == nullptr)
            return fallback;
        if (value->kind == Value::Kind::number)
            return value->number;
        if (value->kind == Value::Kind::integer)
            return static_cast<double>(value->integer);
        return fallback;
    }

    static juce::int64 integer(const Node* node, const int fieldId, const juce::int64 fallback)
    {
        const auto* value = node != nullptr ? node->field(fieldId) : nullptr;
        return value != nullptr && value->kind == Value::Kind::integer ? value->integer : fallback;
    }

    static bool boolean(const Node* node, const int fieldId, const bool fallback)
    {
        const auto* value = node != nullptr ? node->field(fieldId) : nullptr;
        return value != nullptr && value->kind == Value::Kind::boolean ? value->boolean : fallback;
    }

    static juce::String text(const Node* node, const int fieldId)
    {
        const auto* value = node != nullptr ? node->field(fieldId) : nullptr;
        return value != nullptr && value->kind == Value::Kind::text ? value->text : juce::String();
    }

    static juce::uint32 colour(const Node* node, const int fieldId)
    {
        const auto* value = node != nullptr ? node->field(fieldId) : nullptr;
        return value != nullptr && value->kind == Value::Kind::colour ? value->colour : 0;
    }

    std::vector<const Node*> list(const Node* node, const int fieldId) const
    {
        std::vector<const Node*> items;
        const auto* value = node != nullptr ? node->field(fieldId) : nullptr;
        if (value == nullptr || value->kind != Value::Kind::list)
            return items;
        for (const auto& item : value->list)
            if (const auto* resolved = parser_.resolve(item))
                items.push_back(resolved);
        return items;
    }

    // Every object of a class, in the order the file holds them.
    void collect(const Node* node, const int classId, std::vector<const Node*>& found,
                 std::set<const Node*>& seen) const
    {
        if (node == nullptr || !seen.insert(node).second)
            return;
        if (node->classId == classId)
            found.push_back(node);
        for (const auto& field : node->fields)
            collectValue(field.value, classId, found, seen);
    }

    void collectValue(const Value& value, const int classId, std::vector<const Node*>& found,
                      std::set<const Node*>& seen) const
    {
        if (value.kind == Value::Kind::object)
            collect(value.object.get(), classId, found, seen);
        else if (value.kind == Value::Kind::list)
            for (const auto& item : value.list)
                collectValue(item, classId, found, seen);
        else if (value.kind == Value::Kind::map)
            for (const auto& entry : value.map)
                collectValue(entry.second, classId, found, seen);
        else if (value.kind == Value::Kind::route && value.object != nullptr)
            collect(value.object.get(), classId, found, seen);
    }

    // Objects of a class reachable without crossing a route: a track's own
    // nested group, not the master its output points at.
    void collectInside(const Node* node, const int classId, std::vector<const Node*>& found,
                       std::set<const Node*>& seen) const
    {
        if (node == nullptr || !seen.insert(node).second)
            return;
        if (node->classId == classId) {
            found.push_back(node);
            return;
        }
        for (const auto& field : node->fields)
            collectInsideValue(field.value, classId, found, seen);
    }

    void collectInsideValue(const Value& value, const int classId, std::vector<const Node*>& found,
                            std::set<const Node*>& seen) const
    {
        if (value.kind == Value::Kind::object)
            collectInside(value.object.get(), classId, found, seen);
        else if (value.kind == Value::Kind::list)
            for (const auto& item : value.list)
                collectInsideValue(item, classId, found, seen);
        else if (value.kind == Value::Kind::map)
            for (const auto& entry : value.map)
                collectInsideValue(entry.second, classId, found, seen);
    }

    void appendGroup(const Node* group, std::vector<const Node*>& tracks, std::set<const Node*>& listed) const
    {
        if (group == nullptr || !listed.insert(group).second)
            return;
        // Its tracks, then its effect tracks (the returns).
        auto items = list(group, id::trackList);
        for (const auto* effect : list(group, id::effectTrackList))
            items.push_back(effect);
        for (const auto* item : items) {
            if (item->classId == 477) {
                appendGroup(item, tracks, listed);
                continue;
            }
            if (!listed.insert(item).second)
                continue;
            tracks.push_back(item);
            std::set<const Node*> seen;
            std::vector<const Node*> nested;
            for (const auto& field : item->fields)
                collectInsideValue(field.value, 477, nested, seen);
            for (const auto* child : nested)
                appendGroup(child, tracks, listed);
        }
    }

    // Volume atoms hold the cube root of the linear gain: 1.0 is 0 dB,
    // 0.68 the -10 dB Bitwig gives a new track.
    static float volumeToDb(const double value)
    {
        if (value <= 1.0e-3)
            return -60.0f;
        return static_cast<float>(juce::jlimit(-60.0, 12.0, 60.0 * std::log10(value)));
    }

    void readRoot(const Node* root)
    {
        out_.name = text(root, id::name);
        if (const auto* transport = child(root, id::transport)) {
            if (const auto* tempo = child(transport, id::tempo))
                out_.tempo = juce::jlimit(20.0, 300.0, number(tempo, id::value, 120.0));
            if (const auto* signature = child(transport, id::timeSignature)) {
                const auto numerator = integer(signature, id::signatureNumerator, 0);
                const auto denominator = integer(signature, id::signatureDenominator, 0);
                if (numerator > 0 && denominator > 0) {
                    out_.numerator = static_cast<int>(juce::jlimit<juce::int64>(1, 32, numerator));
                    out_.denominator = static_cast<int>(juce::jlimit<juce::int64>(1, 32, denominator));
                } else if (const auto packed = integer(signature, id::packedSignature, 0); packed > 0) {
                    const auto low = static_cast<int>(packed & 0xf);
                    const auto shift = static_cast<int>((packed >> 4) & 0xf);
                    if (low > 0 && shift < 6) {
                        out_.numerator = low;
                        out_.denominator = 1 << shift;
                    }
                }
            }
        }

        // The tracks: the root group's list (and its effect tracks), a group
        // track's own list where it sits. The master is the one the root
        // group names (master_track); a track of type 2 elsewhere is a
        // hybrid track (notes and audio together), read as the others are.
        std::vector<const Node*> tracks;
        std::set<const Node*> listed;
        std::set<const Node*> seen;
        std::vector<const Node*> rootGroups;
        collectInside(root, 477, rootGroups, seen);
        const Node* master = nullptr;
        for (const auto* group : rootGroups)
            if (master == nullptr)
                master = child(group, id::masterTrack);
        if (master != nullptr)
            listed.insert(master);
        for (const auto* group : rootGroups)
            appendGroup(group, tracks, listed);
        if (tracks.empty()) {
            seen.clear();
            collect(root, 144, tracks, seen);
        }
        if (master != nullptr)
            if (const auto* mixer = child(master, id::mixer))
                if (const auto* volume = child(mixer, id::volume))
                    out_.masterGainDb = volumeToDb(number(volume, id::value, 1.0));
        auto index = 0;
        for (const auto* track : tracks)
            if (track != master)
                readTrack(track, ++index);
    }

    void readTrack(const Node* track, const int index)
    {
        ImportedTrack imported;
        imported.name = text(track, id::title);
        imported.colour = colour(track, id::colour);
        const auto type = integer(track, id::trackType, -1);
        juce::String instrumentName;
        if (const auto* mixer = child(track, id::mixer)) {
            if (const auto* volume = child(mixer, id::volume))
                imported.gainDb = volumeToDb(number(volume, id::value, 1.0));
            if (const auto* pan = child(mixer, id::pan))
                imported.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, number(pan, id::value, 0.0)));
            if (const auto* mute = child(mixer, id::mute))
                imported.muted = boolean(mute, id::boolValue, false);
            imported.solo = boolean(mixer, id::solo, false);
        }
        if (const auto* chain = child(track, id::deviceChain))
            for (const auto* device : list(chain, id::devices)) {
                const auto* preset = child(device, id::devicePreset);
                auto name = text(preset, id::deviceName);
                if (name.isEmpty())
                    name = text(device, id::userDeviceName);
                if (name.isEmpty())
                    name = "device";
                const auto deviceType = integer(preset, id::deviceType, 0);
                if (name == "Track Controls")
                    continue;
                const auto creator = text(preset, id::deviceCreator);
                imported.devices.add(creator.isNotEmpty() && creator != "Bitwig" ? name + " (" + creator + ")" : name);
                ImportedDevice entry;
                entry.name = name;
                entry.plugin = creator.isNotEmpty() && creator != "Bitwig";
                entry.vendor = entry.plugin ? creator : juce::String();
                entry.type = entry.plugin ? juce::String() : name;
                entry.instrument = deviceType != 0 && deviceType != 1;
                entry.noteEffect = deviceType == 1;
                // Its settings: each parameter an object with its id as text
                // (697) and its value as the first number, whole number or
                // switch beside it ("F1FREQ" 47.6, "OSC1OCT" 2).
                {
                    std::set<const Node*> seenSettings;
                    std::function<void(const Node*, int)> walk = [&](const Node* node, const int depth) {
                        if (node == nullptr || depth > 12 || !seenSettings.insert(node).second || entry.settings.size() > 400)
                            return;
                        const auto* key = node->field(697);
                        if (key != nullptr && key->kind == Value::Kind::text && key->text.isNotEmpty())
                            for (const auto& field : node->fields) {
                                if (field.id == 697)
                                    continue;
                                if (field.value.kind == Value::Kind::number)
                                    entry.settings.emplace_back(key->text, field.value.number);
                                else if (field.value.kind == Value::Kind::integer)
                                    entry.settings.emplace_back(key->text, static_cast<double>(field.value.integer));
                                else if (field.value.kind == Value::Kind::boolean)
                                    entry.settings.emplace_back(key->text, field.value.boolean ? 1.0 : 0.0);
                                else
                                    continue;
                                break;
                            }
                        for (const auto& field : node->fields) {
                            if (field.value.kind == Value::Kind::object)
                                walk(parser_.resolve(field.value), depth + 1);
                            else if (field.value.kind == Value::Kind::list)
                                for (const auto& item : field.value.list)
                                    if (item.kind == Value::Kind::object)
                                        walk(parser_.resolve(item), depth + 1);
                        }
                    };
                    walk(device, 0);
                }
                imported.chain.push_back(std::move(entry));
                if (deviceType != 0 && deviceType != 1) {
                    imported.hadInstrument = true;
                    if (instrumentName.isEmpty())
                        instrumentName = name;
                }
            }
        if (type == 0 || type == 2)
            imported.hadInstrument = true;

        if (const auto* timeline = child(track, id::timeline))
            if (const auto* clipTimeline = child(timeline, id::clipTimeline))
                for (const auto* clip : list(clipTimeline, id::events))
                    readClip(clip, imported, -1, number(clip, id::time, 0.0), type);
        if (const auto* matrix = child(track, id::matrixSlots)) {
            auto scene = 0;
            for (const auto* slot : list(matrix, id::slots)) {
                if (const auto* clip = child(slot, id::slotClip))
                    readClip(clip, imported, scene, scene * kLauncherSceneBeats, type);
                ++scene;
            }
        }

        const auto hasNotes = std::any_of(imported.midiClips.begin(), imported.midiClips.end(),
                                          [](const auto& clip) { return !clip.notes.empty(); });
        if (hasNotes || (imported.audioClips.empty() && (type == 0 || !imported.midiClips.empty())))
            imported.midi = true;
        else if (!imported.audioClips.empty())
            imported.midi = false;
        else if (type != 1) {
            // Neither clips nor notes on a track that is not audio: a group
            // or an effect track. Its children, when any, come on their own.
            if (imported.devices.isEmpty())
                return;
            imported.midi = type == 0;
        }
        // Unnamed, as Bitwig shows it: its instrument's name, else its kind
        // and its place ("Audio 14").
        if (imported.name.isEmpty()) {
            imported.name = instrumentName;
            if (imported.name.isEmpty())
                imported.name = (type == 1 ? "Audio " : type == 2 ? "Hybrid " : imported.midi ? "Inst " : "Audio ") + juce::String(index);
        }
        out_.tracks.push_back(std::move(imported));
    }

    void readClip(const Node* clip, ImportedTrack& track, const int scene, const double startBeats,
                  const juce::int64 trackType)
    {
        const auto name = text(clip, id::clipName);
        const auto clipColour = colour(clip, id::clipColour);
        const auto muted = boolean(clip, id::isMuted, false);
        const auto durationBeats = number(clip, id::duration, 0.0);
        if (durationBeats <= 1.0e-6)
            return;
        const auto* content = child(clip, id::content);
        const auto* cue = child(content, id::cueMarkers);
        const auto looping = boolean(cue, id::loopEnabled, false);
        const auto* loopRegion = child(cue, id::loopRegion);
        const auto loopStart = number(loopRegion, id::time, 0.0);
        const auto loopEnd = loopStart + number(loopRegion, id::duration, 0.0);
        const auto playStart = number(child(cue, id::startMarker), id::time, 0.0);
        const auto fadeIn = out_.beatsToSeconds(number(clip, id::clipFadeIn, 0.0));
        const auto fadeOut = out_.beatsToSeconds(number(clip, id::clipFadeOut, 0.0));

        std::vector<ImportedNote> notes;
        struct AudioEvent {
            const Node* event {};
            double time {};
            double duration {};
        };
        std::vector<AudioEvent> audioEvents;
        // Bitwig 5 keeps one lane per key under the note timeline; Bitwig 4
        // made the timeline itself the lane.
        std::vector<const Node*> lanes;
        if (const auto* noteTimeline = child(content, id::noteTimeline)) {
            lanes = list(noteTimeline, id::laneList);
            if (lanes.empty() && noteTimeline->field(id::events) != nullptr)
                lanes.push_back(noteTimeline);
        }
        for (const auto* lane : lanes) {
                const auto* laneKey = lane->field(id::laneKey);
                for (const auto* event : list(lane, id::events)) {
                    const auto* keyValue = laneKey != nullptr ? laneKey : event->field(id::laneKey);
                    const auto hasKey = keyValue != nullptr && keyValue->kind == Value::Kind::integer;
                    const auto* eventContent = child(event, id::content);
                    const auto isAudio = child(event, id::sampleHandle) != nullptr
                                         || (eventContent != nullptr
                                             && (child(eventContent, id::sampleHandle) != nullptr
                                                 || child(eventContent, id::warpTimeline) != nullptr));
                    if (isAudio) {
                        audioEvents.push_back({ event, number(event, id::time, 0.0),
                                                number(event, id::duration, 0.0) });
                    } else if (hasKey) {
                        if (boolean(event, id::isMuted, false))
                            continue;
                        ImportedNote note;
                        note.note = juce::jlimit(0, 127, static_cast<int>(keyValue->integer));
                        note.startBeats = number(event, id::time, 0.0);
                        note.lengthBeats = std::max(0.01, number(event, id::duration, 0.25));
                        auto velocity = number(event, id::noteOnVelocity, -1.0);
                        if (velocity < 0.0) {
                            // Without a named velocity, the first fraction
                            // after the duration is the closest thing.
                            auto pastDuration = false;
                            for (const auto& field : event->fields) {
                                if (field.id == id::duration) {
                                    pastDuration = true;
                                    continue;
                                }
                                if (pastDuration && field.value.kind == Value::Kind::number
                                    && field.value.number > 0.0 && field.value.number <= 1.0) {
                                    velocity = field.value.number;
                                    break;
                                }
                            }
                        }
                        note.velocity = static_cast<float>(juce::jlimit(0.01, 1.0, velocity > 0.0 ? velocity : 0.8));
                        notes.push_back(note);
                    }
                }
            }

        if (!notes.empty() || (audioEvents.empty() && trackType == 0)) {
            ImportedMidiClip midiClip;
            midiClip.name = name;
            midiClip.startBeats = startBeats;
            midiClip.lengthBeats = std::max(0.25, durationBeats);
            midiClip.looping = looping && scene >= 0;
            midiClip.muted = muted;
            midiClip.colour = clipColour;
            midiClip.launcherScene = scene;
            unrollContent(playStart, midiClip.lengthBeats, looping, loopStart, loopEnd,
                          [&](const double from, const double to, const double offset) {
                              for (const auto& note : notes)
                                  if (note.startBeats >= from - 1.0e-6 && note.startBeats < to - 1.0e-6) {
                                      auto placed = note;
                                      placed.startBeats = note.startBeats - from + offset;
                                      placed.lengthBeats = std::min(note.lengthBeats,
                                                                    midiClip.lengthBeats - placed.startBeats);
                                      if (placed.lengthBeats > 0.01)
                                          midiClip.notes.push_back(placed);
                                  }
                          });
            if (!midiClip.notes.empty() || audioEvents.empty())
                track.midiClips.push_back(std::move(midiClip));
        }

        for (const auto& audio : audioEvents)
            readAudioEvent(audio.event, audio.time, audio.duration, track, name, clipColour, muted,
                           scene, startBeats, durationBeats, playStart, looping, loopStart, loopEnd,
                           fadeIn, fadeOut);
    }

    void readAudioEvent(const Node* event, const double eventTime, const double eventDuration,
                        ImportedTrack& track, const juce::String& clipName, const juce::uint32 clipColour,
                        const bool muted, const int scene, const double startBeats,
                        const double durationBeats, const double playStart, const bool looping,
                        const double loopStart, const double loopEnd, const double fadeIn,
                        const double fadeOut)
    {
        const auto* sample = child(event, id::content);
        const auto* handle = child(event, id::sampleHandle);
        if (handle == nullptr)
            handle = child(sample, id::sampleHandle);
        const auto* sampleReference = child(handle, id::sampleReference);
        const auto* reference = child(sampleReference, id::fileReference);
        auto fileName = text(reference, id::fileName);
        if (fileName.isEmpty())
            fileName = text(sampleReference, id::sampleName);
        const auto pathInProject = text(reference, id::pathInProject);
        const auto packaged = text(reference, id::packagedPath);
        const auto absolute = text(child(reference, id::initialLocation), id::absolutePath);
        juce::File file;
        if (pathInProject.isNotEmpty())
            file = resolveProjectAudioFile(folder_, pathInProject);
        if (file == juce::File {} && absolute.isNotEmpty())
            file = resolveProjectAudioFile(folder_, absolute);
        if (file == juce::File {} && fileName.isNotEmpty())
            file = resolveProjectAudioFile(folder_, fileName);
        if (file == juce::File {} && (packaged.isNotEmpty() || fileName.isNotEmpty()))
            file = packagedFile(fileNameOf(packaged.isNotEmpty() ? packaged : fileName));
        const auto original = pathInProject.isNotEmpty() ? pathInProject
                              : absolute.isNotEmpty() ? absolute
                              : packaged.isNotEmpty() ? packaged : fileName;
        if (file == juce::File {}) {
            out_.missingFiles.addIfNotAlreadyThere(original.isNotEmpty() ? original : clipName);
            return;
        }

        const auto* warps = child(sample, id::warpTimeline);
        auto sampleSeconds = number(sample, id::sampleDuration, 0.0);
        if (sampleSeconds <= 0.0)
            sampleSeconds = number(warps, id::warpSampleLength, 0.0);
        if (sampleSeconds <= 0.0)
            for (const auto* warp : list(warps, id::events))
                sampleSeconds = std::max(sampleSeconds, number(warp, id::warpSampleTime, 0.0));
        const auto contentStart = number(event, id::contentStartTime, 0.0);
        // Without a known length, the file is taken as long as the event.
        const auto sampleStop = number(event, id::sampleStopTime,
                                       sampleSeconds > 0.0 ? sampleSeconds : 1.0e9);
        const auto sampleLoops = boolean(sample, id::sampleLoopEnabled, false);
        const auto sampleLoopStart = number(sample, id::sampleLoopStart, 0.0);
        const auto sampleLoopEnd = number(sample, id::sampleLoopEnd, sampleSeconds);
        const auto raw = integer(event->field(id::warpMode) != nullptr ? event : sample, id::warpMode, 0) == 0;
        TimeMap map;
        map.fallbackSecondsPerBeat = 60.0 / out_.tempo;
        for (const auto* warp : list(warps, id::events))
            map.points.emplace_back(number(warp, id::time, 0.0), number(warp, id::warpSampleTime, 0.0));

        ImportedAudioClip base;
        base.name = clipName.isNotEmpty() ? clipName : fileName;
        base.file = file;
        base.originalPath = original;
        base.colour = clipColour;
        base.muted = muted;
        base.launcherScene = scene;
        base.looping = looping || sampleLoops;
        base.fadeInSeconds = std::max(fadeIn, out_.beatsToSeconds(number(event, id::eventFadeIn, 0.0)));
        base.fadeOutSeconds = std::max(fadeOut, out_.beatsToSeconds(number(event, id::eventFadeOut, 0.0)));

        const auto eventEnd = eventTime + eventDuration;
        unrollContent(playStart, durationBeats, looping && scene < 0, loopStart, loopEnd,
                      [&](const double from, const double to, const double offset) {
                          const auto windowFrom = std::max(from, eventTime);
                          const auto windowTo = std::min(to, eventEnd);
                          if (windowTo <= windowFrom + 1.0e-6)
                              return;
                          const auto localFrom = windowFrom - eventTime;
                          const auto localTo = windowTo - eventTime;
                          auto piece = base;
                          piece.startSeconds = out_.beatsToSeconds(startBeats + offset + (windowFrom - from));
                          piece.durationSeconds = out_.beatsToSeconds(localTo - localFrom);
                          if (raw || !map.usable()) {
                              // Native speed: the timeline seconds are the
                              // source seconds.
                              piece.sourceOffsetSeconds = contentStart + out_.beatsToSeconds(localFrom);
                              const auto available = sampleStop - piece.sourceOffsetSeconds;
                              if (available <= 0.0)
                                  return;
                              if (piece.durationSeconds > available + 1.0e-3) {
                                  if (sampleLoops && sampleLoopEnd > sampleLoopStart + 1.0e-3 && scene < 0) {
                                      // The sample repeats to fill the event.
                                      auto placed = 0.0;
                                      auto sourceFrom = piece.sourceOffsetSeconds;
                                      auto repeats = 0;
                                      while (placed < piece.durationSeconds - 1.0e-3 && repeats < kMaximumLoopRepeats) {
                                          auto part = piece;
                                          part.startSeconds = piece.startSeconds + placed;
                                          part.sourceOffsetSeconds = sourceFrom;
                                          part.durationSeconds = std::min(sampleLoopEnd - sourceFrom,
                                                                          piece.durationSeconds - placed);
                                          if (part.durationSeconds <= 1.0e-3)
                                              break;
                                          if (repeats > 0)
                                              part.fadeInSeconds = 0.0;
                                          if (placed + part.durationSeconds < piece.durationSeconds - 1.0e-3)
                                              part.fadeOutSeconds = 0.0;
                                          track.audioClips.push_back(part);
                                          placed += part.durationSeconds;
                                          sourceFrom = sampleLoopStart;
                                          ++repeats;
                                      }
                                      return;
                                  }
                                  piece.durationSeconds = available;
                              }
                          } else {
                              piece.sourceOffsetSeconds = std::max(0.0, map.secondsAt(localFrom));
                              const auto sourceLength = map.secondsAt(localTo) - piece.sourceOffsetSeconds;
                              if (sourceLength > 0.0
                                  && std::abs(sourceLength - piece.durationSeconds) > 0.01 * piece.durationSeconds)
                                  piece.sourceDurationSeconds = sourceLength;
                              if (sampleSeconds > 0.0 && piece.sourceOffsetSeconds >= sampleSeconds)
                                  return;
                          }
                          piece.fadeInSeconds = std::min(piece.fadeInSeconds, piece.durationSeconds * 0.5);
                          piece.fadeOutSeconds = std::min(piece.fadeOutSeconds, piece.durationSeconds * 0.5);
                          track.audioClips.push_back(piece);
                      });
    }

    // A file from Bitwig's installed packages or the user library, by
    // name; the folders are listed the first time one is needed.
    juce::File packagedFile(const juce::String& name)
    {
        if (name.isEmpty())
            return {};
        if (!libraryIndexed_) {
            libraryIndexed_ = true;
            juce::Array<juce::File> roots;
            roots.add(juce::File::getSpecialLocation(juce::File::windowsLocalAppData)
                          .getChildFile("Bitwig Studio").getChildFile("installed-packages"));
            roots.add(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                          .getChildFile("Bitwig Studio").getChildFile("Library"));
            roots.add(juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                          .getChildFile("Bitwig Studio").getChildFile("Library"));
            for (const auto& root : roots) {
                if (!root.isDirectory())
                    continue;
                for (const auto& entry : juce::RangedDirectoryIterator(root, true, "*", juce::File::findFiles)) {
                    const auto& candidate = entry.getFile();
                    if (looksLikeAudio(candidate.getFileName()))
                        library_.emplace(candidate.getFileName().toLowerCase(), candidate);
                }
            }
        }
        const auto found = library_.find(name.toLowerCase());
        return found != library_.end() ? found->second : juce::File {};
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
    juce::MemoryBlock data_;
    Parser parser_;
    bool libraryIndexed_ {};
    std::map<juce::String, juce::File> library_;
};

} // namespace bitwig

} // namespace

// ---------------------------------------------------------------------------

juce::StringArray supportedProjectExtensions()
{
    // (A MIDI file is a project only when chosen as one: dropped, it is
    // notes for a track.)
    return { ".dawproject", ".als", ".song", ".bwproject", ".rpp", ".flp", ".ardour", ".mmp", ".mmpz", ".aaf", ".zpj" };
}

juce::String supportedProjectWildcard()
{
    juce::StringArray patterns;
    for (const auto& extension : supportedProjectExtensions())
        patterns.add("*" + extension);
    patterns.add("*.mid");
    patterns.add("*.midi");
    return patterns.joinIntoString(";");
}

juce::File resolveProjectAudioFile(const juce::File& projectFolder, const juce::String& rawPath)
{
    auto path = rawPath.trim();
    if (path.isEmpty())
        return {};
    if (path.startsWithIgnoreCase("file://")) {
        path = juce::URL::removeEscapeChars(path.fromFirstOccurrenceOf("file://", false, true));
        while (path.startsWith("/") && path.length() > 2 && path[2] == ':')
            path = path.substring(1);
    }
#if ! JUCE_WINDOWS
    path = path.replaceCharacter('\\', '/');
#endif
    if (juce::File::isAbsolutePath(path)) {
        const juce::File absolute(path);
        if (absolute.existsAsFile())
            return absolute;
    }
    if (projectFolder.isDirectory()) {
        const auto relative = projectFolder.getChildFile(path);
        if (relative.existsAsFile())
            return relative;
        const auto name = fileNameOf(path);
        if (name.isNotEmpty()) {
            juce::Array<juce::File> matches;
            projectFolder.findChildFiles(matches, juce::File::findFiles, true, name);
            if (!matches.isEmpty()) {
                matches.sort();
                return matches[0];
            }
        }
    }
    return {};
}

juce::Result importDawProject(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    DawProjectReader reader(file, out);
    return reader.read();
}

juce::Result importAbletonLiveSet(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    AbletonReader reader(file, out);
    return reader.read();
}

juce::Result importStudioOneSong(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    StudioOneReader reader(file, out);
    return reader.read();
}

juce::Result importBitwigProject(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    bitwig::Reader reader(file, out);
    return reader.read();
}

juce::Result importProjectFile(const juce::File& file, ImportedProject& out)
{
    if (!file.existsAsFile())
        return juce::Result::fail("No file at " + file.getFullPathName());
    const auto extension = file.getFileExtension().toLowerCase();
    auto result = juce::Result::fail("Unsupported project format \"" + extension + "\"; CyDAW reads "
                                     + supportedProjectExtensions().joinIntoString(", "));
    if (extension == ".dawproject")
        result = importDawProject(file, out);
    else if (extension == ".als")
        result = importAbletonLiveSet(file, out);
    else if (extension == ".song")
        result = importStudioOneSong(file, out);
    else if (extension == ".bwproject")
        result = importBitwigProject(file, out);
    else if (extension == ".rpp")
        result = importReaperProject(file, out);
    else if (extension == ".flp")
        result = importFlStudioProject(file, out);
    else if (extension == ".ardour")
        result = importArdourSession(file, out);
    else if (extension == ".mmp" || extension == ".mmpz")
        result = importLmmsProject(file, out);
    else if (extension == ".aaf")
        result = importAafComposition(file, out);
    else if (extension == ".zpj")
        result = importZrythmProject(file, out);
    else if (extension == ".mid" || extension == ".midi")
        result = importMidiAndStems(file, out);
    if (result.failed())
        return result;
    if (out.name.isEmpty())
        out.name = file.getFileNameWithoutExtension();
    // Names are what the person recognises tracks by: keep them apart.
    std::set<juce::String> used;
    auto index = 0;
    for (auto& track : out.tracks)
        track.name = uniqueName(used, track.name, (track.midi ? "Inst " : "Audio ") + juce::String(++index));
    return juce::Result::ok();
}

juce::String describeImportedProject(const ImportedProject& project)
{
    juce::StringArray lines;
    lines.add(project.name + " (" + project.format
              + (project.application.isNotEmpty() ? ", " + project.application : juce::String()) + ")");
    lines.add(juce::String(project.tempo, 2) + " BPM, " + juce::String(project.numerator) + "/"
              + juce::String(project.denominator));
    auto notes = 0;
    auto clips = 0;
    for (const auto& track : project.tracks) {
        auto trackNotes = 0;
        for (const auto& clip : track.midiClips)
            trackNotes += static_cast<int>(clip.notes.size());
        const auto trackClips = static_cast<int>(track.midiClips.size() + track.audioClips.size());
        notes += trackNotes;
        clips += trackClips;
        auto line = "  " + track.name + (track.midi ? " [MIDI] " : " [audio] ") + juce::String(trackClips)
                    + " clip(s)";
        if (trackNotes > 0)
            line += ", " + juce::String(trackNotes) + " note(s)";
        if (!track.devices.isEmpty())
            line += "; devices: " + track.devices.joinIntoString(", ");
        lines.add(line);
    }
    lines.insert(2, juce::String(static_cast<int>(project.tracks.size())) + " track(s), "
                        + juce::String(clips) + " clip(s), " + juce::String(notes) + " note(s)");
    if (!project.missingFiles.isEmpty()) {
        lines.add("Missing audio files:");
        for (const auto& missing : project.missingFiles)
            lines.add("  " + missing);
    }
    if (!project.warnings.isEmpty()) {
        lines.add("To review:");
        for (const auto& warning : project.warnings)
            lines.add("  " + warning);
    }
    return lines.joinIntoString("\n");
}

} // namespace cydaw::app::project_import
