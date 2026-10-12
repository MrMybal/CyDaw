#include "PianoRollComponent.h"
#include "PaintProfile.h"
#include "Localization.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace cydaw::app {
namespace {
constexpr float keyboardWidth = 54.0f;
constexpr float toolbarHeight = 96.0f;
constexpr float rulerHeight = 42.0f;
constexpr float velocityLaneHeight = 76.0f;
constexpr float velocityHeaderHeight = 28.0f;
constexpr int midiLowestNote = 0;
constexpr int midiHighestNote = 127;

bool sameNoteData(
    const std::vector<AudioTrackController::MidiNote>& first,
    const std::vector<AudioTrackController::MidiNote>& second)
{
    if (first.size() != second.size())
        return false;
    constexpr double tolerance = 0.0000001;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& left = first[index];
        const auto& right = second[index];
        if (left.id != right.id || left.noteNumber != right.noteNumber
            || std::abs(left.startBeats - right.startBeats) > tolerance
            || std::abs(left.durationBeats - right.durationBeats) > tolerance
            || std::abs(left.velocity - right.velocity) > 0.000001f)
            return false;
    }
    return true;
}
}

PianoRollComponent::PianoRollComponent()
{
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(true);
    for (auto* component : { static_cast<juce::Component*>(&trackSelector_),
                             static_cast<juce::Component*>(&clipSelector_),
                             static_cast<juce::Component*>(&addTrackButton_),
                             static_cast<juce::Component*>(&addClipButton_),
                             static_cast<juce::Component*>(&gainSlider_),
                             static_cast<juce::Component*>(&panSlider_),
                             static_cast<juce::Component*>(&muteButton_),
                             static_cast<juce::Component*>(&soloButton_),
                             static_cast<juce::Component*>(&recordQuantizationSelector_),
                             static_cast<juce::Component*>(&editSnapSelector_),
                             static_cast<juce::Component*>(&editSnapTypeSelector_),
                             static_cast<juce::Component*>(&overdubButton_),
                             static_cast<juce::Component*>(&clipLoopButton_),
                             static_cast<juce::Component*>(&playButton_),
                             static_cast<juce::Component*>(&stopButton_),
                             static_cast<juce::Component*>(&returnButton_),
                             static_cast<juce::Component*>(&spacePlayModeSelector_),
                             static_cast<juce::Component*>(&durationLabel_),
                             static_cast<juce::Component*>(&startLabel_),
                             static_cast<juce::Component*>(&endLabel_),
                             static_cast<juce::Component*>(&loopStartLabel_),
                             static_cast<juce::Component*>(&loopEndLabel_),
                             static_cast<juce::Component*>(&durationEditor_),
                             static_cast<juce::Component*>(&startEditor_),
                             static_cast<juce::Component*>(&endEditor_),
                             static_cast<juce::Component*>(&loopStartEditor_),
                             static_cast<juce::Component*>(&loopEndEditor_),
                             static_cast<juce::Component*>(&importMidiButton_),
                             static_cast<juce::Component*>(&exportMidiButton_),
                             static_cast<juce::Component*>(&undoButton_),
                             static_cast<juce::Component*>(&redoButton_),
                             static_cast<juce::Component*>(&noteDataSelector_),
                             static_cast<juce::Component*>(&recordingStatusLabel_) })
        addAndMakeVisible(*component);

    for (auto* label : { &durationLabel_, &startLabel_, &endLabel_,
                         &loopStartLabel_, &loopEndLabel_ }) {
        label->setColour(juce::Label::textColourId,
                         juce::Colour(0xffaeb6c5));
        label->setFont(juce::FontOptions(9.0f, juce::Font::bold));
        label->setJustificationType(juce::Justification::centredRight);
    }
    for (auto* editor : { &durationEditor_, &startEditor_, &endEditor_,
                          &loopStartEditor_, &loopEndEditor_ }) {
        editor->setJustification(juce::Justification::centred);
        editor->setInputRestrictions(12, "0123456789.,-");
        editor->setSelectAllWhenFocused(true);
        editor->onReturnKey = [this] { applyClipBoundsFromEditors(); };
        editor->onFocusLost = [this] { applyClipBoundsFromEditors(); };
    }

    gainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    gainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
    gainSlider_.setRange(-60.0, 12.0, 0.1);
    gainSlider_.setDoubleClickReturnValue(true, 0.0);
    gainSlider_.setTextValueSuffix(" dB");
    panSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    panSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
    panSlider_.setRange(-1.0, 1.0, 0.01);
    panSlider_.setDoubleClickReturnValue(true, 0.0);

    trackSelector_.onChange = [this] {
        const auto index = trackSelector_.getSelectedItemIndex();
        if (!updatingContext_ && index >= 0
            && index < static_cast<int>(trackItems_.size())
            && trackSelectionHandler_)
            trackSelectionHandler_(trackItems_[static_cast<std::size_t>(index)].id);
    };
    clipSelector_.onChange = [this] {
        const auto index = clipSelector_.getSelectedItemIndex();
        if (!updatingContext_ && index >= 0
            && index < static_cast<int>(clipItems_.size())
            && clipSelectionHandler_)
            clipSelectionHandler_(clipItems_[static_cast<std::size_t>(index)].id);
    };
    addTrackButton_.onClick = [this] {
        if (addTrackHandler_)
            addTrackHandler_();
    };
    addClipButton_.onClick = [this] {
        if (addClipHandler_)
            addClipHandler_();
    };
    const auto publishMixer = [this] {
        if (!updatingContext_ && mixerHandler_)
            mixerHandler_(static_cast<float>(gainSlider_.getValue()),
                          static_cast<float>(panSlider_.getValue()),
                          muteButton_.getToggleState(), soloButton_.getToggleState());
    };
    gainSlider_.onValueChange = publishMixer;
    panSlider_.onValueChange = publishMixer;
    muteButton_.onClick = publishMixer;
    soloButton_.onClick = publishMixer;
    recordQuantizationSelector_.addItem("REC FREE", 1);
    recordQuantizationSelector_.addItem("REC 1/4", 2);
    recordQuantizationSelector_.addItem("REC 1/8", 3);
    recordQuantizationSelector_.addItem("REC 1/16", 4);
    recordQuantizationSelector_.addItem("REC 1/32", 5);
    recordQuantizationSelector_.setSelectedId(4, juce::dontSendNotification);
    editSnapSelector_.setSelectedId(4, juce::dontSendNotification);
    editSnapTypeSelector_.setSelectedId(1,
                                        juce::dontSendNotification);
    editSnapSelector_.onChange = [this] { updateSnapControls(); };
    editSnapTypeSelector_.onChange = [this] { updateSnapControls(); };
    noteDataSelector_.onChange = [this] { repaint(); };
    overdubButton_.setToggleState(true, juce::dontSendNotification);
    clipLoopButton_.setClickingTogglesState(true);
    clipLoopButton_.onClick = [this] {
        clipLooping_ = clipLoopButton_.getToggleState();
        refreshClipBoundsControls();
        publishClipBounds();
        repaint();
    };
    playButton_.onClick = [this] { triggerPlayPause(); };
    stopButton_.onClick = [this] {
        if (transportHandler_)
            transportHandler_(TransportCommand::stop);
    };
    returnButton_.onClick = [this] {
        if (transportHandler_)
            transportHandler_(TransportCommand::returnToStart);
    };
    spacePlayModeSelector_.setSelectedId(1, juce::dontSendNotification);
    importMidiButton_.onClick = [this] {
        if (importMidiHandler_ && !recording_)
            importMidiHandler_();
    };
    exportMidiButton_.onClick = [this] {
        if (exportMidiHandler_ && !recording_)
            exportMidiHandler_();
    };
    undoButton_.onClick = [this] {
        if (undoHandler_ && !recording_)
            undoHandler_();
    };
    redoButton_.onClick = [this] {
        if (redoHandler_ && !recording_)
            redoHandler_();
    };
    recordingStatusLabel_.setColour(juce::Label::textColourId,
                                    juce::Colour(0xffaeb6c5));
    recordingStatusLabel_.setJustificationType(
        juce::Justification::centredLeft);
    recordingStatusLabel_.setFont(juce::FontOptions(10.5f));
}

void PianoRollComponent::setNotes(std::vector<AudioTrackController::MidiNote> notes)
{
    notes_ = std::move(notes);
    normaliseSelection();
    auto requiredBeats = std::max(4.0, clipLengthBeats_);
    for (const auto& note : notes_)
        requiredBeats = std::max(requiredBeats, note.startBeats + note.durationBeats);
    contentBeats_ = std::ceil(requiredBeats / 4.0) * 4.0;
    visibleBeats_ = juce::jlimit(1.0, std::max(4.0, contentBeats_),
                                 visibleBeats_);
    visibleStartBeat_ = juce::jlimit(
        0.0, std::max(0.0, contentBeats_ + 4.0 - visibleBeats_),
        visibleStartBeat_);
    exportMidiButton_.setEnabled(!recording_ && currentTrackId_ != 0
                                 && currentClipId_ != 0 && !notes_.empty());
    updateStatusLabel();
    repaint();
}

