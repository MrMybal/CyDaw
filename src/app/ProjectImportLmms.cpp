#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <map>

// LMMS's project: .mmp (XML) or .mmpz (the same, compressed the way Qt's
// qCompress does: a 4-byte big-endian length, then a zlib stream). Times are
// ticks, 192 a bar of 4/4 (48 a beat); a key is a MIDI note less 12 (57 is
// A4); volumes are percent (100: unity), pans -100..100.
// Instrument tracks (type 0) hold patterns of notes; a beat/bassline track
// (type 1) places the patterns of the instruments of its own container,
// which CyDAW unrolls onto those instruments' tracks; a sample track (type 2)
// holds audio clips.

namespace cydaw::app::project_import {
namespace {

using namespace shared;

constexpr double ticksPerBeat = 48.0;

class LmmsReader {
public:
    LmmsReader(const juce::File& file, ImportedProject& out) : file_(file), out_(out), folder_(file.getParentDirectory()) {}

    juce::Result read()
    {
        std::unique_ptr<juce::XmlElement> document;
        if (file_.hasFileExtension("mmpz")) {
            juce::MemoryBlock bytes;
            if (!file_.loadFileAsData(bytes) || bytes.getSize() < 6)
                return juce::Result::fail("The file could not be opened: " + file_.getFullPathName());
            juce::MemoryInputStream compressed(static_cast<const char*>(bytes.getData()) + 4, bytes.getSize() - 4, false);
            juce::GZIPDecompressorInputStream unzipped(&compressed, false, juce::GZIPDecompressorInputStream::zlibFormat);
            document = juce::XmlDocument::parse(readBounded(unzipped));
        } else {
            document = juce::XmlDocument::parse(file_);
        }
        if (document == nullptr || !document->hasTagName("lmms-project"))
            return juce::Result::fail("Not an LMMS project: " + file_.getFileName());
        out_.format = "lmms";
        out_.application = "LMMS " + document->getStringAttribute("creatorversion");
        if (const auto* head = document->getChildByName("head")) {
            out_.tempo = juce::jlimit(10.0, 999.0, head->getDoubleAttribute("bpm", 140.0));
            out_.numerator = juce::jlimit(1, 32, head->getIntAttribute("timesig_numerator", 4));
            out_.denominator = juce::jlimit(1, 32, head->getIntAttribute("timesig_denominator", 4));
            out_.masterGainDb = linearToDb(head->getDoubleAttribute("mastervol", 100.0) / 100.0);
        }
        const auto* song = document->getChildByName("song");
        const auto* container = song != nullptr ? song->getChildByName("trackcontainer") : nullptr;
        if (container == nullptr)
            return juce::Result::fail("The LMMS project holds no song");
        // The beat/bassline tracks in their order: the n-th plays the
        // patterns at place n of the instruments of the one container they
        // share (written in the first of them).
        std::vector<const juce::XmlElement*> beatTracks;
        for (const auto* track : container->getChildWithTagNameIterator("track")) {
            switch (track->getIntAttribute("type")) {
            case 0: readInstrument(*track); break;
            case 1: beatTracks.push_back(track); break;
            case 2: readSamples(*track); break;
            case 5:
            case 6: automation_ = true; break;
            default: break;
            }
        }
        readBeats(beatTracks);
        if (automation_)
            out_.warnings.add("Automation tracks were left out.");
        return juce::Result::ok();
    }

private:
    [[nodiscard]] static double beats(const double ticks) { return ticks / ticksPerBeat; }

    // An instrument track's own: its name, mix, instrument and effects.
    ImportedTrack instrumentTrack(const juce::XmlElement& element)
    {
        ImportedTrack track;
        track.midi = true;
        track.name = element.getStringAttribute("name");
        track.muted = element.getBoolAttribute("muted", false);
        track.solo = element.getBoolAttribute("solo", false);
        const auto* instrumentTrack = element.getChildByName("instrumenttrack");
        if (instrumentTrack != nullptr) {
            track.gainDb = linearToDb(instrumentTrack->getDoubleAttribute("vol", 100.0) / 100.0);
            track.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, instrumentTrack->getDoubleAttribute("pan", 0.0) / 100.0));
            if (const auto* instrument = instrumentTrack->getChildByName("instrument"))
                track.chain.push_back(deviceOf(*instrument));
            if (const auto* chain = instrumentTrack->getChildByName("fxchain"))
                readEffects(*chain, track);
        }
        for (const auto& device : track.chain)
            track.devices.add(device.name);
        track.hadInstrument = !track.chain.empty() && track.chain.front().instrument;
        return track;
    }

    // Its clips ("pattern", "midiclip" since LMMS 1.3), in the song.
    static std::vector<const juce::XmlElement*> patternsOf(const juce::XmlElement& element)
    {
        std::vector<const juce::XmlElement*> patterns;
        for (const auto* child : element.getChildIterator())
            if (child->hasTagName("pattern") || child->hasTagName("midiclip"))
                patterns.push_back(child);
        return patterns;
    }

