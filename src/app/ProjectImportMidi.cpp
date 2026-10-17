#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <map>

// What any application can export when CyDAW cannot read its project (Reason,
// and the others): a standard MIDI file of its parts, and its tracks bounced
// to audio files ("stems") in the same folder. A track per MIDI track with
// notes (its name, its tempo and signature from the file; channel 10 a drum
// kit), and a track per audio file beside it, from the start.

namespace cydaw::app::project_import {
namespace {

using namespace shared;

class MidiStemsReader {
public:
    MidiStemsReader(const juce::File& file, ImportedProject& out) : file_(file), out_(out) {}

    juce::Result read()
    {
        juce::FileInputStream input(file_);
        juce::MidiFile midi;
        if (!input.openedOk() || !midi.readFrom(input))
            return juce::Result::fail("Not a MIDI file: " + file_.getFileName());
        const auto perQuarter = midi.getTimeFormat();
        if (perQuarter <= 0)
            return juce::Result::fail("A MIDI file counted in timecode (SMPTE) is not read: " + file_.getFileName());
        out_.format = "midi";
        out_.application = "MIDI file and stems";
        out_.name = file_.getFileNameWithoutExtension();
        // The tempo and signature: the first ones.
        auto tempoFound = false;
        auto tempoChanges = 0;
        for (int index = 0; index < midi.getNumTracks(); ++index)
            for (const auto* event : *midi.getTrack(index)) {
                const auto& message = event->message;
                if (message.isTempoMetaEvent()) {
                    ++tempoChanges;
                    if (!tempoFound) {
                        out_.tempo = juce::jlimit(20.0, 999.0, 60.0 / std::max(1.0e-6, message.getTempoSecondsPerQuarterNote()));
                        tempoFound = true;
                    }
                } else if (message.isTimeSignatureMetaEvent() && event->message.getTimeStamp() <= 0.0) {
                    int numerator = 4;
                    int denominator = 4;
                    message.getTimeSignatureInfo(numerator, denominator);
                    out_.numerator = juce::jlimit(1, 32, numerator);
                    out_.denominator = juce::jlimit(1, 32, denominator);
                }
            }
        if (tempoChanges > 1)
            out_.warnings.add("The tempo changes along the MIDI file: CyDAW keeps " + juce::String(out_.tempo, 2) + " BPM.");
        for (int index = 0; index < midi.getNumTracks(); ++index) {
            juce::MidiMessageSequence sequence(*midi.getTrack(index));
            sequence.updateMatchedPairs();
            juce::String name;
            // By channel: a part of its own each.
            std::map<int, std::vector<ImportedNote>> byChannel;
            auto last = 0.0;
            for (const auto* event : sequence) {
                const auto& message = event->message;
                if (message.isTrackNameEvent() && name.isEmpty())
                    name = message.getTextFromTextMetaEvent().trim();
                if (!message.isNoteOn() || event->noteOffObject == nullptr)
                    continue;
                const auto start = message.getTimeStamp() / perQuarter;
                const auto end = event->noteOffObject->message.getTimeStamp() / perQuarter;
                byChannel[message.getChannel()].push_back(
                    { message.getNoteNumber(), start, std::max(1.0e-3, end - start), message.getFloatVelocity() });
                last = std::max(last, end);
            }
            for (auto& [channel, notes] : byChannel) {
                ImportedTrack track;
                track.midi = true;
                track.name = name.isNotEmpty() ? name : "MIDI " + juce::String(index + 1);
                if (byChannel.size() > 1)
                    track.name << " (" << juce::String(channel) << ")";
                ImportedDevice device;
                device.instrument = true;
                device.type = channel == 10 ? "Drums" : "Instrument";
                device.name = device.type;
                track.chain.push_back(device);
                track.hadInstrument = true;
                ImportedMidiClip clip;
                clip.name = track.name;
                clip.lengthBeats = std::max(1.0, std::ceil(last / 4.0) * 4.0);
                clip.notes = std::move(notes);
                track.midiClips.push_back(std::move(clip));
                out_.tracks.push_back(std::move(track));
            }
        }
        // The audio files beside it: each a track, from the start.
        juce::Array<juce::File> stems;
        file_.getParentDirectory().findChildFiles(stems, juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        stems.sort();
        if (stems.size() > 64) {
            out_.warnings.add(juce::String(stems.size()) + " audio files beside the MIDI file: none was taken as a stem (too many).");
            stems.clear();
        }
        for (const auto& stem : stems) {
            ImportedTrack track;
            track.name = stem.getFileNameWithoutExtension();
            ImportedAudioClip clip;
            clip.name = track.name;
            clip.file = stem;
            clip.originalPath = stem.getFullPathName();
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            if (std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(stem)); reader != nullptr && reader->sampleRate > 0.0)
                clip.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            if (clip.durationSeconds <= 0.0)
                continue;
            track.audioClips.push_back(std::move(clip));
            out_.tracks.push_back(std::move(track));
        }
        if (!stems.isEmpty())
            out_.warnings.add(juce::String(stems.size()) + " audio file(s) beside the MIDI file came in as stems, from the start.");
        return juce::Result::ok();
    }

private:
    juce::File file_;
    ImportedProject& out_;
};

} // namespace

juce::Result importMidiAndStems(const juce::File& file, ImportedProject& out)
{
    out.source = file;
    MidiStemsReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