void PianoRollComponent::setContext(std::vector<ContextItem> tracks,
                                    const std::uint64_t selectedTrackId,
                                    std::vector<ContextItem> clips,
                                    const std::uint64_t selectedClipId,
                                    const double clipStartBeats,
                                    const double clipLengthBeats,
                                    const double playStartBeats,
                                    const double playEndBeats,
                                    const double loopStartBeats,
                                    const double loopEndBeats,
                                    const bool clipLooping,
                                    const float gainDb,
                                    const float pan,
                                    const bool muted,
                                    const bool solo)
{
    updatingContext_ = true;
    const auto clipChanged = selectedTrackId != currentTrackId_
                             || selectedClipId != currentClipId_;
    if (clipChanged) {
        selectedNoteIds_.clear();
        gestureMode_ = GestureMode::none;
        pendingGridClick_ = false;
    }
    currentTrackId_ = selectedTrackId;
    currentClipId_ = selectedClipId;
    trackItems_ = std::move(tracks);
    clipItems_ = std::move(clips);
    clipStartBeats_ = std::max(0.0, clipStartBeats);
    clipLengthBeats_ = std::max(0.25, clipLengthBeats);
    playStartBeats_ = juce::jlimit(
        0.0, std::max(0.0, clipLengthBeats_ - 0.03125),
        playStartBeats);
    playEndBeats_ = juce::jlimit(
        playStartBeats_ + 0.03125, clipLengthBeats_,
        playEndBeats > playStartBeats_ ? playEndBeats
                                       : clipLengthBeats_);
    loopStartBeats_ = juce::jlimit(
        playStartBeats_, std::max(playStartBeats_,
                                  playEndBeats_ - 0.03125),
        loopStartBeats);
    loopEndBeats_ = juce::jlimit(
        loopStartBeats_ + 0.03125, playEndBeats_,
        loopEndBeats > loopStartBeats_ ? loopEndBeats
                                       : playEndBeats_);
    clipLooping_ = clipLooping;
    clipLoopButton_.setToggleState(clipLooping_,
                                   juce::dontSendNotification);
    if (clipChanged) {
        contentBeats_ = std::max(4.0,
                                 std::ceil(clipLengthBeats_ / 4.0) * 4.0);
        visibleStartBeat_ = 0.0;
        visibleBeats_ = contentBeats_;
    }
    trackSelector_.clear(juce::dontSendNotification);
    clipSelector_.clear(juce::dontSendNotification);
    auto selectedTrackIndex = -1;
    for (std::size_t index = 0; index < trackItems_.size(); ++index) {
        trackSelector_.addItem(trackItems_[index].name, static_cast<int>(index) + 1);
        if (trackItems_[index].id == selectedTrackId)
            selectedTrackIndex = static_cast<int>(index);
    }
    auto selectedClipIndex = -1;
    for (std::size_t index = 0; index < clipItems_.size(); ++index) {
        clipSelector_.addItem(clipItems_[index].name, static_cast<int>(index) + 1);
        if (clipItems_[index].id == selectedClipId)
            selectedClipIndex = static_cast<int>(index);
    }
    trackSelector_.setSelectedItemIndex(selectedTrackIndex, juce::dontSendNotification);
    clipSelector_.setSelectedItemIndex(selectedClipIndex, juce::dontSendNotification);
    clipSelector_.setEnabled(!clipItems_.empty());
    addClipButton_.setEnabled(selectedTrackIndex >= 0);
    exportMidiButton_.setEnabled(!recording_ && selectedTrackId != 0
                                 && selectedClipId != 0 && !notes_.empty());
    gainSlider_.setValue(gainDb, juce::dontSendNotification);
    panSlider_.setValue(pan, juce::dontSendNotification);
    muteButton_.setToggleState(muted, juce::dontSendNotification);
    soloButton_.setToggleState(solo, juce::dontSendNotification);
    updatingContext_ = false;
    refreshClipBoundsControls();
}

double PianoRollComponent::visibleStartBeat() const noexcept
{
    return visibleStartBeat_;
}

double PianoRollComponent::visibleEndBeat() const noexcept
{
    return visibleStartBeat_ + visibleBeats_;
}

void PianoRollComponent::setTempo(const double beatsPerMinute)
{
    tempoBpm_ = juce::jlimit(20.0, 400.0, beatsPerMinute);
    repaint();
}

void PianoRollComponent::setPlayhead(const double seconds)
{
    const auto next = std::max(0.0, seconds);
    if (std::abs(playheadSeconds_ - next) < 1.0e-9)
        return;
    // Repainting the roll redraws every note, the keyboard and the velocity
    // lane. Thirty times a second, for a two-pixel line.
    const auto column = [this](const double time) {
        const auto grid = gridBounds();
        const auto beats = time * tempoBpm_ / 60.0;
        const auto span = std::max(1.0e-9, visibleBeats_);
        const auto x = grid.getX()
            + static_cast<float>((beats - visibleStartBeat_) / span)
                  * grid.getWidth();
        if (x < grid.getX() || x > grid.getRight())
            return juce::Rectangle<int> {};
        auto strip = getLocalBounds();
        strip.setX(juce::roundToInt(x) - 3);
        strip.setWidth(8);
        strip.setY(juce::roundToInt(toolbarHeight));
        strip.setHeight(getHeight() - juce::roundToInt(toolbarHeight));
        return strip.getIntersection(getLocalBounds());
    };
    const auto previous = column(playheadSeconds_);
    playheadSeconds_ = next;
    const auto moved = column(playheadSeconds_);
    if (previous.isEmpty() && moved.isEmpty())
        return;
    if (!previous.isEmpty() && !moved.isEmpty()
        && std::abs(previous.getX() - moved.getX()) <= previous.getWidth() * 4) {
        repaint(previous.getUnion(moved));
        return;
    }
    if (!previous.isEmpty())
        repaint(previous);
    if (!moved.isEmpty())
        repaint(moved);
}

void PianoRollComponent::setTransportState(const bool playing)
{
    previewPlaying_ = playing;
    playButton_.setButtonText(juce::String::fromUTF8(
        playing ? "\xE2\x9D\x9A\xE2\x9D\x9A" : "\xE2\x96\xB6"));
    playButton_.setColour(
        juce::TextButton::buttonColourId,
        playing ? juce::Colour(0xffff8a00) : juce::Colour(0xff303640));
    playButton_.setTooltip(playing ? tr("transport.pause")
                                   : tr("transport.play"));
}

void PianoRollComponent::triggerPlayPause()
{
    if (!transportHandler_)
        return;
    if (!previewPlaying_ && spacePlayModeSelector_.getSelectedId() == 2)
        transportHandler_(TransportCommand::returnToStart);
    transportHandler_(TransportCommand::play);
}

void PianoRollComponent::setRecordingState(const bool recording,
                                           const std::size_t recordedNotes,
                                           const int droppedEvents)
{
    recording_ = recording;
    recordedNoteCount_ = recordedNotes;
    droppedRecordingEvents_ = std::max(0, droppedEvents);
    trackSelector_.setEnabled(!recording_);
    clipSelector_.setEnabled(!recording_ && !clipItems_.empty());
    addTrackButton_.setEnabled(!recording_);
    addClipButton_.setEnabled(!recording_ && !trackItems_.empty());
    recordQuantizationSelector_.setEnabled(!recording_);
    overdubButton_.setEnabled(!recording_);
    importMidiButton_.setEnabled(!recording_);
    exportMidiButton_.setEnabled(!recording_ && currentTrackId_ != 0
                                 && currentClipId_ != 0 && !notes_.empty());
    updateStatusLabel();
    undoButton_.setEnabled(canUndo_ && !recording_);
    redoButton_.setEnabled(canRedo_ && !recording_);
    refreshClipBoundsControls();
    repaint();
}

void PianoRollComponent::setHistoryState(const bool canUndo,
                                         const bool canRedo)
{
    canUndo_ = canUndo;
    canRedo_ = canRedo;
    undoButton_.setEnabled(canUndo && !recording_);
    redoButton_.setEnabled(canRedo && !recording_);
}

bool PianoRollComponent::recordOverdubEnabled() const noexcept
{
    return overdubButton_.getToggleState();
}

AudioTrackController::MidiRecordQuantization
PianoRollComponent::recordQuantization() const noexcept
{
    return static_cast<AudioTrackController::MidiRecordQuantization>(
        juce::jlimit(0, 4, recordQuantizationSelector_.getSelectedId() - 1));
}

std::size_t PianoRollComponent::selectedNoteCount() const noexcept
{
    return selectedNoteIds_.size();
}

void PianoRollComponent::selectAllNotes()
{
    if (recording_)
        return;
    selectedNoteIds_.clear();
    selectedNoteIds_.reserve(notes_.size());
    for (const auto& note : notes_)
        selectedNoteIds_.push_back(note.id);
    updateStatusLabel();
    repaint();
}

bool PianoRollComponent::copySelectedNotes()
{
    if (recording_ || selectedNoteIds_.empty())
        return false;
    noteClipboard_.clear();
    for (const auto& note : notes_)
        if (isSelected(note.id)) {
            auto copy = note;
            copy.id = 0;
            noteClipboard_.push_back(copy);
        }
    std::sort(noteClipboard_.begin(), noteClipboard_.end(),
              [](const auto& first, const auto& second) {
                  if (first.startBeats != second.startBeats)
                      return first.startBeats < second.startBeats;
                  return first.noteNumber < second.noteNumber;
              });
    updateStatusLabel();
    return !noteClipboard_.empty();
}

bool PianoRollComponent::cutSelectedNotes()
{
    return copySelectedNotes() && deleteSelectedNotes();
}

bool PianoRollComponent::pasteNotes()
{
    if (recording_ || noteClipboard_.empty())
        return false;
    const auto localPlayhead = playheadSeconds_ * tempoBpm_ / 60.0
                               - clipStartBeats_;
    return insertClipboardAt(snapBeat(std::max(0.0, localPlayhead)));
}

bool PianoRollComponent::duplicateSelectedNotes()
{
    if (recording_ || selectedNoteIds_.empty())
        return false;
    auto latestEnd = 0.0;
    for (const auto& note : notes_)
        if (isSelected(note.id))
            latestEnd = std::max(latestEnd,
                                 note.startBeats + note.durationBeats);
    if (!copySelectedNotes())
        return false;
    return insertClipboardAt(snapBeat(latestEnd + 0.25));
}

bool PianoRollComponent::deleteSelectedNotes()
{
    if (recording_ || selectedNoteIds_.empty())
        return false;
    auto desired = notes_;
    std::erase_if(desired, [this](const auto& note) {
        return isSelected(note.id);
    });
    if (!applyNotes(std::move(desired)))
        return false;
    selectedNoteIds_.clear();
    updateStatusLabel();
    repaint();
    return true;
}

bool PianoRollComponent::isSelected(const std::uint64_t noteId) const noexcept
{
    return std::find(selectedNoteIds_.begin(), selectedNoteIds_.end(), noteId)
           != selectedNoteIds_.end();
}

void PianoRollComponent::normaliseSelection()
{
    std::erase_if(selectedNoteIds_, [this](const auto id) {
        return std::none_of(notes_.begin(), notes_.end(), [id](const auto& note) {
            return note.id == id;
        });
    });
    std::sort(selectedNoteIds_.begin(), selectedNoteIds_.end());
    selectedNoteIds_.erase(
        std::unique(selectedNoteIds_.begin(), selectedNoteIds_.end()),
        selectedNoteIds_.end());
}

void PianoRollComponent::selectOnly(const std::uint64_t noteId)
{
    selectedNoteIds_.assign(1, noteId);
    updateStatusLabel();
    repaint();
}