    void readInstrument(const juce::XmlElement& element)
    {
        auto track = instrumentTrack(element);
        for (const auto* pattern : patternsOf(element)) {
            ImportedMidiClip clip;
            clip.name = pattern->getStringAttribute("name", track.name);
            clip.startBeats = beats(pattern->getDoubleAttribute("pos"));
            clip.lengthBeats = std::max(0.25, beats(patternLength(*pattern)));
            clip.muted = pattern->getBoolAttribute("muted", false);
            clip.notes = notesOf(*pattern);
            track.midiClips.push_back(std::move(clip));
        }
        out_.tracks.push_back(std::move(track));
    }

    [[nodiscard]] static double patternLength(const juce::XmlElement& pattern)
    {
        auto last = 0.0;
        for (const auto* note : pattern.getChildWithTagNameIterator("note"))
            last = std::max(last, note->getDoubleAttribute("pos") + std::max(1.0, note->getDoubleAttribute("len")));
        // A step pattern ("steps", 12 ticks each) is as long as its steps.
        const auto steps = pattern.getDoubleAttribute("steps", 0.0) * 12.0;
        const auto length = std::max({ pattern.getDoubleAttribute("len", 0.0), last, steps });
        return length > 0.0 ? std::ceil(length / 192.0 - 1.0e-9) * 192.0 : 192.0;
    }

    [[nodiscard]] static std::vector<ImportedNote> notesOf(const juce::XmlElement& pattern)
    {
        std::vector<ImportedNote> notes;
        for (const auto* note : pattern.getChildWithTagNameIterator("note")) {
            // A step's note has a negative length: as long as a step.
            const auto length = note->getDoubleAttribute("len", 48.0);
            notes.push_back({ juce::jlimit(0, 127, note->getIntAttribute("key", 57) + 12), beats(note->getDoubleAttribute("pos")),
                              std::max(1.0e-3, beats(length > 0.0 ? length : 12.0)),
                              static_cast<float>(juce::jlimit(0.05, 1.0, note->getDoubleAttribute("vol", 100.0) / 125.0)) });
        }
        return notes;
    }

    // A sound LMMS names relative to its samples folder ("drums/kick01.ogg"):
    // next to the project, else in LMMS's own folders.
    juce::File sampleFile(const juce::String& written)
    {
        // LMMS 1.3 marks where a path starts from: its own samples
        // ("factorysample:"), the person's ("usersample:").
        auto path = written.trim();
        for (const auto* prefix : { "factorysample:", "usersample:" })
            if (path.startsWith(prefix))
                path = path.fromFirstOccurrenceOf(prefix, false, false);
        if (path.isEmpty())
            return {};
        if (const auto found = resolveProjectAudioFile(folder_, path); found != juce::File())
            return found;
        if (!juce::File::isAbsolutePath(path))
            for (const auto& root : { juce::File::getSpecialLocation(juce::File::globalApplicationsDirectory).getChildFile("LMMS/data/samples"),
                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("lmms/samples"),
                                      juce::File("/usr/share/lmms/samples") })
                if (const auto candidate = root.getChildFile(path); candidate.existsAsFile())
                    return candidate;
        out_.missingFiles.addIfNotAlreadyThere(written);
        return {};
    }

    // LMMS's own instruments, as CyDAW reads them (ImportedInstruments);
    // VeSTige hosts a plug-in, AudioFileProcessor plays a sound.
    ImportedDevice deviceOf(const juce::XmlElement& instrument)
    {
        ImportedDevice device;
        device.instrument = true;
        device.type = instrument.getStringAttribute("name");
        device.name = device.type;
        const auto* settings = instrument.getFirstChildElement();
        if (device.type == "vestige" && settings != nullptr) {
            device.plugin = true;
            const auto path = settings->getStringAttribute("plugin");
            device.name = fileNameOf(path).upToLastOccurrenceOf(".", false, false);
            device.format = path.endsWithIgnoreCase(".vst3") ? "VST3" : "VST2";
        } else if (device.type == "audiofileprocessor" && settings != nullptr) {
            device.sample = sampleFile(settings->getStringAttribute("src"));
        }
        return device;
    }

    void readEffects(const juce::XmlElement& chain, ImportedTrack& track)
    {
        for (const auto* effect : chain.getChildWithTagNameIterator("effect")) {
            ImportedDevice device;
            device.type = effect->getStringAttribute("name");
            device.name = device.type;
            if (device.type == "vsteffectcontrols" || device.type == "vsteffect") {
                device.plugin = true;
                if (const auto* settings = effect->getChildByName("vsteffectcontrols"))
                    device.name = fileNameOf(settings->getStringAttribute("plugin")).upToLastOccurrenceOf(".", false, false);
                device.format = "VST2";
            } else if (device.type == "ladspaeffect" || device.type == "lv2effect") {
                device.plugin = true;
                device.format = device.type == "lv2effect" ? "LV2" : "LADSPA";
                if (const auto* key = effect->getChildByName("key"))
                    if (const auto* attribute = key->getChildByName("attribute"))
                        device.name = attribute->getStringAttribute("value", device.name);
            }
            track.chain.push_back(std::move(device));
        }
    }

