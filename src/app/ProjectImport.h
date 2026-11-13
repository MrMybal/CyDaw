#include <cydaw/base/Utilities.h>
#include <cydaw/base/File.h>
#include <cydaw/base/Text.h>
#pragma once


#include <vector>

namespace cydaw::app::project_import {

// What another application's project comes down to once read: a tempo,
// tracks with their mixer, the clips on them, the notes in the MIDI clips,
// the audio files the audio clips play — and everything that could not be
// carried over, named, so the person knows what to redo.
struct ImportedNote {
    int note { 60 };
    double startBeats {};   // from the start of the clip
    double lengthBeats { 1.0 };
    float velocity { 0.8f };
};

struct ImportedMidiClip {
    cydaw::base::Text name;
    double startBeats {};   // in the arrangement; a launcher clip sits at scene * 16
    double lengthBeats { 16.0 };
    bool looping {};
    bool muted {};
    std::uint32_t colour {};
    int launcherScene { -1 };  // -1: arrangement; otherwise the launcher slot
    std::vector<ImportedNote> notes;
};

struct ImportedAudioClip {
    cydaw::base::Text name;
    cydaw::base::File file;
    cydaw::base::Text originalPath;
    double startSeconds {};
    double durationSeconds {};        // on the timeline
    double sourceOffsetSeconds {};
    // The amount of source played over durationSeconds; 0 means the same,
    // any other value a stretch the original application applied.
    double sourceDurationSeconds {};
    double fadeInSeconds {};
    double fadeOutSeconds {};
    float gainDb {};
    bool looping {};
    bool muted {};
    std::uint32_t colour {};
    int launcherScene { -1 };
};

// One device of a track's chain, as the other application had it: a plug-in
// (its name, its maker, its format) or one of its own devices (its type),
// an instrument or an effect. Its settings, when they could be read, by the
// other application's own names.
struct ImportedDevice {
    cydaw::base::Text name;
    cydaw::base::Text vendor;
    cydaw::base::Text format;      // "VST3", "VST2", "CLAP", "AU" for a plug-in
    cydaw::base::Text type;        // the other application's own device ("Polysynth", "Operator")
    bool plugin {};
    bool instrument {};
    bool noteEffect {};
    std::vector<std::pair<cydaw::base::Text, double>> settings;
    // A sampler's sound, when the other application's sampler played one.
    cydaw::base::File sample;
};

struct ImportedTrack {
    cydaw::base::Text name;
    bool midi {};
    float gainDb {};
    float pan {};
    bool muted {};
    bool solo {};
    std::uint32_t colour {};
    // The devices and plugins the track carried, by name: not translated,
    // reported.
    cydaw::base::TextList devices;
    // The same in order, with what each one is.
    std::vector<ImportedDevice> chain;
    bool hadInstrument {};
    std::vector<ImportedAudioClip> audioClips;
    std::vector<ImportedMidiClip> midiClips;
};

struct ImportedProject {
    cydaw::base::File source;
    cydaw::base::Text name;
    cydaw::base::Text format;      // "dawproject", "ableton", "studio-one", "bitwig"
    cydaw::base::Text application; // what wrote the file, when it says
    double tempo { 120.0 };
    int numerator { 4 };
    int denominator { 4 };
    float masterGainDb {};
    std::vector<ImportedTrack> tracks;
    cydaw::base::TextList warnings;
    cydaw::base::TextList missingFiles;

    [[nodiscard]] double beatsToSeconds(const double beats) const
    {
        return beats * 60.0 / (tempo > 0.0 ? tempo : 120.0);
    }
    [[nodiscard]] double secondsToBeats(const double seconds) const
    {
        return seconds * (tempo > 0.0 ? tempo : 120.0) / 60.0;
    }
};

// Reads a project by its extension: .dawproject (Bitwig, Studio One, Cubase
// and others export it), .als (Ableton Live), .song (Studio One), .bwproject
// (Bitwig's own file), .rpp (Reaper), .flp (FL Studio), .ardour (Ardour),
// .mmp/.mmpz (LMMS), .aaf (exported by Logic, Pro Tools, Cubase, Nuendo,
// Studio One and others), .zpj (Zrythm), .mid (a MIDI file and the audio
// files beside it, as any application exports them). The result is what could be read;
// the warnings say what could not.
[[nodiscard]] cydaw::base::Result importProjectFile(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importReaperProject(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importFlStudioProject(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importArdourSession(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importLmmsProject(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importAafComposition(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importZrythmProject(const cydaw::base::File& file, ImportedProject& out);
// A standard MIDI file and the audio files beside it (stems): what any other
// application exports (Reason…).
[[nodiscard]] cydaw::base::Result importMidiAndStems(const cydaw::base::File& file, ImportedProject& out);
// Where audio made by an import goes (two mono files made one stereo file):
// the application's own folder for it; the system's temporary one until set.
void setImportedAudioFolder(const cydaw::base::File& folder);
[[nodiscard]] cydaw::base::File importedAudioFolder();
[[nodiscard]] cydaw::base::Result importDawProject(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importAbletonLiveSet(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importStudioOneSong(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::Result importBitwigProject(const cydaw::base::File& file, ImportedProject& out);
[[nodiscard]] cydaw::base::TextList supportedProjectExtensions();
[[nodiscard]] cydaw::base::Text supportedProjectWildcard();

// An audio file named by a project, found next to it when the path it
// carries no longer holds: the same name anywhere under the project's
// folder.
[[nodiscard]] cydaw::base::File resolveProjectAudioFile(const cydaw::base::File& projectFolder,
                                                  const cydaw::base::Text& path);

// A short, readable account of what was read, for a dialog or a log.
[[nodiscard]] cydaw::base::Text describeImportedProject(const ImportedProject& project);

} // namespace cydaw::app::project_import
