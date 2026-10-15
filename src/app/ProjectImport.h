#pragma once

#include <juce_core/juce_core.h>

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
    juce::String name;
    double startBeats {};   // in the arrangement; a launcher clip sits at scene * 16
    double lengthBeats { 16.0 };
    bool looping {};
    bool muted {};
    juce::uint32 colour {};
    int launcherScene { -1 };  // -1: arrangement; otherwise the launcher slot
    std::vector<ImportedNote> notes;
};

struct ImportedAudioClip {
    juce::String name;
    juce::File file;
    juce::String originalPath;
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
    juce::uint32 colour {};
    int launcherScene { -1 };
};

// One device of a track's chain, as the other application had it: a plug-in
// (its name, its maker, its format) or one of its own devices (its type),
// an instrument or an effect. Its settings, when they could be read, by the
// other application's own names.
struct ImportedDevice {
    juce::String name;
    juce::String vendor;
    juce::String format;      // "VST3", "VST2", "CLAP", "AU" for a plug-in
    juce::String type;        // the other application's own device ("Polysynth", "Operator")
    bool plugin {};
    bool instrument {};
    bool noteEffect {};
    std::vector<std::pair<juce::String, double>> settings;
};

struct ImportedTrack {
    juce::String name;
    bool midi {};
    float gainDb {};
    float pan {};
    bool muted {};
    bool solo {};
    juce::uint32 colour {};
    // The devices and plugins the track carried, by name: not translated,
    // reported.
    juce::StringArray devices;
    // The same in order, with what each one is.
    std::vector<ImportedDevice> chain;
    bool hadInstrument {};
    std::vector<ImportedAudioClip> audioClips;
    std::vector<ImportedMidiClip> midiClips;
};

struct ImportedProject {
    juce::File source;
    juce::String name;
    juce::String format;      // "dawproject", "ableton", "studio-one", "bitwig"
    juce::String application; // what wrote the file, when it says
    double tempo { 120.0 };
    int numerator { 4 };
    int denominator { 4 };
    float masterGainDb {};
    std::vector<ImportedTrack> tracks;
    juce::StringArray warnings;
    juce::StringArray missingFiles;

    [[nodiscard]] double beatsToSeconds(const double beats) const
    {
        return beats * 60.0 / (tempo > 0.0 ? tempo : 120.0);
    }
    [[nodiscard]] double secondsToBeats(const double seconds) const
    {
        return seconds * (tempo > 0.0 ? tempo : 120.0) / 60.0;
    }
};

// Reads a project by its extension: .dawproject (Bitwig, Studio One and
// others export it), .als (Ableton Live), .song (Studio One), .bwproject
// (Bitwig's own file). The result is what could be read; the warnings say
// what could not.
[[nodiscard]] juce::Result importProjectFile(const juce::File& file, ImportedProject& out);
[[nodiscard]] juce::Result importDawProject(const juce::File& file, ImportedProject& out);
[[nodiscard]] juce::Result importAbletonLiveSet(const juce::File& file, ImportedProject& out);
[[nodiscard]] juce::Result importStudioOneSong(const juce::File& file, ImportedProject& out);
[[nodiscard]] juce::Result importBitwigProject(const juce::File& file, ImportedProject& out);
[[nodiscard]] juce::StringArray supportedProjectExtensions();
[[nodiscard]] juce::String supportedProjectWildcard();

// An audio file named by a project, found next to it when the path it
// carries no longer holds: the same name anywhere under the project's
// folder.
[[nodiscard]] juce::File resolveProjectAudioFile(const juce::File& projectFolder,
                                                  const juce::String& path);

// A short, readable account of what was read, for a dialog or a log.
[[nodiscard]] juce::String describeImportedProject(const ImportedProject& project);

} // namespace cydaw::app::project_import