    // The beat/bassline tracks ("patterntrack" since LMMS 1.3): each one's
    // placements play, on every instrument of their shared container, that
    // instrument's pattern at the track's place — a clip a placement on the
    // instrument's own track.
    void readBeats(const std::vector<const juce::XmlElement*>& beatTracks)
    {
        const juce::XmlElement* container = nullptr;
        for (const auto* beatTrack : beatTracks)
            for (const auto* holder : { beatTrack->getChildByName("bbtrack"), beatTrack->getChildByName("patterntrack") })
                if (container == nullptr && holder != nullptr)
                    container = holder->getChildByName("trackcontainer");
        if (container == nullptr)
            return;
        for (const auto* instrument : container->getChildWithTagNameIterator("track")) {
            if (instrument->getIntAttribute("type") != 0)
                continue;
            auto track = instrumentTrack(*instrument);
            const auto patterns = patternsOf(*instrument);
            for (std::size_t index = 0; index < beatTracks.size(); ++index) {
                // Its pattern at this place (one a bar: at index * 192).
                const juce::XmlElement* pattern = nullptr;
                for (const auto* candidate : patterns)
                    if (static_cast<std::size_t>(std::lround(candidate->getDoubleAttribute("pos") / 192.0)) == index)
                        pattern = candidate;
                if (pattern == nullptr && index < patterns.size())
                    pattern = patterns[index];
                if (pattern == nullptr || pattern->getChildByName("note") == nullptr)
                    continue;
                const auto notes = notesOf(*pattern);
                const auto content = std::max(beats(patternLength(*pattern)), 0.25);
                const auto name = beatTracks[index]->getStringAttribute("name");
                for (const auto* placed : beatTracks[index]->getChildIterator()) {
                    if (!placed->hasTagName("bbtco") && !placed->hasTagName("patternclip"))
                        continue;
                    ImportedMidiClip clip;
                    clip.name = name;
                    clip.startBeats = beats(placed->getDoubleAttribute("pos"));
                    clip.lengthBeats = std::max(0.25, beats(std::max(1.0, placed->getDoubleAttribute("len", 192.0))));
                    clip.muted = placed->getBoolAttribute("muted", false) || beatTracks[index]->getBoolAttribute("muted", false);
                    repeatContent(content, clip.lengthBeats, true,
                                  [&](const double from, const double to, const double at) { placeNotes(notes, from, to, at, clip.notes); });
                    track.midiClips.push_back(std::move(clip));
                }
            }
            if (!track.midiClips.empty())
                out_.tracks.push_back(std::move(track));
        }
    }

    void readSamples(const juce::XmlElement& element)
    {
        ImportedTrack track;
        track.name = element.getStringAttribute("name");
        track.muted = element.getBoolAttribute("muted", false);
        track.solo = element.getBoolAttribute("solo", false);
        if (const auto* settings = element.getChildByName("sampletrack")) {
            track.gainDb = linearToDb(settings->getDoubleAttribute("vol", 100.0) / 100.0);
            track.pan = static_cast<float>(juce::jlimit(-1.0, 1.0, settings->getDoubleAttribute("pan", 0.0) / 100.0));
            if (const auto* chain = settings->getChildByName("fxchain"))
                readEffects(*chain, track);
        }
        for (const auto& device : track.chain)
            track.devices.add(device.name);
        for (const auto* placed : element.getChildWithTagNameIterator("sampletco")) {
            ImportedAudioClip clip;
            clip.originalPath = placed->getStringAttribute("src");
            clip.name = fileNameOf(clip.originalPath);
            clip.file = sampleFile(clip.originalPath);
            clip.startSeconds = out_.beatsToSeconds(beats(placed->getDoubleAttribute("pos")));
            clip.durationSeconds = std::max(0.01, out_.beatsToSeconds(beats(placed->getDoubleAttribute("len", 192.0))));
            clip.sourceOffsetSeconds = std::max(0.0, -out_.beatsToSeconds(beats(placed->getDoubleAttribute("off", 0.0))));
            clip.muted = placed->getBoolAttribute("muted", false);
            track.audioClips.push_back(std::move(clip));
        }
        out_.tracks.push_back(std::move(track));
    }

    juce::File file_;
    ImportedProject& out_;
    juce::File folder_;
    bool automation_ {};
};

} // namespace

juce::Result importLmmsProject(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    LmmsReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