void PianoRollComponent::toggleSelection(const std::uint64_t noteId)
{
    const auto found = std::find(selectedNoteIds_.begin(),
                                 selectedNoteIds_.end(), noteId);
    if (found == selectedNoteIds_.end())
        selectedNoteIds_.push_back(noteId);
    else
        selectedNoteIds_.erase(found);
    updateStatusLabel();
    repaint();
}

void PianoRollComponent::updateStatusLabel()
{
    if (recording_) {
        auto status = tr("midi.recordingStatus", "count",
                         juce::String(static_cast<int>(recordedNoteCount_)));
        if (droppedRecordingEvents_ > 0)
            status += "  |  " + tr("midi.recordingDropped", "count",
                                     juce::String(droppedRecordingEvents_));
        recordingStatusLabel_.setText(status, juce::dontSendNotification);
        recordingStatusLabel_.setColour(juce::Label::textColourId,
                                        juce::Colour(0xffff7777));
        return;
    }
    recordingStatusLabel_.setColour(juce::Label::textColourId,
                                    juce::Colour(0xffaeb6c5));
    recordingStatusLabel_.setText(
        selectedNoteIds_.empty()
            ? tr("midi.recordReady")
            : tr("midi.selectionStatus", "count",
                 juce::String(static_cast<int>(selectedNoteIds_.size()))),
        juce::dontSendNotification);
}

bool PianoRollComponent::applyNotes(
    std::vector<AudioTrackController::MidiNote> notes)
{
    if (recording_ || sameNoteData(notes, notes_))
        return false;
    if (notesEditHandler_)
        notesEditHandler_(std::move(notes));
    else
        notes_ = std::move(notes);
    normaliseSelection();
    updateStatusLabel();
    repaint();
    return true;
}

bool PianoRollComponent::insertClipboardAt(const double destinationBeat)
{
    if (noteClipboard_.empty())
        return false;
    const auto sourceStart = std::min_element(
        noteClipboard_.begin(), noteClipboard_.end(),
        [](const auto& first, const auto& second) {
            return first.startBeats < second.startBeats;
        })->startBeats;
    std::unordered_set<std::uint64_t> originalIds;
    originalIds.reserve(notes_.size());
    for (const auto& note : notes_)
        originalIds.insert(note.id);
    auto desired = notes_;
    desired.reserve(notes_.size() + noteClipboard_.size());
    for (auto copy : noteClipboard_) {
        copy.id = 0;
        copy.startBeats = std::max(
            0.0, destinationBeat + copy.startBeats - sourceStart);
        desired.push_back(copy);
    }
    if (!applyNotes(std::move(desired)))
        return false;
    selectedNoteIds_.clear();
    for (const auto& note : notes_)
        if (!originalIds.contains(note.id))
            selectedNoteIds_.push_back(note.id);
    normaliseSelection();
    updateStatusLabel();
    repaint();
    return !selectedNoteIds_.empty();
}

void PianoRollComponent::refreshTranslations()
{
    const auto spaceMode = juce::jlimit(
        1, 2, spacePlayModeSelector_.getSelectedId() == 0
                  ? 1 : spacePlayModeSelector_.getSelectedId());
    spacePlayModeSelector_.clear(juce::dontSendNotification);
    spacePlayModeSelector_.addItem(tr("clipEditor.spaceResume"), 1);
    spacePlayModeSelector_.addItem(tr("clipEditor.spaceRestart"), 2);
    spacePlayModeSelector_.setSelectedId(spaceMode,
                                         juce::dontSendNotification);
    spacePlayModeSelector_.setTooltip(tr("clipEditor.spaceModeHelp"));
    emptyMessage_ = tr("midi.empty");
    const auto selectedData = juce::jlimit(
        1, 2, noteDataSelector_.getSelectedId() == 0
                  ? 1 : noteDataSelector_.getSelectedId());
    noteDataSelector_.clear(juce::dontSendNotification);
    noteDataSelector_.addItem(tr("midi.velocity"), 1);
    noteDataSelector_.addItem(tr("midi.noteDuration"), 2);
    noteDataSelector_.setSelectedId(selectedData,
                                    juce::dontSendNotification);
    noteDataSelector_.setTooltip(tr("midi.noteDataHelp"));
    setTooltip(tr("midi.help"));
    trackSelector_.setTextWhenNothingSelected(tr("midi.track"));
    clipSelector_.setTextWhenNothingSelected(tr("midi.clip"));
    addTrackButton_.setButtonText(tr("midi.addTrack"));
    addClipButton_.setButtonText(tr("midi.addClip"));
    muteButton_.setButtonText(tr("track.muteShort"));
    soloButton_.setButtonText(tr("track.soloShort"));
    const auto selectedQuantization = juce::jlimit(
        1, 5, recordQuantizationSelector_.getSelectedId());
    recordQuantizationSelector_.clear(juce::dontSendNotification);
    recordQuantizationSelector_.addItem(tr("midi.recordQuantizeOff"), 1);
    recordQuantizationSelector_.addItem("REC 1/4", 2);
    recordQuantizationSelector_.addItem("REC 1/8", 3);
    recordQuantizationSelector_.addItem("REC 1/16", 4);
    recordQuantizationSelector_.addItem("REC 1/32", 5);
    recordQuantizationSelector_.setSelectedId(selectedQuantization,
                                              juce::dontSendNotification);
    const auto selectedEditSnap = juce::jlimit(
        1, 5, editSnapSelector_.getSelectedId() == 0
                  ? 4 : editSnapSelector_.getSelectedId());
    const auto selectedEditSnapType = juce::jlimit(
        1, 3, editSnapTypeSelector_.getSelectedId() == 0
                  ? 1 : editSnapTypeSelector_.getSelectedId());
    updatingSnapControls_ = true;
    editSnapSelector_.clear(juce::dontSendNotification);
    editSnapSelector_.addItem(tr("snap.off"), 1);
    editSnapSelector_.addItem("1/4", 2);
    editSnapSelector_.addItem("1/8", 3);
    editSnapSelector_.addItem("1/16", 4);
    editSnapSelector_.addItem("1/32", 5);
    editSnapSelector_.setSelectedId(selectedEditSnap,
                                    juce::dontSendNotification);
    editSnapTypeSelector_.clear(juce::dontSendNotification);
    editSnapTypeSelector_.addItem(tr("snap.type.grid"), 1);
    editSnapTypeSelector_.addItem(tr("snap.type.relative"), 2);
    editSnapTypeSelector_.addItem(tr("snap.type.events"), 3);
    editSnapTypeSelector_.setSelectedId(selectedEditSnapType,
                                        juce::dontSendNotification);
    updatingSnapControls_ = false;
    overdubButton_.setButtonText(tr("midi.overdub"));
    clipLoopButton_.setButtonText(tr("transport.loop"));
    playButton_.setButtonText(juce::String::fromUTF8(
        previewPlaying_ ? "\xE2\x9D\x9A\xE2\x9D\x9A"
                        : "\xE2\x96\xB6"));
    stopButton_.setButtonText(juce::String::fromUTF8("\xE2\x96\xA0"));
    returnButton_.setButtonText(juce::String("|")
                                + juce::String::fromUTF8("\xE2\x97\x80"));
    playButton_.setTooltip(previewPlaying_ ? tr("transport.pause")
                                           : tr("transport.play"));
    stopButton_.setTooltip(tr("transport.stop"));
    returnButton_.setTooltip(tr("clipEditor.returnToStart"));
    durationLabel_.setText(tr("clipEditor.duration"),
                           juce::dontSendNotification);
    startLabel_.setText(tr("clipEditor.start"),
                        juce::dontSendNotification);
    endLabel_.setText(tr("clipEditor.end"),
                      juce::dontSendNotification);
    loopStartLabel_.setText(tr("clipEditor.loopStart"),
                            juce::dontSendNotification);
    loopEndLabel_.setText(tr("clipEditor.loopEnd"),
                          juce::dontSendNotification);
    durationEditor_.setTooltip(tr("midi.clipDurationHelp"));
    startEditor_.setTooltip(tr("midi.clipStartHelp"));
    endEditor_.setTooltip(tr("midi.clipEndHelp"));
    loopStartEditor_.setTooltip(tr("midi.clipLoopStartHelp"));
    loopEndEditor_.setTooltip(tr("midi.clipLoopEndHelp"));
    importMidiButton_.setButtonText(tr("midi.importShort"));
    exportMidiButton_.setButtonText(tr("midi.exportShort"));
    undoButton_.setButtonText(tr("audioEditor.undo"));
    redoButton_.setButtonText(tr("audioEditor.redo"));
    trackSelector_.setTooltip(tr("midi.trackHelp"));
    clipSelector_.setTooltip(tr("midi.clipHelp"));
    addTrackButton_.setTooltip(tr("midi.addTrackHelp"));
    addClipButton_.setTooltip(tr("midi.addClipHelp"));
    gainSlider_.setTooltip(tr("midi.gainHelp"));
    panSlider_.setTooltip(tr("midi.panHelp"));
    muteButton_.setTooltip(tr("track.mute"));
    soloButton_.setTooltip(tr("track.solo"));
    recordQuantizationSelector_.setTooltip(tr("midi.recordQuantizeHelp"));
    editSnapSelector_.setTooltip(tr("snap.help"));
    editSnapTypeSelector_.setTooltip(tr("snap.type.help"));
    overdubButton_.setTooltip(tr("midi.overdubHelp"));
    clipLoopButton_.setTooltip(tr("clipEditor.loopHelp"));
    importMidiButton_.setTooltip(tr("midi.importHelp"));
    exportMidiButton_.setTooltip(tr("midi.exportHelp"));
    undoButton_.setTooltip(tr("midi.undoHelp"));
    redoButton_.setTooltip(tr("midi.redoHelp"));
    updateStatusLabel();
    repaint();
}

void PianoRollComponent::setAddHandler(std::function<void(int, double, double, float)> handler)
{
    addHandler_ = std::move(handler);
}

void PianoRollComponent::setNotesEditHandler(
    std::function<void(std::vector<AudioTrackController::MidiNote>)> handler)
{
    notesEditHandler_ = std::move(handler);
}

void PianoRollComponent::setSeekHandler(std::function<void(double)> handler)
{
    seekHandler_ = std::move(handler);
}

void PianoRollComponent::setTrackSelectionHandler(
    std::function<void(std::uint64_t)> handler)
{
    trackSelectionHandler_ = std::move(handler);
}

