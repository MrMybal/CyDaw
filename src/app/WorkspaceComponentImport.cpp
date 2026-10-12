// Another application's project brought into this one: what ProjectImport
// reads, placed on tracks the way the remote commands would place it.

#include "WorkspaceComponent.h"
#include "Localization.h"
#include "ProjectImport.h"
#include "TrackLaneComponent.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace cydaw::app {

void WorkspaceComponent::startEmptyProject(const bool withInitialMidiTrack)
{
    // What a load does before it reads a file, and then the one MIDI
    // track the application starts with.
    if (audio_.hasRecordingSession())
        finishAudioRecording(false);
    if (audio_.isMidiRecording())
        audio_.stopMidiRecording(false);
    audio_.stop();
    audio_.stopLauncher();
    (void) projectAutosavePool_.removeAllJobs(true, 10'000);
    projectAutosaveInProgress_.store(false, std::memory_order_release);
    audio_.stopSamplePreview();
    audio_.clearTracks();
    audio_.clearMidiTracks();
    launcherActions_.clear();
    midiNodeGraphStates_.clear();
    trackModulationStates_.clear();
    clearTrackLanes();
    audio_.setTempo(120.0);
    tempoSlider_.setValue(120.0, juce::dontSendNotification);
    pianoRoll_.setTempo(120.0);
    audio_.resetLoopRange();
    loopButton_.setToggleState(false, juce::dontSendNotification);
    audio_.setLooping(false);
    audio_.setPosition(0.0);
    currentProjectFile_ = {};
    projectDirty_ = false;
    projectLabel_.setText("CyDAW", juce::dontSendNotification);
    if (withInitialMidiTrack)
        addEmptyMidiTrack();
    setEditorMode(1);
    refreshClipLauncher();
    refreshMixer();
    refreshTimelineRanges();
    projectDirty_ = false;
}

juce::Result WorkspaceComponent::importExternalProject(const juce::File& file, juce::var& report)
{
    using namespace project_import;
    if (audio_.isRecording() || audio_.isMidiRecording())
        return juce::Result::fail("a recording is running");
    ImportedProject project;
    if (const auto read = importProjectFile(file, project); read.failed())
        return read;

    startEmptyProject(false);
    audio_.setTempo(project.tempo);
    tempoSlider_.setValue(project.tempo, juce::dontSendNotification);
    pianoRoll_.setTempo(project.tempo);

    juce::StringArray instrumentTracks;
    auto importedAudio = 0;
    auto importedMidi = 0;
    auto importedClips = 0;
    auto importedNotes = 0;
    auto launcherOverflowReported = false;

    const auto addAudioTrack = [&](const juce::String& name, const ImportedTrack& source) {
        std::size_t index {};
        if (const auto created = audio_.addEmptyAudioTrack(&index); created.failed()) {
            project.warnings.add("Track \"" + name + "\" could not be created: " + created.getErrorMessage());
            return;
        }
        addTrackLane(index);
        if (index < trackPresentations_.size()) {
            trackPresentations_[index].name = name;
            if (source.colour != 0)
                trackPresentations_[index].colour = juce::Colour(source.colour);
            if (index < trackLanes_.size())
                trackLanes_[index]->setPresentation(name, trackPresentations_[index].colour);
        }
        audio_.setTrackMixer(index, source.gainDb, source.pan, source.muted);
        audio_.setTrackSolo(index, source.solo);
        auto mutedClips = 0;
        std::set<int> filledScenes;
        for (const auto& clip : source.audioClips) {
            if (clip.file == juce::File {} || !clip.file.existsAsFile())
                continue;
            if (clip.launcherScene >= 0 && !filledScenes.insert(clip.launcherScene).second)
                continue;
            const auto colour = clip.colour != 0 ? clip.colour : 0xffff9b2fu;
            const auto duration = std::max(0.01, clip.durationSeconds);
            if (clip.launcherScene >= 0) {
                if (clip.launcherScene >= AudioTrackController::maximumLauncherScenes) {
                    if (!launcherOverflowReported)
                        project.warnings.add("Launcher clips beyond scene "
                                             + juce::String(AudioTrackController::maximumLauncherScenes)
                                             + " were left out.");
                    launcherOverflowReported = true;
                    continue;
                }
                if (audio_.setLauncherClip(index, clip.launcherScene, clip.name, clip.sourceOffsetSeconds,
                                           duration, clip.looping, colour, clip.file))
                    ++importedClips;
                else
                    project.warnings.add("Launcher clip \"" + clip.name + "\" was refused on \"" + name + "\".");
                continue;
            }
            const auto clipId = audio_.addAudioClip(index, std::max(0.0, clip.startSeconds),
                                                    std::max(0.0, clip.sourceOffsetSeconds), duration,
                                                    clip.name, colour, clip.file,
                                                    std::max(0.0, clip.fadeInSeconds),
                                                    std::max(0.0, clip.fadeOutSeconds));
            if (clipId == 0) {
                project.warnings.add("Clip \"" + clip.name + "\" could not be placed on \"" + name + "\".");
                continue;
            }
            ++importedClips;
            if (clip.sourceDurationSeconds > 0.0
                && std::abs(clip.sourceDurationSeconds - duration) > 1.0e-3)
                (void) audio_.updateAudioClip(index, clipId, std::max(0.0, clip.startSeconds),
                                              std::max(0.0, clip.sourceOffsetSeconds), duration,
                                              -1.0, -1.0, clip.sourceDurationSeconds);
            if (clip.muted)
                ++mutedClips;
        }
        if (mutedClips > 0)
            project.warnings.add("\"" + name + "\": " + juce::String(mutedClips)
                                 + " clip(s) were muted in the original and play here.");
        ++importedAudio;
    };

    const auto addMidiTrack = [&](const juce::String& name, const ImportedTrack& source) {
        const auto id = audio_.addMidiTrack(name);
        if (id == 0) {
            project.warnings.add("MIDI track \"" + name + "\" could not be created.");
            return;
        }
        for (const auto& track : audio_.midiTracks())
            if (track.id == id)
                for (const auto& clip : track.clips)
                    (void) audio_.removeMidiClip(id, clip.id);
        audio_.setMidiTrackMixer(id, source.gainDb, source.pan, source.muted, source.solo);
        auto notes = 0;
        auto mutedClips = 0;
        for (const auto& clip : source.midiClips) {
            if (clip.launcherScene >= AudioTrackController::maximumLauncherScenes) {
                if (!launcherOverflowReported)
                    project.warnings.add("Launcher clips beyond scene "
                                         + juce::String(AudioTrackController::maximumLauncherScenes)
                                         + " were left out.");
                launcherOverflowReported = true;
                continue;
            }
            const auto colour = clip.colour != 0 ? clip.colour : 0xff4fd3aeu;
            const auto clipId = audio_.addMidiClip(id, clip.name.isNotEmpty() ? clip.name : tr("midi.clip"),
                                                   std::max(0.0, clip.startBeats),
                                                   std::max(0.25, clip.lengthBeats), clip.looping, colour);
            if (clipId == 0) {
                project.warnings.add("MIDI clip \"" + clip.name + "\" could not be placed on \"" + name + "\".");
                continue;
            }
            ++importedClips;
            for (const auto& note : clip.notes)
                if (audio_.addMidiNote(id, clipId, note.note, std::max(0.0, note.startBeats),
                                       std::max(0.01, note.lengthBeats),
                                       juce::jlimit(0.01f, 1.0f, note.velocity)) != 0)
                    ++notes;
            if (clip.muted)
                ++mutedClips;
        }
        if (mutedClips > 0)
            project.warnings.add("\"" + name + "\": " + juce::String(mutedClips)
                                 + " clip(s) were muted in the original and play here.");
        importedNotes += notes;
        ++importedMidi;
        if (source.hadInstrument || notes > 0)
            instrumentTracks.add(name);
    };

    for (const auto& track : project.tracks) {
        const auto hasNotes = std::any_of(track.midiClips.begin(), track.midiClips.end(),
                                          [](const auto& clip) { return !clip.notes.empty(); });
        if (track.midi) {
            addMidiTrack(track.name, track);
            if (!track.audioClips.empty()) {
                auto audioPart = track;
                audioPart.midi = false;
                audioPart.midiClips.clear();
                addAudioTrack(track.name + " (audio)", audioPart);
            }
        } else {
            addAudioTrack(track.name, track);
            if (hasNotes) {
                auto midiPart = track;
                midiPart.midi = true;
                midiPart.audioClips.clear();
                addMidiTrack(track.name + " (MIDI)", midiPart);
            }
        }
    }
    if (audio_.trackCount() == 0 && audio_.midiTracks().empty())
        addEmptyMidiTrack();

    // A sound source on the tracks that had one: the project cannot carry
    // the original instrument, and a silent MIDI track helps nobody.
    for (const auto& name : instrumentTracks) {
        auto placed = false;
        for (const auto* key : { "synth-keys", "table-synth", "synth-pluck" }) {
            auto* object = new juce::DynamicObject();
            object->setProperty("cmd", "insert_device");
            object->setProperty("track", name);
            object->setProperty("key", key);
            const auto response = handleRemoteCommand(juce::var(object));
            if (static_cast<bool>(response.getProperty("ok", false))) {
                placed = true;
                break;
            }
        }
        if (!placed)
            project.warnings.add("No instrument could be placed on \"" + name + "\".");
    }
    for (const auto& track : project.tracks)
        if (!track.devices.isEmpty())
            project.warnings.add("\"" + track.name + "\": devices not carried over: "
                                 + track.devices.joinIntoString(", "));
    if (std::abs(project.masterGainDb) > 0.01f)
        applyMasterGain(project.masterGainDb);

    selectedTrack_.reset();
    selectedMidiClipId_ = 0;
    if (audio_.trackCount() > 0) {
        selectTrack(0);
    } else if (const auto tracks = audio_.midiTracks(); !tracks.empty()) {
        selectedMidiTrackId_ = tracks.front().id;
        (void) audio_.setLiveMidiTrack(tracks.front().id);
        restoreNodeGraphForMidiTrack(tracks.front().id);
        refreshMidiTrackMixerControls();
        refreshNodeDeviceSurface();
    }
    setEditorMode(1);
    refreshMidiEditor();
    refreshClipLauncher();
    refreshMixer();
    refreshTimelineRanges();
    updateTrackDescription();
    currentProjectFile_ = {};
    markProjectDirty();

    auto* object = new juce::DynamicObject();
    object->setProperty("format", project.format);
    object->setProperty("application", project.application);
    object->setProperty("name", project.name);
    object->setProperty("source", file.getFullPathName());
    object->setProperty("tempo", project.tempo);
    object->setProperty("numerator", project.numerator);
    object->setProperty("denominator", project.denominator);
    object->setProperty("masterGainDb", project.masterGainDb);
    juce::Array<juce::var> tracks;
    for (const auto& track : project.tracks) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("name", track.name);
        entry->setProperty("kind", track.midi ? "midi" : "audio");
        entry->setProperty("clips", static_cast<int>(track.midiClips.size() + track.audioClips.size()));
        auto notes = 0;
        for (const auto& clip : track.midiClips)
            notes += static_cast<int>(clip.notes.size());
        entry->setProperty("notes", notes);
        entry->setProperty("gainDb", track.gainDb);
        entry->setProperty("pan", track.pan);
        entry->setProperty("muted", track.muted);
        entry->setProperty("solo", track.solo);
        juce::Array<juce::var> devices;
        for (const auto& device : track.devices)
            devices.add(device);
        entry->setProperty("devices", devices);
        tracks.add(juce::var(entry));
    }
    object->setProperty("tracks", tracks);
    juce::Array<juce::var> warnings;
    for (const auto& warning : project.warnings)
        warnings.add(warning);
    object->setProperty("warnings", warnings);
    juce::Array<juce::var> missing;
    for (const auto& path : project.missingFiles)
        missing.add(path);
    object->setProperty("missingFiles", missing);
    auto* imported = new juce::DynamicObject();
    imported->setProperty("audioTracks", importedAudio);
    imported->setProperty("midiTracks", importedMidi);
    imported->setProperty("clips", importedClips);
    imported->setProperty("notes", importedNotes);
    object->setProperty("imported", juce::var(imported));
    object->setProperty("summary", describeImportedProject(project));
    report = juce::var(object);
    return juce::Result::ok();
}

void WorkspaceComponent::chooseProjectImportFile()
{
    chooser_ = std::make_unique<juce::FileChooser>(tr("dialog.importProject"), juce::File {},
                                                   project_import::supportedProjectWildcard());
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (!file.existsAsFile())
                return;
            juce::var report;
            const auto result = importExternalProject(file, report);
            if (result.failed()) {
                showError(tr("error.importTitle"), result.getErrorMessage());
                return;
            }
            const auto* imported = report.getProperty("imported", juce::var()).getDynamicObject();
            const auto counts = imported != nullptr
                ? tr("import.counts")
                      .replace("{audio}", imported->getProperty("audioTracks").toString())
                      .replace("{midi}", imported->getProperty("midiTracks").toString())
                      .replace("{clips}", imported->getProperty("clips").toString())
                      .replace("{notes}", imported->getProperty("notes").toString())
                : juce::String();
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, tr("import.title"),
                counts + "\n\n" + report.getProperty("summary", juce::var()).toString());
        });
}

} // namespace cydaw::app
