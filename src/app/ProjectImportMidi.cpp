#include <cydaw/base/Utilities.h>
#include <cydaw/integration/AudioFiles.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include "ProjectImport.h"
#include "ProjectImportShared.h"

#include <cydaw/midi/StandardMidiFile.h>


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
    MidiStemsReader(const cydaw::base::File& file, ImportedProject& out) : file_(file), out_(out) {}

    cydaw::base::Result read()
    {
        cyaudio::MidiFile midi;
        if (cydaw::midi::loadStandardFile(file_,midi).failed())
            return cydaw::base::Result::fail("Not a MIDI file: " + file_.getFileName());
        const auto perQuarter = static_cast<std::int16_t>(midi.division);
        if (perQuarter <= 0)
            return cydaw::base::Result::fail("A MIDI file counted in timecode (SMPTE) is not read: " + file_.getFileName());
        out_.format = "midi";
        out_.application = "MIDI file and stems";
        out_.name = file_.getFileNameWithoutExtension();
        // The tempo and signature: the first ones.
        auto tempoFound = false;
        auto tempoChanges = 0;
        for (const auto& track : midi.tracks)
            for (const auto& event : track) {
                if (const auto bpm = cydaw::midi::tempo(event)) {
                    ++tempoChanges;
                    if (!tempoFound) { out_.tempo = cydaw::base::limit(20.0,999.0,*bpm); tempoFound = true; }
                } else if (const auto meter = cydaw::midi::signature(event); meter && event.tick == 0) {
                    out_.numerator = cydaw::base::limit(1,32,int(meter->first));
                    out_.denominator = cydaw::base::limit(1,32,int(meter->second));
                }
            }
        if (tempoChanges > 1)
            out_.warnings.add("The tempo changes along the MIDI file: CyDAW keeps " + cydaw::base::Text(out_.tempo, 2) + " BPM.");
        for (std::size_t index = 0; index < midi.tracks.size(); ++index) {
            auto name = cydaw::midi::trackName(midi.tracks[index]);
            std::map<int, std::vector<ImportedNote>> byChannel;
            auto last = 0.0;
            for (const auto& note : cydaw::midi::pairedNotes(midi.tracks[index])) {
                if (!note.end) continue;
                const auto start = double(note.tick) / perQuarter;
                const auto end = double(*note.end) / perQuarter;
                byChannel[int(note.channel)].push_back({int(note.key),start,std::max(1.0e-3,end-start),float(note.velocity)/127.f});
                last=std::max(last,end);
            }
            for (auto& [channel, notes] : byChannel) {
                ImportedTrack track;
                track.midi = true;
                track.name = name.isNotEmpty() ? name : "MIDI " + cydaw::base::Text(index + 1);
                if (byChannel.size() > 1)
                    track.name << " (" << cydaw::base::Text(channel) << ")";
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
        cydaw::base::Array<cydaw::base::File> stems;
        file_.getParentDirectory().findChildFiles(stems, cydaw::base::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        stems.sort();
        if (stems.size() > 64) {
            out_.warnings.add(cydaw::base::Text(stems.size()) + " audio files beside the MIDI file: none was taken as a stem (too many).");
            stems.clear();
        }
        for (const auto& stem : stems) {
            ImportedTrack track;
            track.name = stem.getFileNameWithoutExtension();
            ImportedAudioClip clip;
            clip.name = track.name;
            clip.file = stem;
            clip.originalPath = stem.getFullPathName();
            cydaw::audio::AudioFormatManager formats;
            cydaw::integration::registerCyAudioCoreReaders(formats);
            if (std::unique_ptr<cydaw::audio::AudioFormatReader> reader(formats.createReaderFor(stem)); reader != nullptr && reader->sampleRate > 0.0)
                clip.durationSeconds = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
            if (clip.durationSeconds <= 0.0)
                continue;
            track.audioClips.push_back(std::move(clip));
            out_.tracks.push_back(std::move(track));
        }
        if (!stems.isEmpty())
            out_.warnings.add(cydaw::base::Text(stems.size()) + " audio file(s) beside the MIDI file came in as stems, from the start.");
        return cydaw::base::Result::ok();
    }

private:
    cydaw::base::File file_;
    ImportedProject& out_;
};

} // namespace

cydaw::base::Result importMidiAndStems(const cydaw::base::File& file, ImportedProject& out)
{
    out.source = file;
    MidiStemsReader reader(file, out);
    return reader.read();
}

} // namespace cydaw::app::project_import