void PianoRollComponent::setClipSelectionHandler(
    std::function<void(std::uint64_t)> handler)
{
    clipSelectionHandler_ = std::move(handler);
}

void PianoRollComponent::setAddTrackHandler(std::function<void()> handler)
{
    addTrackHandler_ = std::move(handler);
}

void PianoRollComponent::setAddClipHandler(std::function<void()> handler)
{
    addClipHandler_ = std::move(handler);
}

void PianoRollComponent::setImportMidiHandler(std::function<void()> handler)
{
    importMidiHandler_ = std::move(handler);
}

void PianoRollComponent::setExportMidiHandler(std::function<void()> handler)
{
    exportMidiHandler_ = std::move(handler);
}

void PianoRollComponent::setMixerHandler(
    std::function<void(float, float, bool, bool)> handler)
{
    mixerHandler_ = std::move(handler);
}

void PianoRollComponent::setUndoHandler(std::function<void()> handler)
{
    undoHandler_ = std::move(handler);
}

void PianoRollComponent::setRedoHandler(std::function<void()> handler)
{
    redoHandler_ = std::move(handler);
}

void PianoRollComponent::setEditGestureHandlers(
    std::function<void()> beginHandler,
    std::function<void()> endHandler)
{
    beginEditHandler_ = std::move(beginHandler);
    endEditHandler_ = std::move(endHandler);
}

void PianoRollComponent::setViewChangedHandler(
    std::function<void(double, double)> handler)
{
    viewChangedHandler_ = std::move(handler);
}

void PianoRollComponent::setClipBoundsHandler(
    std::function<void(double, double, double, double, double, bool)> handler)
{
    clipBoundsHandler_ = std::move(handler);
}

void PianoRollComponent::setTransportHandler(
    std::function<void(TransportCommand)> handler)
{
    transportHandler_ = std::move(handler);
}

void PianoRollComponent::paint(juce::Graphics& graphics)
{
    const paint_profile::Scoped paintScope(paint_profile::counters().roll);
    graphics.fillAll(juce::Colour(0xff11141a));
    const auto grid = gridBounds();
    // What this paint is for: a playhead strip, a dragged note, or the
    // whole roll. Rows, lines, notes and the velocity lane outside it are
    // skipped rather than drawn and thrown away.
    const auto clip = graphics.getClipBounds().toFloat();
    const auto velocity = velocityBounds();
    const auto velocityHeader = juce::Rectangle<float>(
        keyboardWidth,
        std::max(toolbarHeight + rulerHeight,
                 static_cast<float>(getHeight()) - velocityLaneHeight),
        std::max(1.0f, static_cast<float>(getWidth()) - keyboardWidth),
        velocityHeaderHeight);
    const auto visibleHighestNote = visibleLowestNote_ + visibleNoteCount_ - 1;
    const auto rowHeight = grid.getHeight()
                           / static_cast<float>(visibleNoteCount_);
    const auto beatWidth = grid.getWidth() / static_cast<float>(visibleBeats_);

    graphics.setColour(juce::Colour(0xff181d25));
    graphics.fillRect(0.0f, toolbarHeight + rulerHeight, keyboardWidth, grid.getHeight());
    graphics.setColour(juce::Colour(0xff1a1f27));
    graphics.fillRect(grid);

    for (int note = visibleLowestNote_; note <= visibleHighestNote; ++note) {
        const auto y = grid.getY()
                       + static_cast<float>(visibleHighestNote - note)
                             * rowHeight;
        if (y + rowHeight < clip.getY() - 1.0f || y > clip.getBottom() + 1.0f)
            continue;
        const auto pitchClass = note % 12;
        const auto black = pitchClass == 1 || pitchClass == 3 || pitchClass == 6
                           || pitchClass == 8 || pitchClass == 10;
        if (black) {
            graphics.setColour(juce::Colour(0xff151920));
            graphics.fillRect(grid.getX(), y, grid.getWidth(), rowHeight);
            graphics.setColour(juce::Colour(0xff242b36));
            graphics.fillRect(0.0f, y, keyboardWidth * 0.68f, rowHeight);
        }
        graphics.setColour(juce::Colour(0xff2a303a));
        graphics.drawHorizontalLine(static_cast<int>(y), 0.0f, static_cast<float>(getWidth()));
        if (pitchClass == 0) {
            graphics.setColour(juce::Colour(0xffaeb5c2));
            graphics.setFont(juce::FontOptions(9.0f));
            graphics.drawText("C" + juce::String(note / 12 - 1), 4, static_cast<int>(y),
                              static_cast<int>(keyboardWidth) - 8, static_cast<int>(std::ceil(rowHeight)),
                              juce::Justification::centredLeft);
        }
    }

    const auto editStep = snapStepBeats();
    if (editStep > 0.0
        && editStep / visibleBeats_ * grid.getWidth() >= 6.0f) {
        const auto firstStep = static_cast<long long>(
            std::floor(visibleStartBeat_ / editStep));
        const auto lastStep = static_cast<long long>(
            std::ceil((visibleStartBeat_ + visibleBeats_) / editStep));
        graphics.setColour(juce::Colour(0xff252c36));
        for (auto step = firstStep; step <= lastStep; ++step) {
            const auto beat = static_cast<double>(step) * editStep;
            if (std::abs(beat - std::round(beat)) < 0.000001)
                continue;
            const auto x = grid.getX()
                + static_cast<float>((beat - visibleStartBeat_)
                                     / visibleBeats_) * grid.getWidth();
            if (x < clip.getX() - 1.0f || x > clip.getRight() + 1.0f)
                continue;
            graphics.drawVerticalLine(juce::roundToInt(x),
                                      toolbarHeight,
                                      velocity.getBottom());
        }
    }

    const auto firstVisibleBeat = static_cast<int>(
        std::floor(visibleStartBeat_));
    const auto lastVisibleBeat = static_cast<int>(
        std::ceil(visibleStartBeat_ + visibleBeats_));
    for (int beat = firstVisibleBeat; beat <= lastVisibleBeat; ++beat) {
        const auto x = grid.getX()
                       + static_cast<float>(
                           static_cast<double>(beat) - visibleStartBeat_)
                             * beatWidth;
        // The bar number sits up to forty pixels right of its line.
        if (x < clip.getX() - 41.0f || x > clip.getRight() + 1.0f)
            continue;
        graphics.setColour(juce::Colour(beat % 4 == 0 ? 0xff495260 : 0xff303744));
        graphics.drawVerticalLine(static_cast<int>(x), toolbarHeight,
                                  velocity.getBottom());
        if (beat % 4 == 0) {
            graphics.setColour(juce::Colour(0xffaeb5c2));
            graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            graphics.drawText(juce::String(beat / 4 + 1), static_cast<int>(x) + 4,
                              static_cast<int>(toolbarHeight) + 2, 36, 18,
                              juce::Justification::centredLeft);
        }
    }

    const auto xForBeat = [this, &grid](const double beat) {
        return grid.getX()
               + static_cast<float>((beat - visibleStartBeat_)
                                    / std::max(0.000001, visibleBeats_))
                     * grid.getWidth();
    };
    const auto rulerTop = toolbarHeight;
    const auto bandHeight = rulerHeight / 3.0f;
    const auto durationY = rulerTop + bandHeight * 0.5f;
    const auto playY = rulerTop + bandHeight * 1.5f;
    const auto loopY = rulerTop + bandHeight * 2.5f;
    graphics.setColour(juce::Colour(0xff323a46));
    graphics.drawHorizontalLine(
        juce::roundToInt(rulerTop + bandHeight), 0.0f,
        static_cast<float>(getWidth()));
    graphics.drawHorizontalLine(
        juce::roundToInt(rulerTop + bandHeight * 2.0f), 0.0f,
        static_cast<float>(getWidth()));
    graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    graphics.setColour(juce::Colour(0xff8f98a7));
    graphics.drawText("DUR", 4, juce::roundToInt(rulerTop),
                      static_cast<int>(keyboardWidth) - 8,
                      juce::roundToInt(bandHeight),
                      juce::Justification::centredLeft);
    graphics.drawText("S / E", 4,
                      juce::roundToInt(rulerTop + bandHeight),
                      static_cast<int>(keyboardWidth) - 8,
                      juce::roundToInt(bandHeight),
                      juce::Justification::centredLeft);
    graphics.drawText("LOOP", 4,
                      juce::roundToInt(rulerTop + bandHeight * 2.0f),
                      static_cast<int>(keyboardWidth) - 8,
                      juce::roundToInt(bandHeight),
                      juce::Justification::centredLeft);
    const auto drawRange = [&graphics](const float startX,
                                        const float endX,
                                        const float y,
                                        const juce::Colour colour,
                                        const juce::String& startText,
                                        const juce::String& endText) {
        graphics.setColour(colour.withAlpha(0.82f));
        graphics.fillRect(std::min(startX, endX), y - 1.5f,
                          std::max(1.0f, std::abs(endX - startX)), 3.0f);
        const auto drawHandle = [&graphics, colour, y](
                                    const float x,
                                    const juce::String& text) {
            graphics.setColour(colour);
            graphics.fillRoundedRectangle(x - 5.5f, y - 5.5f,
                                           11.0f, 11.0f, 2.0f);
            graphics.setColour(juce::Colour(0xff101419));
            graphics.setFont(juce::FontOptions(7.0f, juce::Font::bold));
            graphics.drawText(text, juce::roundToInt(x - 6.0f),
                              juce::roundToInt(y - 6.0f), 12, 12,
                              juce::Justification::centred);
        };
        drawHandle(startX, startText);
        drawHandle(endX, endText);
    };
    const auto clipStartX = xForBeat(0.0);
    const auto clipEndX = xForBeat(clipLengthBeats_);
    drawRange(clipStartX, clipEndX, durationY,
              juce::Colour(0xffff9b2f), "0", "D");
    const auto playX1 = xForBeat(playStartBeats_);
    const auto playX2 = xForBeat(playEndBeats_);
    drawRange(playX1, playX2, playY, juce::Colour(0xff54d4c0),
              "S", "E");
    if (clipLooping_) {
        const auto loopX1 = xForBeat(loopStartBeats_);
        const auto loopX2 = xForBeat(loopEndBeats_);
        drawRange(loopX1, loopX2, loopY, juce::Colour(0xffb889ff),
                  "L", "R");
    }

    for (const auto& note : notes_) {
        const auto bounds = noteBounds(note).reduced(1.0f);
        if (!bounds.intersects(clip))
            continue;
        const auto selected = isSelected(note.id);
        graphics.setColour(juce::Colour(selected ? 0xff79ebca : 0xff4fd3ae));
        graphics.fillRoundedRectangle(bounds, 2.5f);
        graphics.setColour(juce::Colour(0xffb8ffe9));
        graphics.drawRoundedRectangle(bounds, 2.5f, 1.0f);
        if (selected && bounds.getWidth() >= 7.0f) {
            graphics.setColour(juce::Colour(0xff173b34));
            graphics.fillRect(bounds.getRight() - 3.0f, bounds.getY() + 1.0f,
                              2.0f, std::max(1.0f, bounds.getHeight() - 2.0f));
        }
    }

    if (gestureMode_ == GestureMode::marquee
        && !marqueeBounds_.isEmpty()) {
        graphics.setColour(juce::Colour(0x284fd3ae));
        graphics.fillRect(marqueeBounds_);
        graphics.setColour(juce::Colour(0xff79ebca));
        graphics.drawRect(marqueeBounds_, 1.0f);
    }

    graphics.setColour(juce::Colour(0xff171c24));
    graphics.fillRect(velocityHeader);
    graphics.setColour(juce::Colour(0xff3b4553));
    graphics.drawHorizontalLine(static_cast<int>(velocityHeader.getY()),
                                0.0f, static_cast<float>(getWidth()));
    graphics.setColour(juce::Colour(0xff121720));
    graphics.fillRect(velocity);
    graphics.fillRect(0.0f, velocity.getY(), keyboardWidth,
                      velocity.getHeight());
    graphics.setColour(juce::Colour(0xff3b4553));
    graphics.drawHorizontalLine(static_cast<int>(velocity.getY()),
                                0.0f, static_cast<float>(getWidth()));
    graphics.setColour(juce::Colour(0xff8e98aa));
    graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    const auto durationData = noteDataSelector_.getSelectedId()
                              == static_cast<int>(NoteDataLane::duration);
    const auto velocityHeight = std::max(1.0f, velocity.getHeight() - 11.0f);
    for (const auto& note : notes_) {
        if (!clip.intersects(velocity))
            break;
        const auto x = grid.getX()
                       + static_cast<float>(
                           (note.startBeats - visibleStartBeat_)
                           / visibleBeats_)
                             * grid.getWidth();
        if (x < velocity.getX() || x > velocity.getRight()
            || x < clip.getX() - 11.0f || x > clip.getRight() + 1.0f)
            continue;
        const auto width = juce::jlimit(
            3.0f, 10.0f,
            static_cast<float>(note.durationBeats) * beatWidth * 0.22f);
        const auto dataAmount = durationData
            ? juce::jlimit(0.01f, 1.0f,
                           static_cast<float>(note.durationBeats / 4.0))
            : note.velocity;
        const auto height = velocityHeight * dataAmount;
        graphics.setColour(juce::Colour(
            isSelected(note.id) ? 0xff79ebca : 0xff596579));
        graphics.fillRoundedRectangle(
            x - width * 0.5f, velocity.getBottom() - height - 4.0f,
            width, height, 1.5f);
    }

    const auto playheadBeats = playheadSeconds_ * tempoBpm_ / 60.0;
    const auto playheadX = grid.getX()
        + static_cast<float>((playheadBeats - visibleStartBeat_)
                             / visibleBeats_) * grid.getWidth();
    if (playheadX >= grid.getX() && playheadX <= grid.getRight()) {
        graphics.setColour(juce::Colour(0xffefb45b));
        graphics.fillRect(playheadX, toolbarHeight, 2.0f,
                          static_cast<float>(getHeight()) - toolbarHeight);
    }

    if (notes_.empty()) {
        graphics.setColour(juce::Colour(0xff7f889a));
        graphics.setFont(juce::FontOptions(12.0f));
        graphics.drawFittedText(emptyMessage_, grid.toNearestInt().reduced(30),
                                juce::Justification::centred, 2);
    }
    graphics.setColour(juce::Colour(0xff303744));
    graphics.drawRect(getLocalBounds(), 1);
}

