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
    ArdourReader(const juce::File& file, ImportedProject& out) : file_(file), out_(out), folder_(file.getParentDirectory()) {}

    juce::Result read()
    {
        const auto document = juce::XmlDocument::parse(file_);
        if (document == nullptr || !document->hasTagName("Session"))
            return juce::Result::fail("Not an Ardour session: " + file_.getFileName());
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
        return juce::Result::ok();
    }

private:
    void readTempo(const juce::XmlElement& session)
    {
        const auto* map = session.getChildByName("TempoMap");
        if (map == nullptr)
            return;
        superclocks_ = map->getDoubleAttribute("superclocks-per-second", superclocks_);
        const juce::XmlElement* tempo = nullptr;
        const juce::XmlElement* meter = nullptr;
        auto tempos = 0;
        std::function<void(const juce::XmlElement&)> walk = [&](const juce::XmlElement& element) {
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
            out_.warnings.add("The tempo changes along the session: CyDAW keeps " + juce::String(out_.tempo, 2) + " BPM.");
        if (meter != nullptr) {
            out_.numerator = juce::jlimit(1, 32, static_cast<int>(meter->getDoubleAttribute("divisions-per-bar", 4.0)));
            out_.denominator = juce::jlimit(
                1, 32, static_cast<int>(meter->getDoubleAttribute("note-value", meter->getDoubleAttribute("note-type", 4.0))));
        }
    }

    // A time in seconds, whichever way it is written.
    [[nodiscard]] double seconds(const juce::String& text) const
    {
        const auto value = text.trim();
        if (value.startsWithChar('a'))
            return value.substring(1).getDoubleValue() / superclocks_;
        if (value.startsWithChar('b'))
            return out_.beatsToSeconds(value.substring(1).getDoubleValue() / ticksPerBeat);
        return value.getDoubleValue() / sampleRate_;
    }

    [[nodiscard]] double beats(const juce::String& text) const
    {
        const auto value = text.trim();
        if (value.startsWithChar('b'))
            return value.substring(1).getDoubleValue() / ticksPerBeat;
        return out_.secondsToBeats(seconds(value));
    }

    static const juce::XmlElement* controllable(const juce::XmlElement& element, const juce::String& name)
    {
        for (const auto* child : element.getChildIterator()) {
            if (child->hasTagName("Controllable") && child->getStringAttribute("name") == name)
                return child;
            if (const auto* inner = controllable(*child, name))
                return inner;
        }
        return nullptr;
    }

    void readRoute(const juce::XmlElement& route)
    {
        const auto* presentation = route.getChildByName("PresentationInfo");
        // Where it says what it is: its PresentationInfo, or (Ardour 3 to 4) the route itself.
        const auto flags = (presentation != nullptr ? presentation->getStringAttribute("flags") : juce::String()) + ","
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
            static const juce::StringArray plugins { "lv2", "vst", "lxvst", "vst3", "ladspa", "au", "mac-vst", "luaproc" };
            if (!plugins.contains(type))
                continue;
            ImportedDevice device;
            device.name = processor->getStringAttribute("name");
            device.plugin = type != "luaproc";
            device.type = device.plugin ? juce::String() : device.name;
            device.format = type == "vst3" ? "VST3" : type.startsWith("vst") || type == "lxvst" || type == "mac-vst" ? "VST2"
                            : type == "au" ? "AU" : type.toUpperCase();
            device.instrument = track.midi && !track.hadInstrument;
            track.hadInstrument = track.hadInstrument || device.instrument;
            track.devices.add(device.name);
            track.chain.push_back(std::move(device));
        }
        // Its regions.
        const juce::XmlElement* playlist = nullptr;
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

    [[nodiscard]] juce::File sourceFile(const juce::XmlElement& source, const juce::String& kind) const
    {
        const auto name = source.getStringAttribute("name");
        const auto origin = source.getStringAttribute("origin");
        const auto session = out_.name.isNotEmpty() ? out_.name : file_.getFileNameWithoutExtension();
        for (const auto& candidate : { folder_.getChildFile("interchange").getChildFile(session).getChildFile(kind).getChildFile(name),
                                       juce::File::isAbsolutePath(name) ? juce::File(name) : juce::File(),
                                       juce::File::isAbsolutePath(origin) ? juce::File(origin) : juce::File() })
            if (candidate != juce::File() && candidate.existsAsFile())
                return candidate;
        return resolveProjectAudioFile(folder_, name);
    }

    void readRegion(const juce::XmlElement& region, ImportedTrack& track)
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
            if (file == juce::File()) {
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
        if (clip.file == juce::File())
            out_.missingFiles.addIfNotAlreadyThere(clip.originalPath);
        // A stereo region: its two mono files (left, right) made one.
        if (const auto right = sources_.find(region.getStringAttribute("source-1")); right != sources_.end() && clip.file != juce::File()) {
            const auto rightFile = sourceFile(*right->second, "audiofiles");
            if (rightFile != juce::File())
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

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
    double sampleRate_ { 48'000.0 };
    double superclocks_ { 282'240'000.0 };
    std::map<juce::String, const juce::XmlElement*> sources_;
    std::map<juce::String, const juce::XmlElement*> playlists_;
    std::map<juce::String, const juce::XmlElement*> playlistsByName_;
};

} // namespace

juce::Result importArdourSession(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    ArdourReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
