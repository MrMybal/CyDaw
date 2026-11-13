#include <cydaw/base/File.h>
#include <cydaw/base/Xml.h>
#include <cydaw/base/Text.h>
#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <map>

// Ardour's session (.ardour): XML. Sources (the files: audiofiles and
// midifiles under interchange/<session>/), routes (tracks and buses, their
// gain, pan, mute, solo and plug-ins), playlists of regions (a source, its
// place, its length, where in the source it starts). Ardour 7 and later write
// a time as "a<superclock>" (audio time) or "b<ticks>" (1920 a beat);
// earlier sessions in samples.

namespace cydaw::app::project_import {
namespace {

using namespace shared;

constexpr double ticksPerBeat = 1'920.0;

class ArdourReader {
public:
    ArdourReader(const cydaw::base::File& file, ImportedProject& out) : file_(file), out_(out), folder_(file.getParentDirectory()) {}

    cydaw::base::Result read()
    {
        const auto document = cydaw::base::XmlDocument::parse(file_);
        if (document == nullptr || !document->hasTagName("Session"))
            return cydaw::base::Result::fail("Not an Ardour session: " + file_.getFileName());
        out_.format = "ardour";
        out_.application = "Ardour (session " + document->getStringAttribute("version") + ")";
        out_.name = document->getStringAttribute("name");
        sampleRate_ = std::max(1.0, document->getDoubleAttribute("sample-rate", 48'000.0));
        readTempo(*document);
        if (const auto* sources = document->getChildByName("Sources"))
            for (const auto* source : sources->getChildWithTagNameIterator("Source"))
                sources_[source->getStringAttribute("id")] = source;
        if (const auto* playlists = document->getChildByName("Playlists"))
            for (const auto* playlist : playlists->getChildWithTagNameIterator("Playlist")) {
                playlists_[playlist->getStringAttribute("id")] = playlist;
                playlistsByName_[playlist->getStringAttribute("name")] = playlist;
            }
        if (const auto* routes = document->getChildByName("Routes"))
            for (const auto* route : routes->getChildWithTagNameIterator("Route"))
                readRoute(*route);
        return cydaw::base::Result::ok();
    }

private:
    void readTempo(const cydaw::base::XmlElement& session)
    {
        const auto* map = session.getChildByName("TempoMap");
        if (map == nullptr)
            return;
        superclocks_ = map->getDoubleAttribute("superclocks-per-second", superclocks_);
        const cydaw::base::XmlElement* tempo = nullptr;
        const cydaw::base::XmlElement* meter = nullptr;
        auto tempos = 0;
        std::function<void(const cydaw::base::XmlElement&)> walk = [&](const cydaw::base::XmlElement& element) {
            for (const auto* child : element.getChildIterator()) {
                if (child->hasTagName("Tempo")) {
                    ++tempos;
                    if (tempo == nullptr)
                        tempo = child;
                } else if (child->hasTagName("Meter") && meter == nullptr) {
                    meter = child;
                }
                walk(*child);
            }
        };
        walk(*map);
        if (tempo != nullptr) {
            // Notes per minute of `note-type` (a quarter is 4).
            auto bpm = tempo->getDoubleAttribute("npm", tempo->getDoubleAttribute("beats-per-minute", 120.0));
            const auto noteType = tempo->getDoubleAttribute("note-type", 4.0);
            if (noteType > 0.0)
                bpm *= 4.0 / noteType;
            out_.tempo = juce::jlimit(20.0, 999.0, bpm);
        }
        if (tempos > 1)
            out_.warnings.add("The tempo changes along the session: CyDAW keeps " + cydaw::base::Text(out_.tempo, 2) + " BPM.");
        if (meter != nullptr) {
            out_.numerator = juce::jlimit(1, 32, static_cast<int>(meter->getDoubleAttribute("divisions-per-bar", 4.0)));
            out_.denominator = juce::jlimit(
                1, 32, static_cast<int>(meter->getDoubleAttribute("note-value", meter->getDoubleAttribute("note-type", 4.0))));
        }
    }

    // A time in seconds, whichever way it is written.
    [[nodiscard]] double seconds(const cydaw::base::Text& text) const
    {
        const auto value = text.trim();
        if (value.startsWithChar('a'))
            return value.substring(1).getDoubleValue() / superclocks_;
        if (value.startsWithChar('b'))
            return out_.beatsToSeconds(value.substring(1).getDoubleValue() / ticksPerBeat);
        return value.getDoubleValue() / sampleRate_;
    }

    [[nodiscard]] double beats(const cydaw::base::Text& text) const
    {
        const auto value = text.trim();
        if (value.startsWithChar('b'))
            return value.substring(1).getDoubleValue() / ticksPerBeat;
        return out_.secondsToBeats(seconds(value));
    }

    static const cydaw::base::XmlElement* controllable(const cydaw::base::XmlElement& element, const cydaw::base::Text& name)
    {
        for (const auto* child : element.getChildIterator()) {
            if (child->hasTagName("Controllable") && child->getStringAttribute("name") == name)
                return child;
            if (const auto* inner = controllable(*child, name))
                return inner;
        }
        return nullptr;
    }

    void readRoute(const cydaw::base::XmlElement& route)
    {
        const auto* presentation = route.getChildByName("PresentationInfo");
        // Where it says what it is: its PresentationInfo, or (Ardour 3 to 4) the route itself.
        const auto flags = (presentation != nullptr ? presentation->getStringAttribute("flags") : cydaw::base::Text()) + ","
                           + route.getStringAttribute("flags");
        const auto* gain = controllable(route, "gaincontrol");
        if (flags.contains("MasterOut")) {
            if (gain != nullptr)
                out_.masterGainDb = linearToDb(gain->getDoubleAttribute("value", 1.0));
            return;
        }
        if (flags.contains("MonitorOut") || flags.contains("VCA") || flags.contains("FoldbackBus"))
            return;
        ImportedTrack track;
        track.name = route.getStringAttribute("name");
        track.midi = flags.contains("MidiTrack") || route.getStringAttribute("default-type") == "midi";
        if (gain != nullptr)
            track.gainDb = linearToDb(gain->getDoubleAttribute("value", 1.0));
        if (const auto* pan = controllable(route, "pan-azimuth"))
            track.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, pan->getDoubleAttribute("value", 0.5) * 2.0 - 1.0));
        if (const auto* mute = controllable(route, "mute"))
            track.muted = mute->getDoubleAttribute("value", 0.0) > 0.5;
        if (const auto* solo = controllable(route, "solo"))
            track.solo = solo->getDoubleAttribute("value", 0.0) > 0.5 || solo->getBoolAttribute("self-solo", false);
        if (presentation != nullptr && presentation->hasAttribute("color")) {
            const auto colour = static_cast<juce::uint32>(presentation->getStringAttribute("color").getLargeIntValue());
            // RGBA.
            if (colour != 0)
                track.colour = opaque(static_cast<juce::uint8>(colour >> 24), static_cast<juce::uint8>(colour >> 16),
                                      static_cast<juce::uint8>(colour >> 8));
        }
        if (flags.contains("AudioBus") || flags.contains("MidiBus"))
            out_.warnings.add("Bus \"" + track.name + "\" came in as a track without clips; its sends were left out.");
        // Its plug-ins, in their order: on a MIDI track the first is its instrument.
        for (const auto* processor : route.getChildWithTagNameIterator("Processor")) {
            const auto type = processor->getStringAttribute("type");
            static const cydaw::base::TextList plugins { "lv2", "vst", "lxvst", "vst3", "ladspa", "au", "mac-vst", "luaproc" };
            if (!plugins.contains(type))
                continue;
            ImportedDevice device;
            device.name = processor->getStringAttribute("name");
            device.plugin = type != "luaproc";
            device.type = device.plugin ? cydaw::base::Text() : device.name;
            device.format = type == "vst3" ? "VST3" : type.startsWith("vst") || type == "lxvst" || type == "mac-vst" ? "VST2"
                            : type == "au" ? "AU" : type.toUpperCase();
            device.instrument = track.midi && !track.hadInstrument;
            track.hadInstrument = track.hadInstrument || device.instrument;
            track.devices.add(device.name);
            track.chain.push_back(std::move(device));
        }
        // Its regions.
        const cydaw::base::XmlElement* playlist = nullptr;
        for (const auto* key : { "audio-playlist", "midi-playlist" })
            if (const auto found = playlists_.find(route.getStringAttribute(key)); found != playlists_.end())
                playlist = found->second;
        if (playlist == nullptr)
            if (const auto found = playlistsByName_.find(route.getStringAttribute("playlist")); found != playlistsByName_.end())
                playlist = found->second;
        if (playlist != nullptr)
            for (const auto* region : playlist->getChildWithTagNameIterator("Region"))
                readRegion(*region, track);
        out_.tracks.push_back(std::move(track));
    }