void PianoRollComponent::resized()
{
    auto toolbar = getLocalBounds().removeFromTop(
        static_cast<int>(toolbarHeight)).reduced(6, 3);
    auto firstRow = toolbar.removeFromTop(28);
    auto secondRow = toolbar.removeFromTop(29);
    auto boundsRow = toolbar.removeFromTop(30);
    const auto toggleWidth = 28;
    soloButton_.setBounds(firstRow.removeFromRight(toggleWidth));
    muteButton_.setBounds(firstRow.removeFromRight(toggleWidth));
    firstRow.removeFromRight(4);
    panSlider_.setBounds(firstRow.removeFromRight(92));
    firstRow.removeFromRight(4);
    gainSlider_.setBounds(firstRow.removeFromRight(118));
    firstRow.removeFromRight(6);
    const auto addWidth = juce::jlimit(62, 88, firstRow.getWidth() / 7);
    addClipButton_.setBounds(firstRow.removeFromRight(addWidth));
    firstRow.removeFromRight(5);
    addTrackButton_.setBounds(firstRow.removeFromRight(addWidth));
    firstRow.removeFromRight(8);
    const auto selectorWidth = std::max(90, firstRow.getWidth() / 2);
    trackSelector_.setBounds(firstRow.removeFromLeft(selectorWidth));
    firstRow.removeFromLeft(5);
    clipSelector_.setBounds(firstRow);

    recordQuantizationSelector_.setBounds(
        secondRow.removeFromLeft(116).reduced(1));
    returnButton_.setBounds(secondRow.removeFromLeft(34).reduced(2, 0));
    stopButton_.setBounds(secondRow.removeFromLeft(34).reduced(2, 0));
    playButton_.setBounds(secondRow.removeFromLeft(38).reduced(2, 0));
    spacePlayModeSelector_.setBounds(
        secondRow.removeFromLeft(104).reduced(2, 0));
    clipLoopButton_.setBounds(secondRow.removeFromLeft(68).reduced(2, 0));
    overdubButton_.setBounds(secondRow.removeFromLeft(108).reduced(2, 0));
    importMidiButton_.setBounds(secondRow.removeFromLeft(82).reduced(2, 0));
    exportMidiButton_.setBounds(secondRow.removeFromLeft(82).reduced(2, 0));
    redoButton_.setBounds(secondRow.removeFromRight(74).reduced(2, 0));
    undoButton_.setBounds(secondRow.removeFromRight(74).reduced(2, 0));
    editSnapTypeSelector_.setBounds(
        secondRow.removeFromRight(96).reduced(2, 0));
    editSnapSelector_.setBounds(
        secondRow.removeFromRight(72).reduced(2, 0));
    recordingStatusLabel_.setBounds(secondRow.reduced(8, 0));

    const auto pairWidth = juce::jlimit(
        104, 148, std::max(1, boundsRow.getWidth() / 5));
    const auto setBoundsPair = [&boundsRow, pairWidth](
                                   juce::Label& label,
                                   juce::TextEditor& editor) {
        auto pair = boundsRow.removeFromLeft(
            std::min(pairWidth, boundsRow.getWidth()));
        label.setBounds(pair.removeFromLeft(
            juce::jlimit(54, 86, pair.getWidth() * 3 / 5)).reduced(1, 2));
        editor.setBounds(pair.reduced(2, 4));
    };
    setBoundsPair(durationLabel_, durationEditor_);
    setBoundsPair(startLabel_, startEditor_);
    setBoundsPair(endLabel_, endEditor_);
    setBoundsPair(loopStartLabel_, loopStartEditor_);
    setBoundsPair(loopEndLabel_, loopEndEditor_);
    const auto laneTop = juce::roundToInt(std::max(
        toolbarHeight + rulerHeight,
        static_cast<float>(getHeight()) - velocityLaneHeight));
    noteDataSelector_.setBounds(
        juce::roundToInt(keyboardWidth) + 4, laneTop + 3,
        juce::jlimit(104, 168, getWidth() / 8), 22);
}

void PianoRollComponent::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (recording_)
        return;

    if (event.mods.isMiddleButtonDown()
        && event.position.y >= toolbarHeight + rulerHeight) {
        navigatingGrid_ = true;
        rulerGestureMoved_ = false;
        rulerDragLast_ = event.position;
        pitchDragAccumulator_ = 0.0f;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    if (event.position.x < keyboardWidth) {
        if (event.position.y >= toolbarHeight + rulerHeight
            && event.mods.isLeftButtonDown()) {
            navigatingPitches_ = true;
            rulerDragLast_ = event.position;
            pitchDragAccumulator_ = 0.0f;
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        }
        return;
    }

    if (event.position.y < toolbarHeight)
        return;

    if (event.position.y < toolbarHeight + rulerHeight) {
        if (event.mods.isLeftButtonDown()) {
            draggedBoundHandle_ = boundHandleAt(event.position);
            if (draggedBoundHandle_ != BoundHandle::none) {
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }
        navigatingRuler_ = event.mods.isLeftButtonDown()
                           || event.mods.isMiddleButtonDown();
        rulerGestureMoved_ = false;
        rulerDragLast_ = event.position;
        rulerAnchorBeat_ = beatAt(event.position.x);
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    const auto additive = event.mods.isCommandDown()
                          || event.mods.isShiftDown();
    if (velocityBounds().contains(event.position)) {
        if (const auto* note = velocityNoteAt(event.position)) {
            const auto noteId = note->id;
            if (additive)
                toggleSelection(noteId);
            else if (!isSelected(noteId))
                selectOnly(noteId);
            if (isSelected(noteId))
                beginGesture(GestureMode::noteData, event.position);
        } else if (!additive) {
            selectedNoteIds_.clear();
            updateStatusLabel();
            repaint();
        }
        return;
    }

    if (const auto* note = noteAt(event.position)) {
        const auto noteId = note->id;
        if (event.mods.isPopupMenu()) {
            if (!isSelected(noteId))
                selectOnly(noteId);
            showNoteMenu();
            return;
        }

        if (additive)
            toggleSelection(noteId);
        else if (!isSelected(noteId))
            selectOnly(noteId);
        if (!isSelected(noteId))
            return;
        beginGesture(isResizeHandle(*note, event.position)
                         ? GestureMode::resize
                         : GestureMode::move,
                     event.position);
        return;
    }

    if (!gridBounds().contains(event.position)
        || !event.mods.isLeftButtonDown())
        return;
    selectionAtGestureStart_ = selectedNoteIds_;
    additiveMarquee_ = additive;
    if (!additive)
        selectedNoteIds_.clear();
    pendingGridClick_ = true;
    pendingNoteNumber_ = noteNumberAt(event.position.y);
    pendingStartBeat_ = snapBeat(beatAt(event.position.x));
    marqueeAnchor_ = event.position;
    marqueeBounds_ = {};
    updateStatusLabel();
    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (recording_)
        return;

    if (draggedBoundHandle_ != BoundHandle::none) {
        dragBoundHandle(draggedBoundHandle_,
                        snapBeat(beatAt(event.position.x)));
        return;
    }

    if (navigatingGrid_ || navigatingPitches_) {
        const auto delta = event.position - rulerDragLast_;
        if (navigatingGrid_ && std::abs(delta.x) >= 1.0f
            && gridBounds().getWidth() > 0.0f)
            panTimeline(-static_cast<double>(delta.x)
                        / static_cast<double>(gridBounds().getWidth())
                        * visibleBeats_);
        const auto rowHeight = gridBounds().getHeight()
                               / static_cast<float>(visibleNoteCount_);
        pitchDragAccumulator_ += delta.y;
        if (std::abs(pitchDragAccumulator_) >= rowHeight) {
            const auto notes = static_cast<int>(
                std::trunc(pitchDragAccumulator_ / rowHeight));
            panPitches(notes);
            pitchDragAccumulator_ -= static_cast<float>(notes) * rowHeight;
        }
        rulerDragLast_ = event.position;
        return;
    }

    if (navigatingRuler_) {
        const auto delta = event.position - rulerDragLast_;
        if (std::abs(delta.x) >= 1.0f || std::abs(delta.y) >= 1.0f)
            rulerGestureMoved_ = true;
        if (std::abs(delta.y) >= 1.0f)
            zoomTimeline(
                std::pow(1.025, -static_cast<double>(delta.y)),
                rulerAnchorBeat_);
        if (std::abs(delta.x) >= 1.0f && gridBounds().getWidth() > 0.0f)
            panTimeline(-static_cast<double>(delta.x)
                        / static_cast<double>(gridBounds().getWidth())
                        * visibleBeats_);
        rulerDragLast_ = event.position;
        return;
    }

    if (pendingGridClick_) {
        if (event.position.getDistanceFrom(marqueeAnchor_) < 4.0f)
            return;
        pendingGridClick_ = false;
        gestureMode_ = GestureMode::marquee;
    }

    if (gestureMode_ == GestureMode::marquee) {
        const auto constrained = gridBounds().getConstrainedPoint(
            event.position);
        marqueeBounds_ = juce::Rectangle<float>(marqueeAnchor_, constrained)
                              .getSmallestIntegerContainer().toFloat()
                              .getIntersection(gridBounds());
        selectedNoteIds_ = additiveMarquee_
                               ? selectionAtGestureStart_
                               : std::vector<std::uint64_t> {};
        for (const auto& note : notes_)
            if (noteBounds(note).intersects(marqueeBounds_)
                && !isSelected(note.id))
                selectedNoteIds_.push_back(note.id);
        normaliseSelection();
        updateStatusLabel();
        repaint();
        return;
    }

    if (gestureMode_ == GestureMode::none || gestureNotes_.empty())
        return;

    auto desired = gestureNotes_;
    const auto grid = gridBounds();
    if (gestureMode_ == GestureMode::move) {
        auto beatDelta = snapBeat(
            static_cast<double>(event.position.x - dragStart_.x)
            / static_cast<double>(grid.getWidth()) * visibleBeats_, 0.0);
        auto minimumStart = std::numeric_limits<double>::max();
        auto minimumPitch = midiHighestNote;
        auto maximumPitch = midiLowestNote;
        for (const auto& note : gestureNotes_)
            if (isSelected(note.id)) {
                minimumStart = std::min(minimumStart, note.startBeats);
                minimumPitch = std::min(minimumPitch, note.noteNumber);
                maximumPitch = std::max(maximumPitch, note.noteNumber);
            }
        beatDelta = std::max(beatDelta, -minimumStart);
        const auto rowHeight = grid.getHeight()
                               / static_cast<float>(visibleNoteCount_);
        auto noteDelta = -static_cast<int>(std::lround(
            (event.position.y - dragStart_.y) / rowHeight));
        noteDelta = juce::jlimit(midiLowestNote - minimumPitch,
                                midiHighestNote - maximumPitch, noteDelta);
        for (auto& note : desired)
            if (isSelected(note.id)) {
                note.startBeats += beatDelta;
                note.noteNumber += noteDelta;
            }
    } else if (gestureMode_ == GestureMode::resize) {
        const auto durationDelta = snapBeat(
            static_cast<double>(event.position.x - dragStart_.x)
            / static_cast<double>(grid.getWidth()) * visibleBeats_, 0.0);
        for (auto& note : desired)
            if (isSelected(note.id))
                note.durationBeats = std::max(
                    0.03125, note.durationBeats + durationDelta);
    } else if (gestureMode_ == GestureMode::noteData) {
        const auto delta = -(event.position.y - dragStart_.y)
                           / velocityLaneHeight;
        const auto durationData = noteDataSelector_.getSelectedId()
                                  == static_cast<int>(
                                      NoteDataLane::duration);
        for (auto& note : desired)
            if (isSelected(note.id)) {
                if (durationData)
                    note.durationBeats = std::max(
                        0.03125, snapBeat(note.durationBeats
                                             + static_cast<double>(delta)
                                                   * 4.0,
                                         0.0));
                else
                    note.velocity = juce::jlimit(
                        0.01f, 1.0f, note.velocity + delta);
            }
    }
    (void) applyNotes(std::move(desired));
}

void PianoRollComponent::mouseUp(const juce::MouseEvent& event)
{
    if (draggedBoundHandle_ != BoundHandle::none) {
        const auto resizedClip
            = draggedBoundHandle_ == BoundHandle::clipLength;
        dragBoundHandle(draggedBoundHandle_,
                        snapBeat(beatAt(event.position.x)));
        if (resizedClip) {
            refreshContentExtent(true);
            if (viewChangedHandler_)
                viewChangedHandler_(visibleStartBeat_,
                                    visibleStartBeat_ + visibleBeats_);
        }
        publishClipBounds();
        draggedBoundHandle_ = BoundHandle::none;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (navigatingGrid_ || navigatingPitches_) {
        navigatingGrid_ = false;
        navigatingPitches_ = false;
        pitchDragAccumulator_ = 0.0f;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (navigatingRuler_) {
        navigatingRuler_ = false;
        if (!rulerGestureMoved_ && seekHandler_)
            seekHandler_(beatAt(event.position.x) * 60.0 / tempoBpm_);
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (pendingGridClick_) {
        pendingGridClick_ = false;
        if (addHandler_) {
            std::unordered_set<std::uint64_t> previousIds;
            previousIds.reserve(notes_.size());
            for (const auto& note : notes_)
                previousIds.insert(note.id);
            addHandler_(pendingNoteNumber_, pendingStartBeat_, 1.0, 0.8f);
            selectedNoteIds_.clear();
            for (const auto& note : notes_)
                if (!previousIds.contains(note.id))
                    selectedNoteIds_.push_back(note.id);
            normaliseSelection();
            updateStatusLabel();
            repaint();
        }
        return;
    }
    if (gestureMode_ == GestureMode::marquee) {
        gestureMode_ = GestureMode::none;
        marqueeBounds_ = {};
        selectionAtGestureStart_.clear();
        repaint();
        return;
    }
    juce::ignoreUnused(event);
    finishGesture();
}

void PianoRollComponent::mouseMove(const juce::MouseEvent& event)
{
    if (recording_) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    if (event.position.x < keyboardWidth
        && event.position.y >= toolbarHeight + rulerHeight) {
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (event.position.y >= toolbarHeight
        && event.position.y < toolbarHeight + rulerHeight
        && event.position.x >= keyboardWidth) {
        setMouseCursor(boundHandleAt(event.position) != BoundHandle::none
                           ? juce::MouseCursor::LeftRightResizeCursor
                           : juce::MouseCursor::DraggingHandCursor);
        return;
    }
    if (velocityBounds().contains(event.position)) {
        setMouseCursor(velocityNoteAt(event.position) != nullptr
                           ? juce::MouseCursor::UpDownResizeCursor
                           : juce::MouseCursor::NormalCursor);
        return;
    }
    if (const auto* note = noteAt(event.position)) {
        setMouseCursor(isResizeHandle(*note, event.position)
                           ? juce::MouseCursor::LeftRightResizeCursor
                           : juce::MouseCursor::DraggingHandCursor);
        return;
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseWheelMove(
    const juce::MouseEvent& event,
    const juce::MouseWheelDetails& wheel)
{
    if (event.position.y < toolbarHeight)
        return;
    if (event.position.x < keyboardWidth) {
        const auto anchor = noteNumberAt(event.position.y);
        if (event.mods.isCommandDown() || event.mods.isCtrlDown())
            zoomPitches(wheel.deltaY > 0.0f ? -6 : 6, anchor);
        else if (std::abs(wheel.deltaY) > 0.0001f)
            panPitches(wheel.deltaY > 0.0f ? 3 : -3);
        return;
    }
    if (event.position.y >= toolbarHeight + rulerHeight) {
        if (event.mods.isMiddleButtonDown()
            || event.mods.isCommandDown()
            || event.mods.isCtrlDown()) {
            zoomTimeline(wheel.deltaY > 0.0f ? 1.35 : 1.0 / 1.35,
                         beatAt(event.position.x));
        } else if (event.mods.isShiftDown()
                   || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
            const auto amount = std::abs(wheel.deltaX) > 0.0001f
                                    ? wheel.deltaX : wheel.deltaY;
            panTimeline(-static_cast<double>(amount)
                        * visibleBeats_ * 0.65);
        } else if (std::abs(wheel.deltaY) > 0.0001f) {
            panPitches(wheel.deltaY > 0.0f ? 3 : -3);
        }
        return;
    }
    if (event.mods.isShiftDown()
        || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
        const auto amount = std::abs(wheel.deltaX) > 0.0001f
                                ? wheel.deltaX : wheel.deltaY;
        panTimeline(-static_cast<double>(amount) * visibleBeats_ * 0.65);
        return;
    }
    if (std::abs(wheel.deltaY) > 0.0001f)
        zoomTimeline(wheel.deltaY > 0.0f ? 1.35 : 1.0 / 1.35,
                     beatAt(event.position.x));
}

bool PianoRollComponent::keyPressed(const juce::KeyPress& key)
{
    if (recording_)
        return false;
    const auto modifiers = key.getModifiers();
    const auto keyCode = key.getKeyCode();
    if (key == juce::KeyPress::spaceKey) {
        triggerPlayPause();
        return transportHandler_ != nullptr;
    }
    if (modifiers.isCommandDown()) {
        if (keyCode == 'A') {
            selectAllNotes();
            return true;
        }
        if (keyCode == 'C')
            return copySelectedNotes();
        if (keyCode == 'X')
            return cutSelectedNotes();
        if (keyCode == 'V')
            return pasteNotes();
        if (keyCode == 'D')
            return duplicateSelectedNotes();
        if (keyCode == 'Z' || keyCode == 'Y')
            return false;
    }
    if (!modifiers.isCommandDown()
        && (keyCode == juce::KeyPress::deleteKey
            || keyCode == juce::KeyPress::backspaceKey))
        return deleteSelectedNotes();
    if (keyCode == juce::KeyPress::escapeKey) {
        selectedNoteIds_.clear();
        updateStatusLabel();
        repaint();
        return true;
    }
    if (keyCode == juce::KeyPress::leftKey
        || keyCode == juce::KeyPress::rightKey) {
        const auto direction = keyCode == juce::KeyPress::leftKey
                                   ? -1.0
                                   : 1.0;
        if (modifiers.isShiftDown())
            return nudgeSelected(0.0, 0, direction * 0.25, 0.0f);
        return nudgeSelected(direction * 0.25, 0, 0.0, 0.0f);
    }
    if (keyCode == juce::KeyPress::upKey
        || keyCode == juce::KeyPress::downKey) {
        const auto direction = keyCode == juce::KeyPress::upKey ? 1 : -1;
        if (modifiers.isAltDown())
            return nudgeSelected(0.0, 0, 0.0,
                                 static_cast<float>(direction) * 0.05f);
        return nudgeSelected(0.0, direction, 0.0, 0.0f);
    }
    return false;
}

juce::Rectangle<float> PianoRollComponent::gridBounds() const
{
    return { keyboardWidth, toolbarHeight + rulerHeight,
             std::max(1.0f, static_cast<float>(getWidth()) - keyboardWidth),
             std::max(1.0f, static_cast<float>(getHeight()) - toolbarHeight
                                - rulerHeight - velocityLaneHeight) };
}

juce::Rectangle<float> PianoRollComponent::velocityBounds() const
{
    return { keyboardWidth,
             std::max(toolbarHeight + rulerHeight,
                      static_cast<float>(getHeight()) - velocityLaneHeight
                          + velocityHeaderHeight),
             std::max(1.0f, static_cast<float>(getWidth()) - keyboardWidth),
             std::max(1.0f, velocityLaneHeight - velocityHeaderHeight) };
}

juce::Rectangle<float> PianoRollComponent::noteBounds(const AudioTrackController::MidiNote& note) const
{
    const auto grid = gridBounds();
    const auto beatWidth = grid.getWidth() / static_cast<float>(visibleBeats_);
    const auto rowHeight = grid.getHeight()
                           / static_cast<float>(visibleNoteCount_);
    const auto visibleHighestNote = visibleLowestNote_
                                    + visibleNoteCount_ - 1;
    return { grid.getX()
                 + static_cast<float>(note.startBeats - visibleStartBeat_)
                       * beatWidth,
             grid.getY()
                 + static_cast<float>(visibleHighestNote - note.noteNumber)
                       * rowHeight,
             std::max(3.0f, static_cast<float>(note.durationBeats) * beatWidth),
             rowHeight };
}

const AudioTrackController::MidiNote* PianoRollComponent::noteAt(const juce::Point<float> point) const
{
    for (auto iterator = notes_.rbegin(); iterator != notes_.rend(); ++iterator)
        if (noteBounds(*iterator).contains(point))
            return &*iterator;
    return nullptr;
}

const AudioTrackController::MidiNote* PianoRollComponent::velocityNoteAt(
    const juce::Point<float> point) const
{
    if (!velocityBounds().contains(point) || notes_.empty())
        return nullptr;
    const auto grid = gridBounds();
    const auto beatWidth = grid.getWidth() / static_cast<float>(visibleBeats_);
    const AudioTrackController::MidiNote* closest = nullptr;
    auto closestDistance = 12.0f;
    for (const auto& note : notes_) {
        const auto x = grid.getX()
                       + static_cast<float>(note.startBeats
                                            - visibleStartBeat_)
                             * beatWidth;
        const auto distance = std::abs(point.x - x);
        if (distance < closestDistance
            || (distance == closestDistance && isSelected(note.id))) {
            closest = &note;
            closestDistance = distance;
        }
    }
    return closest;
}

bool PianoRollComponent::isResizeHandle(
    const AudioTrackController::MidiNote& note,
    const juce::Point<float> point) const noexcept
{
    const auto bounds = noteBounds(note);
    return bounds.expanded(2.0f).contains(point)
           && std::abs(point.x - bounds.getRight()) <= 6.0f;
}

void PianoRollComponent::beginGesture(const GestureMode mode,
                                      const juce::Point<float> point)
{
    gestureMode_ = mode;
    gestureNotes_ = notes_;
    dragStart_ = point;
    if (beginEditHandler_)
        beginEditHandler_();
}

void PianoRollComponent::finishGesture()
{
    if (gestureMode_ == GestureMode::move
        || gestureMode_ == GestureMode::resize
        || gestureMode_ == GestureMode::noteData) {
        if (endEditHandler_)
            endEditHandler_();
    }
    gestureMode_ = GestureMode::none;
    gestureNotes_.clear();
    marqueeBounds_ = {};
    pendingGridClick_ = false;
}

bool PianoRollComponent::nudgeSelected(const double beatDelta,
                                       const int noteDelta,
                                       const double durationDelta,
                                       const float velocityDelta)
{
    if (selectedNoteIds_.empty())
        return false;
    auto safeBeatDelta = beatDelta;
    auto safeNoteDelta = noteDelta;
    auto minimumStart = std::numeric_limits<double>::max();
    auto minimumPitch = midiHighestNote;
    auto maximumPitch = midiLowestNote;
    for (const auto& note : notes_)
        if (isSelected(note.id)) {
            minimumStart = std::min(minimumStart, note.startBeats);
            minimumPitch = std::min(minimumPitch, note.noteNumber);
            maximumPitch = std::max(maximumPitch, note.noteNumber);
        }
    safeBeatDelta = std::max(safeBeatDelta, -minimumStart);
    safeNoteDelta = juce::jlimit(midiLowestNote - minimumPitch,
                                 midiHighestNote - maximumPitch,
                                 safeNoteDelta);
    auto desired = notes_;
    for (auto& note : desired)
        if (isSelected(note.id)) {
            note.startBeats += safeBeatDelta;
            note.noteNumber += safeNoteDelta;
            note.durationBeats = std::max(
                0.03125, note.durationBeats + durationDelta);
            note.velocity = juce::jlimit(
                0.01f, 1.0f, note.velocity + velocityDelta);
        }
    return applyNotes(std::move(desired));
}

void PianoRollComponent::showNoteMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1, tr("midi.copy"), !selectedNoteIds_.empty());
    menu.addItem(2, tr("midi.cut"), !selectedNoteIds_.empty());
    menu.addItem(3, tr("midi.paste"), !noteClipboard_.empty());
    menu.addItem(4, tr("midi.duplicate"), !selectedNoteIds_.empty());
    menu.addItem(5, tr("midi.delete"), !selectedNoteIds_.empty());
    menu.addSeparator();
    menu.addItem(6, tr("midi.selectAll"), !notes_.empty());
    const juce::Component::SafePointer<PianoRollComponent> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this),
        [safeThis](const int result) {
            if (safeThis == nullptr)
                return;
            switch (result) {
                case 1: (void) safeThis->copySelectedNotes(); break;
                case 2: (void) safeThis->cutSelectedNotes(); break;
                case 3: (void) safeThis->pasteNotes(); break;
                case 4: (void) safeThis->duplicateSelectedNotes(); break;
                case 5: (void) safeThis->deleteSelectedNotes(); break;
                case 6: safeThis->selectAllNotes(); break;
                default: break;
            }
        });
}

double PianoRollComponent::beatAt(const float x) const
{
    const auto grid = gridBounds();
    return juce::jlimit(
        visibleStartBeat_, visibleStartBeat_ + visibleBeats_,
        visibleStartBeat_
            + static_cast<double>(x - grid.getX()) / grid.getWidth()
                  * visibleBeats_);
}

int PianoRollComponent::noteNumberAt(const float y) const
{
    const auto grid = gridBounds();
    const auto row = static_cast<int>(
        (y - grid.getY()) / grid.getHeight() * visibleNoteCount_);
    const auto visibleHighestNote = visibleLowestNote_
                                    + visibleNoteCount_ - 1;
    return juce::jlimit(visibleLowestNote_, visibleHighestNote,
                        visibleHighestNote - row);
}

double PianoRollComponent::snapStepBeats() const noexcept
{
    switch (editSnapSelector_.getSelectedId()) {
        case 1: return 0.0;
        case 2: return 1.0;
        case 3: return 0.5;
        case 5: return 0.125;
        case 4:
        default: return 0.25;
    }
}

int PianoRollComponent::gridDivision() const noexcept
{
    const auto step = snapStepBeats();
    return step > 0.0
               ? juce::jlimit(1, 128,
                   juce::roundToInt(4.0 / step))
               : 16;
}

bool PianoRollComponent::snappingEnabled() const noexcept
{
    return snapStepBeats() > 0.0;
}

double PianoRollComponent::snapBeat(
    const double beat,
    const std::optional<double> relativeAnchor) const
{
    const auto step = snapStepBeats();
    if (step <= 0.0)
        return beat;
    if (editSnapTypeSelector_.getSelectedId() == 2
        && relativeAnchor.has_value())
        return *relativeAnchor
               + std::round((beat - *relativeAnchor) / step) * step;
    if (editSnapTypeSelector_.getSelectedId() == 3
        && !notes_.empty()) {
        auto nearest = beat;
        auto distance = step;
        for (const auto& note : notes_) {
            for (const auto edge : {
                     note.startBeats,
                     note.startBeats + note.durationBeats }) {
                const auto candidateDistance = std::abs(edge - beat);
                if (candidateDistance < distance) {
                    distance = candidateDistance;
                    nearest = edge;
                }
            }
        }
        if (distance < step)
            return nearest;
    }
    return std::round(beat / step) * step;
}

void PianoRollComponent::zoomTimeline(const double factor,
                                      const double anchorBeat)
{
    const auto current = std::max(1.0, visibleBeats_);
    const auto maximum = std::max(4.0, contentBeats_ + 4.0);
    const auto next = juce::jlimit(
        1.0, maximum, current / std::max(0.05, factor));
    const auto ratio = juce::jlimit(
        0.0, 1.0, (anchorBeat - visibleStartBeat_) / current);
    visibleStartBeat_ = juce::jlimit(
        0.0, std::max(0.0, maximum - next),
        anchorBeat - ratio * next);
    visibleBeats_ = next;
    repaint();
    if (viewChangedHandler_)
        viewChangedHandler_(visibleStartBeat_,
                            visibleStartBeat_ + visibleBeats_);
}

void PianoRollComponent::panTimeline(const double deltaBeats)
{
    const auto maximum = std::max(4.0, contentBeats_ + 4.0);
    visibleStartBeat_ = juce::jlimit(
        0.0, std::max(0.0, maximum - visibleBeats_),
        visibleStartBeat_ + deltaBeats);
    repaint();
    if (viewChangedHandler_)
        viewChangedHandler_(visibleStartBeat_,
                            visibleStartBeat_ + visibleBeats_);
}

void PianoRollComponent::panPitches(const int noteDelta)
{
    visibleLowestNote_ = juce::jlimit(
        midiLowestNote,
        midiHighestNote - visibleNoteCount_ + 1,
        visibleLowestNote_ + noteDelta);
    repaint();
}

void PianoRollComponent::zoomPitches(const int noteDelta,
                                     const int anchorNote)
{
    const auto previousCount = visibleNoteCount_;
    const auto nextCount = juce::jlimit(
        12, 128, previousCount + noteDelta);
    const auto ratio = juce::jlimit(
        0.0, 1.0,
        static_cast<double>(anchorNote - visibleLowestNote_)
            / static_cast<double>(std::max(1, previousCount - 1)));
    visibleNoteCount_ = nextCount;
    visibleLowestNote_ = juce::jlimit(
        midiLowestNote,
        midiHighestNote - visibleNoteCount_ + 1,
        juce::roundToInt(static_cast<double>(anchorNote)
                         - ratio * static_cast<double>(
                                       visibleNoteCount_ - 1)));
    repaint();
}

void PianoRollComponent::updateSnapControls()
{
    if (!updatingSnapControls_)
        repaint();
}

void PianoRollComponent::normaliseClipBounds()
{
    clipLengthBeats_ = juce::jlimit(0.25, 86'400.0,
                                    clipLengthBeats_);
    playStartBeats_ = juce::jlimit(
        0.0, std::max(0.0, clipLengthBeats_ - 0.03125),
        playStartBeats_);
    playEndBeats_ = juce::jlimit(
        playStartBeats_ + 0.03125, clipLengthBeats_,
        playEndBeats_ > playStartBeats_ ? playEndBeats_
                                        : clipLengthBeats_);
    loopStartBeats_ = juce::jlimit(
        playStartBeats_,
        std::max(playStartBeats_, playEndBeats_ - 0.03125),
        loopStartBeats_);
    loopEndBeats_ = juce::jlimit(
        loopStartBeats_ + 0.03125, playEndBeats_,
        loopEndBeats_ > loopStartBeats_ ? loopEndBeats_
                                        : playEndBeats_);
}

void PianoRollComponent::refreshContentExtent(const bool preserveFullView)
{
    const auto previousContent = std::max(4.0, contentBeats_);
    const auto wasShowingFullClip = visibleStartBeat_ <= 0.000001
                                    && visibleBeats_
                                           >= previousContent - 0.000001;
    auto requiredBeats = std::max(4.0, clipLengthBeats_);
    for (const auto& note : notes_)
        requiredBeats = std::max(
            requiredBeats, note.startBeats + note.durationBeats);
    contentBeats_ = std::max(4.0,
        std::ceil(requiredBeats / 4.0) * 4.0);
    const auto maximum = std::max(4.0, contentBeats_ + 4.0);
    if (preserveFullView && wasShowingFullClip) {
        visibleStartBeat_ = 0.0;
        visibleBeats_ = contentBeats_;
    } else {
        visibleBeats_ = juce::jlimit(1.0, maximum, visibleBeats_);
        visibleStartBeat_ = juce::jlimit(
            0.0, std::max(0.0, maximum - visibleBeats_),
            visibleStartBeat_);
    }
}

void PianoRollComponent::applyClipBoundsFromEditors()
{
    if (updatingContext_ || updatingClipBoundsControls_
        || currentTrackId_ == 0 || currentClipId_ == 0)
        return;
    const auto parse = [](const juce::TextEditor& editor,
                          const double fallback) {
        const auto text = editor.getText().trim()
                              .replaceCharacter(',', '.');
        return text.isNotEmpty() ? text.getDoubleValue() : fallback;
    };
    clipLengthBeats_ = parse(durationEditor_, clipLengthBeats_);
    playStartBeats_ = parse(startEditor_, playStartBeats_);
    playEndBeats_ = parse(endEditor_, playEndBeats_);
    loopStartBeats_ = parse(loopStartEditor_, loopStartBeats_);
    loopEndBeats_ = parse(loopEndEditor_, loopEndBeats_);
    normaliseClipBounds();
    refreshContentExtent(true);
    refreshClipBoundsControls();
    publishClipBounds();
    repaint();
    if (viewChangedHandler_)
        viewChangedHandler_(visibleStartBeat_,
                            visibleStartBeat_ + visibleBeats_);
}

void PianoRollComponent::refreshClipBoundsControls()
{
    updatingClipBoundsControls_ = true;
    const auto format = [](const double beats) {
        return juce::String(beats, 3);
    };
    durationEditor_.setText(format(clipLengthBeats_), false);
    startEditor_.setText(format(playStartBeats_), false);
    endEditor_.setText(format(playEndBeats_), false);
    loopStartEditor_.setText(format(loopStartBeats_), false);
    loopEndEditor_.setText(format(loopEndBeats_), false);
    const auto clipAvailable = currentTrackId_ != 0 && currentClipId_ != 0
                               && !recording_;
    durationLabel_.setEnabled(clipAvailable);
    startLabel_.setEnabled(clipAvailable);
    endLabel_.setEnabled(clipAvailable);
    durationEditor_.setEnabled(clipAvailable);
    startEditor_.setEnabled(clipAvailable);
    endEditor_.setEnabled(clipAvailable);
    loopStartLabel_.setEnabled(clipAvailable && clipLooping_);
    loopEndLabel_.setEnabled(clipAvailable && clipLooping_);
    loopStartEditor_.setEnabled(clipAvailable && clipLooping_);
    loopEndEditor_.setEnabled(clipAvailable && clipLooping_);
    updatingClipBoundsControls_ = false;
}

PianoRollComponent::BoundHandle PianoRollComponent::boundHandleAt(
    const juce::Point<float> position) const noexcept
{
    if (position.x < keyboardWidth || position.y < toolbarHeight
        || position.y >= toolbarHeight + rulerHeight)
        return BoundHandle::none;
    const auto grid = gridBounds();
    const auto xFor = [this, &grid](const double beat) {
        return grid.getX()
               + static_cast<float>((beat - visibleStartBeat_)
                                    / std::max(0.000001, visibleBeats_))
                     * grid.getWidth();
    };
    const auto closest = [&position, &xFor](const double first,
                                            const BoundHandle firstHandle,
                                            const double second,
                                            const BoundHandle secondHandle) {
        const auto firstDistance = std::abs(position.x - xFor(first));
        const auto secondDistance = std::abs(position.x - xFor(second));
        if (std::min(firstDistance, secondDistance) > 10.0f)
            return BoundHandle::none;
        return firstDistance <= secondDistance ? firstHandle
                                               : secondHandle;
    };
    const auto band = static_cast<int>(
        (position.y - toolbarHeight) / (rulerHeight / 3.0f));
    if (band <= 0) {
        if (std::abs(position.x - xFor(clipLengthBeats_)) <= 10.0f)
            return BoundHandle::clipLength;
        return BoundHandle::none;
    }
    if (band == 1)
        return closest(playStartBeats_, BoundHandle::playStart,
                       playEndBeats_, BoundHandle::playEnd);
    if (clipLooping_)
        return closest(loopStartBeats_, BoundHandle::loopStart,
                       loopEndBeats_, BoundHandle::loopEnd);
    return BoundHandle::none;
}

void PianoRollComponent::dragBoundHandle(const BoundHandle handle,
                                         const double beat)
{
    switch (handle) {
        case BoundHandle::clipLength:
            clipLengthBeats_ = juce::jlimit(
                0.25, 86'400.0, std::max(0.25, beat));
            break;
        case BoundHandle::playStart:
            playStartBeats_ = juce::jlimit(
                0.0, std::max(0.0, playEndBeats_ - 0.03125), beat);
            break;
        case BoundHandle::playEnd:
            playEndBeats_ = juce::jlimit(
                playStartBeats_ + 0.03125, clipLengthBeats_, beat);
            break;
        case BoundHandle::loopStart:
            loopStartBeats_ = juce::jlimit(
                playStartBeats_,
                std::max(playStartBeats_, loopEndBeats_ - 0.03125), beat);
            break;
        case BoundHandle::loopEnd:
            loopEndBeats_ = juce::jlimit(
                loopStartBeats_ + 0.03125, playEndBeats_, beat);
            break;
        case BoundHandle::none:
            return;
    }
    normaliseClipBounds();
    refreshClipBoundsControls();
    repaint();
}

void PianoRollComponent::publishClipBounds()
{
    if (clipBoundsHandler_ && currentTrackId_ != 0 && currentClipId_ != 0)
        clipBoundsHandler_(clipLengthBeats_, playStartBeats_,
                           playEndBeats_, loopStartBeats_,
                           loopEndBeats_, clipLooping_);
}

} // namespace cydaw::app