    [[nodiscard]] cydaw::base::File sourceFile(const cydaw::base::XmlElement& source, const cydaw::base::Text& kind) const
    {
        const auto name = source.getStringAttribute("name");
        const auto origin = source.getStringAttribute("origin");
        const auto session = out_.name.isNotEmpty() ? out_.name : file_.getFileNameWithoutExtension();
        for (const auto& candidate : { folder_.getChildFile("interchange").getChildFile(session).getChildFile(kind).getChildFile(name),
                                       cydaw::base::File::isAbsolutePath(name) ? cydaw::base::File(name) : cydaw::base::File(),
                                       cydaw::base::File::isAbsolutePath(origin) ? cydaw::base::File(origin) : cydaw::base::File() })
            if (candidate != cydaw::base::File() && candidate.existsAsFile())
                return candidate;
        return resolveProjectAudioFile(folder_, name);
    }

    void readRegion(const cydaw::base::XmlElement& region, ImportedTrack& track)
    {
        const auto found = sources_.find(region.getStringAttribute("source-0"));
        if (found == sources_.end())
            return;
        const auto& source = *found->second;
        const auto midi = source.getStringAttribute("type") == "midi" || region.getStringAttribute("type") == "midi";
        const auto name = region.getStringAttribute("name");
        const auto muted = region.getBoolAttribute("muted", false);
        if (midi) {
            ImportedMidiClip clip;
            clip.name = name;
            clip.muted = muted;
            clip.startBeats = beats(region.getStringAttribute("position"));
            clip.lengthBeats = std::max(0.25, beats(region.getStringAttribute("length")));
            const auto offset = beats(region.getStringAttribute("start"));
            const auto file = sourceFile(source, "midifiles");
            if (file == cydaw::base::File()) {
                out_.missingFiles.addIfNotAlreadyThere(source.getStringAttribute("name"));
            } else {
                juce::FileInputStream input(file);
                juce::MidiFile smf;
                if (input.openedOk() && smf.readFrom(input)) {
                    const auto perQuarter = std::max<short>(1, smf.getTimeFormat());
                    for (int index = 0; index < smf.getNumTracks(); ++index) {
                        juce::MidiMessageSequence sequence(*smf.getTrack(index));
                        sequence.updateMatchedPairs();
                        for (const auto* event : sequence) {
                            if (!event->message.isNoteOn() || event->noteOffObject == nullptr)
                                continue;
                            const auto start = event->message.getTimeStamp() / perQuarter - offset;
                            const auto end = event->noteOffObject->message.getTimeStamp() / perQuarter - offset;
                            if (start < -1.0e-9 || start >= clip.lengthBeats)
                                continue;
                            clip.notes.push_back({ event->message.getNoteNumber(), start,
                                                   std::max(1.0e-3, std::min(end, clip.lengthBeats) - start),
                                                   event->message.getFloatVelocity() });
                        }
                    }
                }
            }
            track.midi = true;
            track.midiClips.push_back(std::move(clip));
            return;
        }
        ImportedAudioClip clip;
        clip.name = name;
        clip.originalPath = source.getStringAttribute("name");
        clip.file = sourceFile(source, "audiofiles");
        if (clip.file == cydaw::base::File())
            out_.missingFiles.addIfNotAlreadyThere(clip.originalPath);
        // A stereo region: its two mono files (left, right) made one.
        if (const auto right = sources_.find(region.getStringAttribute("source-1")); right != sources_.end() && clip.file != cydaw::base::File()) {
            const auto rightFile = sourceFile(*right->second, "audiofiles");
            if (rightFile != cydaw::base::File())
                clip.file = joinChannels(clip.file, rightFile, out_.name.isNotEmpty() ? out_.name : file_.getFileNameWithoutExtension(),
                                         clip.originalPath.upToLastOccurrenceOf(".", false, false).upToLastOccurrenceOf("%", false, false),
                                         out_);
            else
                out_.missingFiles.addIfNotAlreadyThere(right->second->getStringAttribute("name"));
        }
        clip.startSeconds = std::max(0.0, seconds(region.getStringAttribute("position")));
        clip.durationSeconds = std::max(0.01, seconds(region.getStringAttribute("length")));
        clip.sourceOffsetSeconds = std::max(0.0, seconds(region.getStringAttribute("start")));
        clip.gainDb = linearToDb(region.getDoubleAttribute("scale-amplitude", 1.0));
        clip.muted = muted;
        if (const auto stretch = region.getDoubleAttribute("stretch", 1.0); std::abs(stretch - 1.0) > 1.0e-6)
            clip.sourceDurationSeconds = clip.durationSeconds / stretch;
        track.audioClips.push_back(std::move(clip));
    }

    cydaw::base::File file_;
    ImportedProject& out_;
    cydaw::base::File folder_;
    double sampleRate_ { 48'000.0 };
    double superclocks_ { 282'240'000.0 };
    std::map<cydaw::base::Text, const cydaw::base::XmlElement*> sources_;
    std::map<cydaw::base::Text, const cydaw::base::XmlElement*> playlists_;
    std::map<cydaw::base::Text, const cydaw::base::XmlElement*> playlistsByName_;
};

} // namespace

cydaw::base::Result importArdourSession(const cydaw::base::File& file, ImportedProject& out)
{
    out.source = file;
    ArdourReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
