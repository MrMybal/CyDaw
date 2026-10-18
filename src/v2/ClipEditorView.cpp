#include "ClipEditorView.h"

#include "AudioEditorView.h"
#include "MasterClipEditor.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

bool isBlack(const int note)
{
    const auto pc = ((note % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

std::string noteName(const int note)
{
    static const char* namesFr[] = { "Do", "Do#", "Ré", "Ré#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si" };
    static const char* namesEn[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const auto pc = ((note % 12) + 12) % 12;
    char text[16];
    std::snprintf(text, sizeof(text), "%s%d", ui::french() ? namesFr[pc] : namesEn[pc], note / 12 - 1);
    return text;
}

const app::AudioTrackController::MidiClip* findMidiClip(const app::AudioTrackController& engine, const std::uint64_t trackId,
                                                       const std::uint64_t clipId)
{
    if (const auto* clips = engine.midiClipsView(trackId))
        for (const auto& clip : *clips)
            if (clip.id == clipId)
                return &clip;
    return nullptr;
}

const app::AudioTrackController::AudioClip* findAudioClip(const app::AudioTrackController& engine, const std::size_t index,
                                                         const std::uint64_t clipId)
{
    if (const auto* clips = engine.audioClipsView(index))
        for (const auto& clip : *clips)
            if (clip.id == clipId)
                return &clip;
    return nullptr;
}

// What Ctrl+C took: notes (their starts from the first's) and the room
// they take on the grid; velocities, in time order.
struct NoteClipboard {
    std::vector<app::AudioTrackController::MidiNote> notes;
    double span {};
};

NoteClipboard& noteClipboard()
{
    static NoteClipboard clipboard;
    return clipboard;
}

std::vector<float>& velocityClipboard()
{
    static std::vector<float> clipboard;
    return clipboard;
}

// The picked notes in time order (a chord low to high).
std::vector<app::AudioTrackController::MidiNote> pickedInOrder(const app::AudioTrackController::MidiClip& clip,
                                                                const std::set<std::uint64_t>& selection)
{
    std::vector<app::AudioTrackController::MidiNote> picked;
    for (const auto& note : clip.notes)
        if (selection.contains(note.id))
            picked.push_back(note);
    std::sort(picked.begin(), picked.end(), [](const auto& a, const auto& b) {
        return a.startBeats < b.startBeats || (a.startBeats == b.startBeats && a.noteNumber < b.noteNumber);
    });
    return picked;
}

// A note's curve value at a place along it (0..1), between its points, as
// the engine plays it (each way bent as its first point says).
float curveAt(const std::vector<app::AudioTrackController::NoteExpressionPoint>& points, const float position)
{
    return app::AudioTrackController::NoteExpressionPoint::valueAt(points, position);
}

// Where to read a curve again when it is merged with another: its points,
// and inside its bent ways enough places to keep their shape.
void curvePlaces(const std::vector<app::AudioTrackController::NoteExpressionPoint>& points, std::vector<float>& places)
{
    for (std::size_t index = 0; index < points.size(); ++index) {
        places.push_back(points[index].position);
        if (index + 1 < points.size() && std::abs(points[index].bend) > 1.0e-4f)
            for (auto step = 1; step < 12; ++step)
                places.push_back(points[index].position
                                 + (points[index + 1].position - points[index].position) * static_cast<float>(step) / 12.0f);
    }
}

} // namespace

ClipEditorView::ClipEditorView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host)
    : studio_(studio), state_(state), waveforms_(waveforms), host_(host)
{
}

ClipEditorView::~ClipEditorView() = default;

float ClipEditorView::beatToX(const double beat) const
{
    return gridMin_.x + static_cast<float>((beat - scrollBeats_) * pixelsPerBeat_ * ImGui::GetStyle().FontScaleDpi);
}

double ClipEditorView::xToBeat(const float x) const
{
    return scrollBeats_ + static_cast<double>(x - gridMin_.x) / (pixelsPerBeat_ * ImGui::GetStyle().FontScaleDpi);
}

float ClipEditorView::noteToY(const int note) const
{
    return gridMin_.y + static_cast<float>(127 - note) * dp(rowHeight_) - scrollY_;
}

int ClipEditorView::yToNote(const float y) const
{
    return std::clamp(127 - static_cast<int>(std::floor((y - gridMin_.y + scrollY_) / dp(rowHeight_))), 0, 127);
}

double ClipEditorView::snapBeat(const double beat) const
{
    if (!ui::snapping(state_.pianoRollSnap) || gridBeats_ <= 0.0)
        return beat;
    return std::round(beat / gridBeats_) * gridBeats_;
}

void ClipEditorView::playKey(const int note, const bool on)
{
    auto& keyboard = studio_.engine().keyboardState();
    if (on) {
        if (auditioning_ >= 0)
            keyboard.noteOff(1, auditioning_, 0.0f);
        keyboard.noteOn(1, note, defaultVelocity_);
        auditioning_ = note;
    } else if (auditioning_ >= 0) {
        keyboard.noteOff(1, auditioning_, 0.0f);
        auditioning_ = -1;
    }
}

void ClipEditorView::draw()
{
    auto& engine = studio_.engine();
    // A track's clip opened after the master's: that one; else the master's.
    if (state_.editedClip) {
        state_.editedMasterScene.reset();
    } else if (state_.editedMasterScene) {
        if (master_ == nullptr)
            master_ = std::make_unique<MasterClipEditor>(studio_, state_);
        master_->draw(*state_.editedMasterScene);
        return;
    }
    if (!state_.editedClip) {
        ui::emptyState(L("Aucun clip ouvert", "No clip open"),
                       L("Double-cliquez un clip de l'arrangement ou du lanceur, ou une piste instrument vide pour en créer un.",
                         "Double click a clip of the arrangement or the launcher, or an empty instrument lane to create one."));
        return;
    }
    const auto ref = *state_.editedClip;
    if (ref.launcherAudio()) {
        const auto clip = engine.launcherClip(ref.track.audioIndex, ref.scene);
        if (!studio_.isValid(ref.track) || !clip) {
            state_.editedClip.reset();
            return;
        }
        drawLauncherAudio(ref.track, ref.scene, *clip);
    } else if (ref.track.midi) {
        const auto* clip = findMidiClip(engine, ref.track.midiId, ref.clipId);
        if (clip == nullptr) {
            state_.editedClip.reset();
            return;
        }
        drawMidi(ref.track, *clip);
    } else {
        const auto* clip = findAudioClip(engine, ref.track.audioIndex, ref.clipId);
        if (clip == nullptr) {
            state_.editedClip.reset();
            return;
        }
        drawAudio(ref.track, *clip);
    }
}

// ---- MIDI ------------------------------------------------------------------------------

void ClipEditorView::drawMidiToolbar(const TrackRef& track, const MidiClip& clip)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto chip = ImGui::GetCursorScreenPos();
    const auto height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(chip.x, chip.y + height * 0.2f), ImVec2(chip.x + dp(4.0f), chip.y + height * 0.8f),
                                              theme::fromArgb(clip.colour != 0 ? clip.colour : 0xff4fd3aeu), dp(2.0f));
    ImGui::Dummy(ImVec2(dp(6.0f), height));
    ImGui::SameLine(0.0f, dp(4.0f));
    char name[96];
    std::snprintf(name, sizeof(name), "%s", clip.name.toRawUTF8());
    ImGui::SetNextItemWidth(dp(150.0f));
    if (ImGui::InputText("##clipName", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue))
        (void) engine.setMidiClipPresentation(track.midiId, clip.id, juce::String::fromUTF8(name), 0);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", studio_.trackName(track).toRawUTF8());

    ui::toolbarSeparator();
    ImGui::TextDisabled("%s", L("Longueur", "Length"));
    ImGui::SameLine();
    auto length = static_cast<float>(clip.lengthBeats);
    if (ui::dragNumber("##length", &length, 0.25f, 0.25f, 1024.0f, L("%.2f temps", "%.2f beats"), dp(96.0f))) {
        const auto beats = std::max(0.25, static_cast<double>(length));
        const auto loopEnd = clip.loopEndBeats > 0.0 ? std::min(clip.loopEndBeats, beats) : beats;
        (void) engine.setMidiClipTimelineBounds(track.midiId, clip.id, beats, std::min(clip.playStartBeats, beats - 0.25), beats,
                                                std::min(clip.loopStartBeats, loopEnd - 0.25), loopEnd, clip.looping);
        studio_.markDirty();
    }
    ImGui::SameLine();
    auto looping = clip.looping;
    if (ui::loopToggle("midiClipLoop", &looping, L("Boucle : le clip boucle", "Loop: the clip loops"))) {
        (void) engine.setMidiClipLooping(track.midiId, clip.id, looping);
        studio_.markDirty();
    }

    ui::toolbarSeparator();
    (void) ui::snapToggle("pianoRollSnap", &state_.pianoRollSnap);
    ImGui::SameLine(0.0f, dp(4.0f));
    ImGui::TextDisabled("%s", L("Grille", "Grid"));
    ImGui::SameLine();
    static constexpr double grids[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.0 };
    const char* gridNames[] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", L("Libre", "Off") };
    auto current = 2;
    for (auto index = 0; index < 7; ++index)
        if (std::abs(gridBeats_ - grids[index]) < 1.0e-6)
            current = index;
    ImGui::SetNextItemWidth(dp(70.0f));
    if (ImGui::Combo("##grid", &current, gridNames, 7))
        gridBeats_ = grids[current];
    ImGui::SameLine();
    ImGui::TextDisabled("%s", L("Vél.", "Vel."));
    ImGui::SameLine();
    auto velocity = defaultVelocity_ * 127.0f;
    if (ui::dragNumber("##velocity", &velocity, 0.5f, 1.0f, 127.0f, "%.0f", dp(46.0f)))
        defaultVelocity_ = velocity / 127.0f;
    ui::tooltip(L("Vélocité des nouvelles notes et du clavier", "Velocity of new notes and of the keyboard"));

    ui::toolbarSeparator();
    {
        const char* names[] = { L("Notes", "Notes"), L("Hauteur", "Pitch"), L("Volume", "Gain") };
        const char* tips[] = {
            L("Éditer les notes", "Edit the notes"),
            L("Dessiner la hauteur sur les notes (un demi-ton par rangée) : cliquez une note pour poser un point, "
              "glissez-le, double-cliquez pour l'ôter ; clic droit : formes (scoop, vibrato, chute…). "
              "Ctrl : demi-tons entiers, Maj : fin.",
              "Draw the pitch on the notes (a semitone a row): click a note to place a point, drag it, double click to "
              "remove it; right click: shapes (scoop, vibrato, fall…). Ctrl: whole semitones, Shift: fine."),
            L("Dessiner le volume des notes dans la bande du bas (dB) ; clic droit : formes (souffle, accent…).",
              "Draw the notes' gain in the lane below (dB); right click: shapes (swell, accent…).")
        };
        for (auto index = 0; index < 3; ++index) {
            if (index > 0)
                ImGui::SameLine(0.0f, dp(2.0f));
            auto on = static_cast<int>(expression_) == index;
            if (ui::toggle(ui::labelId(names[index], index == 0 ? "exprNotes" : index == 1 ? "exprPitch" : "exprGain").c_str(), &on,
                           p.accent, ImVec2(0.0f, 0.0f), tips[index]))
                expression_ = static_cast<Expression>(index);
        }
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::presets, L("Formes d'expression pour la sélection", "Expression shapes for the selection"))) {
        shapeTarget_ = 0;
        openShapes_ = true;
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::shuffle, L("Humaniser : petites variations de placement, vélocité et longueur",
                                        "Humanise: small changes of timing, velocity and length")))
        ImGui::OpenPopup("##humanise");

    ui::toolbarSeparator();
    ImGui::BeginDisabled(selection_.empty());
    if (ui::iconButton(icon::grid, L("Quantifier la sélection (Q)", "Quantise the selection (Q)"))) {
        (void) engine.beginMidiEdit(track.midiId, clip.id);
        for (const auto& note : clip.notes)
            if (selection_.contains(note.id))
                (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber,
                                             std::max(0.0, std::round(note.startBeats / std::max(1.0e-3, gridBeats_)) * gridBeats_),
                                             note.durationBeats, note.velocity);
        (void) engine.endMidiEdit();
        studio_.markDirty();
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::trash, L("Supprimer la sélection (Suppr)", "Delete the selection (Del)"))) {
        (void) engine.beginMidiEdit(track.midiId, clip.id);
        for (const auto id : selection_)
            (void) engine.removeMidiNote(track.midiId, clip.id, id);
        (void) engine.endMidiEdit();
        selection_.clear();
        studio_.markDirty();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%d %s", static_cast<int>(clip.notes.size()), L("notes", "notes"));
}

std::optional<double> ClipEditorView::noteField(const char* id, const int field, const std::uint64_t note, const double current,
                                                const float speed, const double minimum, const double maximum, const char* format,
                                                const float width)
{
    auto value = editField_ == field && editNote_ == note ? editBuffer_ : current;
    ImGui::SetNextItemWidth(width);
    ImGui::DragScalar(id, ImGuiDataType_Double, &value, speed, &minimum, &maximum, format);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        editField_ = -1;
        return std::clamp(value, minimum, maximum);
    }
    if (ImGui::IsItemActive()) {
        editField_ = field;
        editNote_ = note;
        editBuffer_ = value;
    } else if (editField_ == field && editNote_ == note) {
        editField_ = -1;
    }
    return std::nullopt;
}

// Under the toolbar, its row always there (the roll does not move when a
// note is picked): one note's values, typed or dragged; several, edited
// together, each change one step of the history.
void ClipEditorView::drawNoteBar(const TrackRef& track, const MidiClip& clip)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    std::vector<MidiNote> picked;
    for (const auto& note : clip.notes)
        if (selection_.contains(note.id))
            picked.push_back(note);
    std::sort(picked.begin(), picked.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeats < b.startBeats || (a.startBeats == b.startBeats && a.noteNumber < b.noteNumber);
    });
    ImGui::AlignTextToFramePadding();
    if (picked.empty()) {
        ImGui::TextDisabled("%s", L("Notes : choisissez-en (clic, cadre, Ctrl+A) pour lire et modifier leurs valeurs, plusieurs à la fois.",
                                    "Notes: pick some (click, frame, Ctrl+A) to read and edit their values, several at once."));
        return;
    }
    // Each picked note changed by `change` (it, its rank in time), as one
    // step of the history.
    const auto apply = [&](auto&& change) {
        (void) engine.beginMidiEdit(track.midiId, clip.id);
        for (std::size_t index = 0; index < picked.size(); ++index) {
            auto note = picked[index];
            change(note, index);
            (void) engine.updateMidiNote(track.midiId, clip.id, note.id, std::clamp(note.noteNumber, 0, 127), std::max(0.0, note.startBeats),
                                         std::max(1.0 / 64.0, note.durationBeats), std::clamp(note.velocity, 1.0f / 127.0f, 1.0f));
        }
        (void) engine.endMidiEdit();
        studio_.markDirty();
    };
    if (picked.size() == 1) {
        const auto note = picked.front();
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Note", "Note"));
        ImGui::SameLine();
        if (const auto key = noteField("##noteKey", 0, note.id, note.noteNumber, 0.15f, 0.0, 127.0, "%.0f", dp(54.0f)))
            apply([&](MidiNote& edited, std::size_t) { edited.noteNumber = static_cast<int>(std::lround(*key)); });
        ui::tooltip(L("Sa hauteur (numéro MIDI) : glisser, ou Ctrl+clic pour la taper", "Its pitch (MIDI number): drag, or Ctrl+click to type it"));
        ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::TextDisabled("%s", noteName(editField_ == 0 && editNote_ == note.id ? static_cast<int>(std::lround(editBuffer_)) : note.noteNumber).c_str());
        ImGui::SameLine(0.0f, dp(12.0f));
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Début", "Start"));
        ImGui::SameLine();
        if (const auto start = noteField("##noteStart", 1, note.id, note.startBeats, 0.01f, 0.0, 65'536.0, L("%.3f temps", "%.3f beats"), dp(100.0f)))
            apply([&](MidiNote& edited, std::size_t) { edited.startBeats = *start; });
        ImGui::SameLine(0.0f, dp(12.0f));
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Durée", "Length"));
        ImGui::SameLine();
        if (const auto length = noteField("##noteLength", 2, note.id, note.durationBeats, 0.01f, 1.0 / 64.0, 4'096.0,
                                          L("%.3f temps", "%.3f beats"), dp(100.0f)))
            apply([&](MidiNote& edited, std::size_t) { edited.durationBeats = *length; });
        ImGui::SameLine(0.0f, dp(12.0f));
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Vélocité", "Velocity"));
        ImGui::SameLine();
        if (const auto velocity = noteField("##noteVelocity", 3, note.id, std::round(note.velocity * 127.0f), 0.5f, 1.0, 127.0, "%.0f", dp(60.0f)))
            apply([&](MidiNote& edited, std::size_t) { edited.velocity = static_cast<float>(*velocity / 127.0); });
        ui::tooltip(L("De 1 à 127 : glisser, ou Ctrl+clic pour la taper", "From 1 to 127: drag, or Ctrl+click to type it"));
        return;
    }

    // Several: Bitwig's multi-edit.
    ImGui::TextColored(theme::vec(p.textDim), "%d %s", static_cast<int>(picked.size()), L("notes", "notes"));
    auto lowest = 127;
    auto highest = 0;
    auto meanVelocity = 0.0f;
    for (const auto& note : picked) {
        lowest = std::min(lowest, note.noteNumber);
        highest = std::max(highest, note.noteNumber);
        meanVelocity += note.velocity;
    }
    meanVelocity /= static_cast<float>(picked.size());
    ImGui::SameLine();
    ImGui::TextDisabled("%s-%s", noteName(lowest).c_str(), noteName(highest).c_str());
    ImGui::SameLine(0.0f, dp(10.0f));
    // Moved together, applied when let go.
    ImGui::SetNextItemWidth(dp(78.0f));
    ImGui::DragInt("##pitchShift", &pitchShift_, 0.1f, -48, 48, L("hauteur %+d", "pitch %+d"));
    ui::tooltip(L("Transpose toutes les notes (demi-tons) ; appliqué en lâchant", "Transposes every note (semitones); applied when let go"));
    if (ImGui::IsItemDeactivatedAfterEdit() && pitchShift_ != 0) {
        const auto shift = pitchShift_;
        apply([shift](MidiNote& note, std::size_t) { note.noteNumber += shift; });
        pitchShift_ = 0;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(86.0f));
    ImGui::DragFloat("##startShift", &startShift_, 0.01f, -64.0f, 64.0f, L("début %+.2f", "start %+.2f"));
    ui::tooltip(L("Déplace toutes les notes (temps) ; appliqué en lâchant", "Moves every note (beats); applied when let go"));
    if (ImGui::IsItemDeactivatedAfterEdit() && std::abs(startShift_) > 1.0e-6f) {
        const auto shift = static_cast<double>(startShift_);
        apply([shift](MidiNote& note, std::size_t) { note.startBeats += shift; });
        startShift_ = 0.0f;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(78.0f));
    ImGui::DragFloat("##lengthScale", &lengthScale_, 0.005f, 0.05f, 8.0f, L("durée x%.2f", "length x%.2f"));
    ui::tooltip(L("Allonge ou raccourcit toutes les notes ; appliqué en lâchant", "Lengthens or shortens every note; applied when let go"));
    if (ImGui::IsItemDeactivatedAfterEdit() && std::abs(lengthScale_ - 1.0f) > 1.0e-4f) {
        const auto scale = static_cast<double>(lengthScale_);
        apply([scale](MidiNote& note, std::size_t) { note.durationBeats *= scale; });
        lengthScale_ = 1.0f;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(dp(70.0f));
    ImGui::DragFloat("##velocityShift", &velocityShift_, 0.25f, -127.0f, 127.0f, L("vél. %+.0f", "vel. %+.0f"));
    ui::tooltip(L("Monte ou baisse toutes les vélocités ; appliqué en lâchant", "Raises or lowers every velocity; applied when let go"));
    if (ImGui::IsItemDeactivatedAfterEdit() && std::abs(velocityShift_) > 0.01f) {
        const auto shift = velocityShift_ / 127.0f;
        apply([shift](MidiNote& note, std::size_t) { note.velocity += shift; });
        velocityShift_ = 0.0f;
    }
    ImGui::SameLine(0.0f, dp(10.0f));
    // The velocities' spread around their middle.
    ImGui::SetNextItemWidth(dp(56.0f));
    ImGui::DragFloat("##velocityScale", &velocityScale_, 0.01f, 0.0f, 4.0f, "x%.2f");
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::SmallButton(L("Écart vél.", "Vel. spread"))) {
        const auto scale = velocityScale_;
        apply([=](MidiNote& note, std::size_t) { note.velocity = meanVelocity + (note.velocity - meanVelocity) * scale; });
    }
    ui::tooltip(L("Écarte (x>1) ou resserre (x<1) les vélocités autour de leur milieu", "Spreads (x>1) or narrows (x<1) the velocities around their middle"));
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::SetNextItemWidth(dp(48.0f));
    ImGui::DragFloat("##velocityRandom", &velocityRandom_, 0.25f, 0.0f, 127.0f, "%.0f");
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::SmallButton(L("Hasard vél.", "Random vel."))) {
        auto& random = juce::Random::getSystemRandom();
        const auto amount = velocityRandom_ / 127.0f;
        apply([&random, amount](MidiNote& note, std::size_t) { note.velocity += (random.nextFloat() * 2.0f - 1.0f) * amount; });
    }
    ui::tooltip(L("Ajoute à chaque vélocité un écart au hasard (sur 127)", "Adds a random offset to each velocity (out of 127)"));
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::SetNextItemWidth(dp(56.0f));
    ImGui::DragFloat("##timingRandom", &timingRandom_, 0.25f, 0.0f, 200.0f, "%.0f ms");
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::SmallButton(L("Hasard temps", "Random time"))) {
        auto& random = juce::Random::getSystemRandom();
        const auto beats = timingRandom_ / 1'000.0 / (60.0 / std::max(1.0, engine.tempo()));
        apply([&random, beats](MidiNote& note, std::size_t) { note.startBeats += (random.nextDouble() * 2.0 - 1.0) * beats; });
    }
    ui::tooltip(L("Décale chaque note d'un écart au hasard (millisecondes)", "Moves each note by a random offset (milliseconds)"));
    ImGui::SameLine(0.0f, dp(8.0f));
    ImGui::SetNextItemWidth(dp(48.0f));
    ImGui::DragFloat("##velocityTilt", &velocityTilt_, 0.25f, -127.0f, 127.0f, "%+.0f");
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ImGui::SmallButton(L("Rampe", "Ramp"))) {
        const auto first = picked.front().startBeats;
        const auto span = std::max(1.0e-9, picked.back().startBeats - first);
        const auto tilt = velocityTilt_ / 127.0f;
        apply([=](MidiNote& note, std::size_t) {
            note.velocity += tilt * static_cast<float>((note.startBeats - first) / span * 2.0 - 1.0);
        });
    }
    ui::tooltip(L("Incline les vélocités : le début baisse, la fin monte (crescendo), ou l'inverse",
                  "Tilts the velocities: the start goes down, the end up (crescendo), or the other way"));
    ImGui::SameLine(0.0f, dp(8.0f));
    if (ImGui::SmallButton(L("Inverser", "Flip")))
        apply([=](MidiNote& note, std::size_t) { note.velocity = 2.0f * meanVelocity - note.velocity; });
    ui::tooltip(L("Les fortes deviennent douces, autour de leur milieu", "Loud ones become soft, around their middle"));
    ImGui::SameLine(0.0f, dp(4.0f));
    if (ImGui::SmallButton("Legato")) {
        // Each note held to the next one's start (chords: to the next
        // start after theirs).
        apply([&picked](MidiNote& note, std::size_t) {
            for (const auto& other : picked)
                if (other.startBeats > note.startBeats + 1.0e-6) {
                    note.durationBeats = other.startBeats - note.startBeats;
                    return;
                }
        });
    }
    ui::tooltip(L("Chaque note tenue jusqu'au début de la suivante", "Each note held until the next one starts"));
}

void ClipEditorView::drawMidi(const TrackRef& track, const MidiClip& clip)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    auto& io = ImGui::GetIO();
    if (shownClip_ != clip.id) {
        shownClip_ = clip.id;
        selection_.clear();
        scrollBeats_ = -0.25;
        scrollY_ = -1.0f;
        (void) engine.setLiveMidiTrack(track.midiId);
    }
    drawMidiToolbar(track, clip);
    drawNoteBar(track, clip);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg0);
    const auto open = ImGui::BeginChild("##roll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return;
    }
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto size = ImGui::GetContentRegionAvail();
    // Wide enough for every white key's name.
    const auto keyboardWidth = dp(72.0f);
    // Over the bars, the band to grab (moves, zooms); between the keys and
    // the notes, the one for their height, as the eye goes from one to the
    // other.
    const auto navigatorHeight = dp(14.0f);
    const auto rulerHeight = navigatorHeight + dp(18.0f);
    const auto rowsBandWidth = dp(16.0f);
    const auto keysRight = origin.x + keyboardWidth;
    // The lane under the notes as tall as dragged, else its usual height.
    const auto splitter = dp(6.0f);
    const auto usualLane = expression_ == Expression::gain ? std::min(dp(150.0f), size.y * 0.36f) : std::min(dp(70.0f), size.y * 0.22f);
    const auto velocityHeight = state_.velocityLaneHeight > 0.0f
                                    ? std::clamp(dp(state_.velocityLaneHeight), dp(36.0f), std::max(dp(36.0f), size.y * 0.6f))
                                    : usualLane;
    gridMin_ = ImVec2(keysRight + rowsBandWidth, origin.y + rulerHeight);
    gridMax_ = ImVec2(origin.x + size.x, origin.y + size.y - velocityHeight - splitter);
    const ImVec2 laneMin(gridMin_.x, gridMax_.y + splitter);
    const ImVec2 laneMax(gridMax_.x, origin.y + size.y);
    const auto rowH = dp(rowHeight_);
    const auto spb = 60.0 / std::max(1.0, engine.tempo());
    const auto colour = theme::fromArgb(clip.colour != 0 ? clip.colour : 0xff4fd3aeu);

    // First view: the notes in the middle, or C4.
    if (scrollY_ < 0.0f) {
        auto centre = 60;
        if (!clip.notes.empty()) {
            auto low = 127;
            auto high = 0;
            for (const auto& note : clip.notes) {
                low = std::min(low, note.noteNumber);
                high = std::max(high, note.noteNumber);
            }
            centre = (low + high) / 2;
        }
        scrollY_ = std::max(0.0f, (127 - centre) * rowH - (gridMax_.y - gridMin_.y) * 0.5f);
        const auto visibleBeats = (gridMax_.x - gridMin_.x) / (pixelsPerBeat_ * ImGui::GetStyle().FontScaleDpi);
        if (clip.lengthBeats > visibleBeats * 0.95)
            pixelsPerBeat_ = std::clamp(static_cast<float>((gridMax_.x - gridMin_.x) / ImGui::GetStyle().FontScaleDpi
                                                           / (clip.lengthBeats + 0.5)),
                                        8.0f, 400.0f);
    }
    scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, 128.0f * rowH - (gridMax_.y - gridMin_.y)));

    // The row under the pointer (over the notes or the keys), shown along
    // both; the selected notes' keys.
    std::optional<int> pointedNote;
    if (io.MousePos.y >= gridMin_.y && io.MousePos.y < gridMax_.y && io.MousePos.x >= origin.x && io.MousePos.x < gridMax_.x
        && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        pointedNote = yToNote(io.MousePos.y);
    std::set<int> selectedPitches;
    for (const auto& note : clip.notes)
        if (selection_.contains(note.id))
            selectedPitches.insert(note.noteNumber);

    // Rows: the black keys' darker, a line and the name at each C.
    list->PushClipRect(gridMin_, gridMax_, true);
    for (auto note = 0; note < 128; ++note) {
        const auto y = noteToY(note);
        if (y + rowH < gridMin_.y || y > gridMax_.y)
            continue;
        list->AddRectFilled(ImVec2(gridMin_.x, y), ImVec2(gridMax_.x, y + rowH), isBlack(note) ? theme::mix(p.bg0, p.bg1, 0.1f)
                                                                                                : theme::mix(p.bg0, p.bg2, 0.75f));
        if (pointedNote && *pointedNote == note)
            list->AddRectFilled(ImVec2(gridMin_.x, y), ImVec2(gridMax_.x, y + rowH), theme::alpha(p.accent, 0.12f));
        if (note % 12 == 0)
            list->AddLine(ImVec2(gridMin_.x, y + rowH), ImVec2(gridMax_.x, y + rowH), p.lineStrong, dp(1.5f));
        else if (note % 12 == 5)
            list->AddLine(ImVec2(gridMin_.x, y + rowH), ImVec2(gridMax_.x, y + rowH), p.grid);
    }
    // Columns.
    const auto ppb = pixelsPerBeat_ * ImGui::GetStyle().FontScaleDpi;
    auto step = gridBeats_ > 0.0 ? gridBeats_ : 0.25;
    while (step * ppb < 6.0)
        step *= 2.0;
    for (auto beat = std::floor(xToBeat(gridMin_.x) / step) * step; beat < xToBeat(gridMax_.x); beat += step) {
        const auto x = beatToX(beat);
        const auto bar = std::abs(std::fmod(beat, studio_.engine().beatsPerBar())) < 1.0e-6;
        const auto whole = std::abs(beat - std::round(beat)) < 1.0e-6;
        list->AddLine(ImVec2(x, gridMin_.y), ImVec2(x, gridMax_.y), bar ? p.lineStrong : whole ? p.gridStrong : p.grid);
    }
    // Each C's name at the grid's left edge, where the notes are.
    if (rowH >= dp(7.0f)) {
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.68f);
        for (auto note = 0; note < 128; note += 12) {
            const auto y = noteToY(note);
            if (y + rowH < gridMin_.y || y > gridMax_.y)
                continue;
            list->AddText(ImVec2(gridMin_.x + dp(4.0f), y + (rowH - ImGui::GetFontSize()) * 0.5f), theme::alpha(p.textDim, 0.7f),
                          noteName(note).c_str());
        }
        ImGui::PopFont();
    }
    // Outside the clip, shaded; the loop, marked.
    const auto clipEnd = beatToX(clip.lengthBeats);
    list->AddRectFilled(ImVec2(clipEnd, gridMin_.y), gridMax_, IM_COL32(0, 0, 0, 90));
    list->AddRectFilled(gridMin_, ImVec2(beatToX(0.0), gridMax_.y), IM_COL32(0, 0, 0, 90));
    list->AddLine(ImVec2(clipEnd, gridMin_.y), ImVec2(clipEnd, gridMax_.y), theme::alpha(colour, 0.9f), dp(1.5f));

    // Input over the grid.
    ImGui::SetCursorScreenPos(gridMin_);
    ImGui::InvisibleButton("##grid", ImVec2(std::max(1.0f, gridMax_.x - gridMin_.x), std::max(1.0f, gridMax_.y - gridMin_.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const auto hovered = ImGui::IsItemHovered();
    const auto mouse = io.MousePos;
    const auto mouseBeat = xToBeat(mouse.x);
    const auto mouseNote = yToNote(mouse.y);
    gridHovered_ = hovered;
    mouseBeat_ = mouseBeat;
    const MidiNote* under = nullptr;
    auto onEdge = false;
    for (const auto& note : clip.notes) {
        const auto x0 = beatToX(note.startBeats);
        const auto x1 = std::max(x0 + 3.0f, beatToX(note.startBeats + note.durationBeats));
        const auto y0 = noteToY(note.noteNumber);
        if (mouse.x >= x0 && mouse.x < x1 && mouse.y >= y0 && mouse.y < y0 + rowH) {
            under = &note;
            onEdge = x1 - mouse.x < std::min(dp(7.0f), (x1 - x0) * 0.35f);
        }
    }

    const auto beginEdit = [&] { (void) engine.beginMidiEdit(track.midiId, clip.id); };
    const auto endEdit = [&] {
        (void) engine.endMidiEdit();
        studio_.markDirty();
    };

    // ---- Expressions: the curves on the notes ----
    // A pitch curve is drawn over its note, a semitone a row; a gain curve
    // in the lane below, -48..+12 dB.
    const auto curveOf = [](const MidiNote& note, const Expression kind) -> const std::vector<NotePoint>& {
        return kind == Expression::pitch ? note.pitch : note.gain;
    };
    const auto laneSpan = std::max(1.0f, laneMax.y - laneMin.y - dp(8.0f));
    const auto valueToY = [&](const MidiNote& note, const Expression kind, const float value) {
        if (kind == Expression::pitch)
            return noteToY(note.noteNumber) + rowH * 0.5f - value * rowH;
        return laneMax.y - dp(3.0f) - (value + 48.0f) / 60.0f * laneSpan;
    };
    const auto yToValue = [&](const MidiNote& note, const Expression kind, const float y) {
        if (kind == Expression::pitch) {
            // On whole semitones, as Bitwig does (a glide lands on a key);
            // Shift: free; Ctrl: by twentieths.
            auto value = (noteToY(note.noteNumber) + rowH * 0.5f - y) / rowH;
            value = io.KeyShift ? value : io.KeyCtrl ? std::round(value * 20.0f) / 20.0f : std::round(value);
            return std::clamp(value, -24.0f, 24.0f);
        }
        auto value = (laneMax.y - dp(3.0f) - y) / laneSpan * 60.0f - 48.0f;
        value = io.KeyCtrl ? std::round(value / 3.0f) * 3.0f : io.KeyShift ? value : std::round(value * 2.0f) / 2.0f;
        return std::clamp(value, -48.0f, 12.0f);
    };
    const auto setCurve = [&](const MidiNote& note, const Expression kind, std::vector<NotePoint> points) {
        (void) engine.setMidiNoteExpression(track.midiId, clip.id, note.id, kind == Expression::pitch ? points : note.pitch,
                                            kind == Expression::gain ? points : note.gain);
    };
    const auto noteById = [&](const std::uint64_t id) -> const MidiNote* {
        if (const auto* now = findMidiClip(engine, track.midiId, clip.id))
            for (const auto& note : now->notes)
                if (note.id == id)
                    return &note;
        return nullptr;
    };
    struct CurveHit {
        const MidiNote* note {};
        int point { -1 };
    };
    // The point under the pointer, else the curve: of the selection first.
    const auto hitCurve = [&](const Expression kind) {
        CurveHit hit;
        auto best = dp(7.0f);
        for (const auto& note : clip.notes) {
            const auto x0 = beatToX(note.startBeats);
            const auto x1 = beatToX(note.startBeats + note.durationBeats);
            if (x1 < gridMin_.x || x0 > gridMax_.x)
                continue;
            const auto& points = curveOf(note, kind);
            for (auto index = 0; index < static_cast<int>(points.size()); ++index) {
                const auto& point = points[static_cast<std::size_t>(index)];
                const ImVec2 at(x0 + point.position * (x1 - x0), valueToY(note, kind, point.value));
                const auto distance = std::hypot(at.x - mouse.x, at.y - mouse.y) - (selection_.contains(note.id) ? 1.5f : 0.0f);
                if (distance < best) {
                    best = distance;
                    hit = { &note, index };
                }
            }
        }
        if (hit.note != nullptr)
            return hit;
        best = kind == Expression::pitch ? std::max(rowH * 0.9f, dp(7.0f)) : dp(12.0f);
        for (const auto& note : clip.notes) {
            if (mouseBeat < note.startBeats || mouseBeat > note.startBeats + note.durationBeats)
                continue;
            const auto position = static_cast<float>((mouseBeat - note.startBeats) / std::max(1.0e-6, note.durationBeats));
            const auto distance = std::abs(mouse.y - valueToY(note, kind, curveAt(curveOf(note, kind), position)))
                                  - (selection_.contains(note.id) ? dp(4.0f) : 0.0f);
            if (distance < best) {
                best = distance;
                hit = { &note, -1 };
            }
        }
        return hit;
    };
    // A click on a point takes it, on a curve adds one there; a double click
    // on a point removes it.
    const auto pressCurve = [&](const Expression kind, const CurveHit& hit) {
        auto points = curveOf(*hit.note, kind);
        if (io.KeyAlt && hit.point < 0) {
            const auto position = static_cast<float>(
                std::clamp((mouseBeat - hit.note->startBeats) / std::max(1.0e-6, hit.note->durationBeats), 0.0, 1.0));
            for (std::size_t index = 0; index + 1 < points.size(); ++index)
                if (points[index].position <= position && position <= points[index + 1].position) {
                    beginEdit();
                    curveNote_ = hit.note->id;
                    curveKind_ = kind;
                    bendSegment_ = static_cast<int>(index);
                    bendFrom_ = points[index].bend;
                    bendY_ = mouse.y;
                    drag_ = Drag::bend;
                    break;
                }
            return;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (hit.point >= 0 && hit.point < static_cast<int>(points.size())) {
                beginEdit();
                points.erase(points.begin() + hit.point);
                setCurve(*hit.note, kind, std::move(points));
                endEdit();
            }
            return;
        }
        beginEdit();
        auto index = hit.point;
        if (index < 0) {
            const auto position = static_cast<float>(
                std::clamp((mouseBeat - hit.note->startBeats) / std::max(1.0e-6, hit.note->durationBeats), 0.0, 1.0));
            // A first point keeps the note where it is elsewhere.
            if (points.empty()) {
                points.push_back({ 0.0f, 0.0f });
                points.push_back({ 1.0f, 0.0f });
            }
            const auto at = std::upper_bound(points.begin(), points.end(), position,
                                             [](const float value, const NotePoint& point) { return value < point.position; });
            index = static_cast<int>(at - points.begin());
            points.insert(at, { position, yToValue(*hit.note, kind, mouse.y) });
            setCurve(*hit.note, kind, std::move(points));
        }
        curveNote_ = hit.note->id;
        curvePoint_ = index;
        curveKind_ = kind;
        drag_ = Drag::curve;
        if (!selection_.contains(hit.note->id) && !io.KeyShift)
            selection_ = { hit.note->id };
    };
    const auto dragCurve = [&] {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            endEdit();
            drag_ = Drag::none;
            return;
        }
        const auto* note = noteById(curveNote_);
        if (note == nullptr) {
            endEdit();
            drag_ = Drag::none;
            return;
        }
        auto points = curveOf(*note, curveKind_);
        if (curvePoint_ < 0 || curvePoint_ >= static_cast<int>(points.size()))
            return;
        const auto index = static_cast<std::size_t>(curvePoint_);
        const auto low = index > 0 ? points[index - 1].position + 0.001f : 0.0f;
        const auto high = index + 1 < points.size() ? points[index + 1].position - 0.001f : 1.0f;
        const auto position = static_cast<float>((xToBeat(mouse.x) - note->startBeats) / std::max(1.0e-6, note->durationBeats));
        points[index].position = std::clamp(position, std::min(low, high), std::max(low, high));
        points[index].value = yToValue(*note, curveKind_, mouse.y);
        if (points != curveOf(*note, curveKind_))
            setCurve(*note, curveKind_, std::move(points));
    };
    // A way's first point dragged with Alt: up bulges it up, whichever way
    // it goes.
    const auto dragBend = [&] {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            endEdit();
            drag_ = Drag::none;
            return;
        }
        const auto* note = noteById(curveNote_);
        if (note == nullptr) {
            endEdit();
            drag_ = Drag::none;
            return;
        }
        auto points = curveOf(*note, curveKind_);
        if (bendSegment_ < 0 || bendSegment_ + 1 >= static_cast<int>(points.size()))
            return;
        const auto index = static_cast<std::size_t>(bendSegment_);
        const auto rising = points[index + 1].value >= points[index].value ? 1.0f : -1.0f;
        points[index].bend = std::clamp(bendFrom_ + (bendY_ - mouse.y) / dp(70.0f) * rising, -1.0f, 1.0f);
        if (points != curveOf(*note, curveKind_))
            setCurve(*note, curveKind_, std::move(points));
    };
    const auto drawCurve = [&](const MidiNote& note, const Expression kind, const ImU32 lineColour, const float thickness,
                               const bool withPoints) {
        const auto& points = curveOf(note, kind);
        const auto x0 = beatToX(note.startBeats);
        const auto x1 = std::max(x0 + 2.0f, beatToX(note.startBeats + note.durationBeats));
        std::vector<ImVec2> line;
        line.reserve(points.size() + 2);
        line.emplace_back(x0, valueToY(note, kind, points.empty() ? 0.0f : points.front().value));
        for (std::size_t index = 0; index < points.size(); ++index) {
            const auto& point = points[index];
            line.emplace_back(x0 + point.position * (x1 - x0), valueToY(note, kind, point.value));
            // A bent way: drawn through its curve, as it plays.
            if (index + 1 < points.size() && std::abs(point.bend) > 1.0e-4f) {
                const auto& next = points[index + 1];
                for (auto step = 1; step < 16; ++step) {
                    const auto u = static_cast<float>(step) / 16.0f;
                    const auto position = point.position + (next.position - point.position) * u;
                    line.emplace_back(x0 + position * (x1 - x0),
                                      valueToY(note, kind, point.value + (next.value - point.value) * NotePoint::bent(u, point.bend)));
                }
            }
        }
        line.emplace_back(x1, valueToY(note, kind, points.empty() ? 0.0f : points.back().value));
        list->AddPolyline(line.data(), static_cast<int>(line.size()), lineColour, thickness);
        if (withPoints && points.size() < 64)
            for (std::size_t index = 0; index < points.size(); ++index) {
                const auto active = drag_ == Drag::curve && curveNote_ == note.id && curvePoint_ == static_cast<int>(index);
                const ImVec2 at(x0 + points[index].position * (x1 - x0), valueToY(note, kind, points[index].value));
                list->AddCircleFilled(at, active ? dp(4.5f) : dp(3.0f), active ? p.accent : lineColour);
            }
    };
    // While a point or a way is dragged: what it is, by the pointer (the
    // pitch in semitones and the key it lands on, the gain in dB, the bend).
    const auto dragLabel = [&] {
        if (drag_ != Drag::curve && drag_ != Drag::bend)
            return;
        const auto* note = noteById(curveNote_);
        if (note == nullptr)
            return;
        const auto& points = curveOf(*note, curveKind_);
        char text[48];
        if (drag_ == Drag::bend) {
            if (bendSegment_ < 0 || bendSegment_ >= static_cast<int>(points.size()))
                return;
            std::snprintf(text, sizeof(text), "%s %+.0f %%", L("Courbe", "Bend"), points[static_cast<std::size_t>(bendSegment_)].bend * 100.0f);
        } else {
            if (curvePoint_ < 0 || curvePoint_ >= static_cast<int>(points.size()))
                return;
            const auto value = points[static_cast<std::size_t>(curvePoint_)].value;
            if (curveKind_ == Expression::gain)
                std::snprintf(text, sizeof(text), "%+.1f dB", value);
            else if (std::abs(value - std::round(value)) < 1.0e-3f)
                std::snprintf(text, sizeof(text), "%+d  \xc2\xb7  %s", static_cast<int>(std::lround(value)),
                              noteName(note->noteNumber + static_cast<int>(std::lround(value))).c_str());
            else
                std::snprintf(text, sizeof(text), "%+.2f", value);
        }
        ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.85f);
        const auto size = ImGui::CalcTextSize(text);
        const ImVec2 at(mouse.x + dp(14.0f), mouse.y - size.y - dp(6.0f));
        auto* front = ImGui::GetForegroundDrawList();
        front->AddRectFilled(ImVec2(at.x - dp(4.0f), at.y - dp(2.0f)), ImVec2(at.x + size.x + dp(4.0f), at.y + size.y + dp(2.0f)), p.bg4, dp(3.0f));
        front->AddText(at, p.text, text);
        ImGui::PopFont();
    };
    const auto curveMode = expression_ == Expression::pitch;
    CurveHit pitchHit;
    if (curveMode && hovered && drag_ == Drag::none)
        pitchHit = hitCurve(Expression::pitch);

    // The wheel scrolls during a gesture too: a selection rectangle reaches
    // the notes out of view, notes are carried up or down.
    const auto gesture = drag_ == Drag::box || drag_ == Drag::move || drag_ == Drag::resize;
    if ((hovered || (gesture && ImGui::IsItemActive())) && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            const auto before = xToBeat(mouse.x);
            pixelsPerBeat_ = std::clamp(pixelsPerBeat_ * (io.MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f), 6.0f, 600.0f);
            scrollBeats_ = before - static_cast<double>(mouse.x - gridMin_.x) / (pixelsPerBeat_ * ImGui::GetStyle().FontScaleDpi);
        } else if (io.KeyShift) {
            scrollBeats_ -= io.MouseWheel * 40.0 / ppb;
        } else if (io.KeyAlt) {
            const auto noteAtMouse = yToNote(mouse.y);
            rowHeight_ = std::clamp(rowHeight_ + io.MouseWheel, 6.0f, 30.0f);
            scrollY_ = (127 - noteAtMouse) * dp(rowHeight_) - (mouse.y - gridMin_.y);
        } else {
            scrollY_ -= io.MouseWheel * rowH * 3.0f;
        }
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        scrollBeats_ -= io.MouseDelta.x / ppb;
        scrollY_ -= io.MouseDelta.y;
    }
    // A gesture held near an edge (or past it) scrolls the roll that way,
    // faster the further it goes.
    if (gesture && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const auto edge = dp(18.0f);
        const auto step = std::min(io.DeltaTime, 0.05f) * 60.0f;
        if (mouse.y < gridMin_.y + edge)
            scrollY_ -= (gridMin_.y + edge - mouse.y) * 0.35f * step;
        else if (mouse.y > gridMax_.y - edge)
            scrollY_ += (mouse.y - (gridMax_.y - edge)) * 0.35f * step;
        if (mouse.x < gridMin_.x + edge)
            scrollBeats_ -= (gridMin_.x + edge - mouse.x) * 0.35f * step / ppb;
        else if (mouse.x > gridMax_.x - edge)
            scrollBeats_ += (mouse.x - (gridMax_.x - edge)) * 0.35f * step / ppb;
        scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, 128.0f * rowH - (gridMax_.y - gridMin_.y)));
    }
    if (!curveMode && drag_ == Drag::none && hovered && under != nullptr && onEdge)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (curveMode && drag_ == Drag::none && hovered && pitchHit.note != nullptr)
        ImGui::SetMouseCursor(pitchHit.point >= 0 ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
    if (drag_ == Drag::resize)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
        velocityFocus_ = false;
    if (curveMode && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state_.focused = Panel::editor;
        dragStart_ = mouse;
        dragStartBeat_ = mouseBeat;
        dragStartY_ = mouse.y - gridMin_.y + scrollY_;
        if (pitchHit.note != nullptr && !io.KeyCtrl) {
            pressCurve(Expression::pitch, pitchHit);
        } else if (under != nullptr && io.KeyCtrl) {
            if (selection_.contains(under->id))
                selection_.erase(under->id);
            else
                selection_.insert(under->id);
        } else {
            if (!io.KeyCtrl && !io.KeyShift)
                selection_.clear();
            drag_ = Drag::box;
        }
    } else if (curveMode) {
        // The notes stay put while their curves are drawn.
    } else if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (under != nullptr) {
            beginEdit();
            (void) engine.removeMidiNote(track.midiId, clip.id, under->id);
            endEdit();
            selection_.erase(under->id);
            under = nullptr;
        } else {
            const auto start = std::max(0.0, gridBeats_ > 0.0 && ui::snapping(state_.pianoRollSnap) ? std::floor(mouseBeat / gridBeats_) * gridBeats_ : mouseBeat);
            beginEdit();
            const auto id = engine.addMidiNote(track.midiId, clip.id, mouseNote, start, lastLength_, defaultVelocity_);
            endEdit();
            if (id != 0)
                selection_ = { id };
            playKey(mouseNote, true);
        }
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state_.focused = Panel::editor;
        dragStart_ = mouse;
        dragStartBeat_ = mouseBeat;
        dragStartNote_ = mouseNote;
        dragStartY_ = mouse.y - gridMin_.y + scrollY_;
        if (under != nullptr) {
            if (io.KeyCtrl) {
                if (selection_.contains(under->id))
                    selection_.erase(under->id);
                else
                    selection_.insert(under->id);
            } else if (!selection_.contains(under->id)) {
                selection_ = { under->id };
            }
            dragNote_ = under->id;
            drag_ = onEdge ? Drag::resize : Drag::move;
            origins_.clear();
            for (const auto& note : clip.notes)
                if (selection_.contains(note.id))
                    origins_[note.id] = note;
            beginEdit();
            playKey(under->noteNumber, true);
        } else {
            if (!io.KeyCtrl && !io.KeyShift)
                selection_.clear();
            drag_ = Drag::box;
        }
    }
    // In an expression mode the right button offers the shapes; else it
    // erases what it passes over.
    if (expression_ != Expression::none && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        shapeTarget_ = under != nullptr ? under->id : pitchHit.note != nullptr ? pitchHit.note->id : 0;
        openShapes_ = true;
    }
    if (drag_ == Drag::curve && curveKind_ == Expression::pitch)
        dragCurve();
    if (drag_ == Drag::bend && curveKind_ == Expression::pitch)
        dragBend();
    if (curveKind_ == Expression::pitch)
        dragLabel();
    if (curveMode && hovered && drag_ == Drag::none && pitchHit.note != nullptr)
        ui::tooltip(L("Clic : un point (sur un point : le prendre) · glisser : sa hauteur, au demi-ton (Maj : libre) · Alt+glisser "
                      "entre deux points : courber · double-clic : retirer · clic droit : glisser vers la note suivante, vibrato…",
                      "Click: a point (on a point: take it) · drag: its pitch, by semitones (Shift: free) · Alt+drag between two "
                      "points: bend · double click: remove · right click: glide to the next note, vibrato…"));
    if (expression_ == Expression::none && hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right) && under != nullptr) {
        beginEdit();
        (void) engine.removeMidiNote(track.midiId, clip.id, under->id);
        endEdit();
        selection_.erase(under->id);
        under = nullptr;
    }

    if (drag_ == Drag::move || drag_ == Drag::resize) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto lead = origins_.find(dragNote_);
            if (lead != origins_.end() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                if (drag_ == Drag::move) {
                    auto deltaBeats = snapBeat(lead->second.startBeats + (mouseBeat - dragStartBeat_)) - lead->second.startBeats;
                    auto deltaNote = mouseNote - dragStartNote_;
                    for (const auto& [id, origin] : origins_) {
                        deltaBeats = std::max(deltaBeats, -origin.startBeats);
                        deltaNote = std::clamp(deltaNote, -origin.noteNumber, 127 - origin.noteNumber);
                    }
                    for (const auto& [id, origin] : origins_)
                        (void) engine.updateMidiNote(track.midiId, clip.id, id, origin.noteNumber + deltaNote,
                                                     origin.startBeats + deltaBeats, origin.durationBeats, origin.velocity);
                    if (auditioning_ != lead->second.noteNumber + deltaNote)
                        playKey(lead->second.noteNumber + deltaNote, true);
                } else {
                    const auto minimum = gridBeats_ > 0.0 ? gridBeats_ * 0.5 : 0.03;
                    const auto end = snapBeat(lead->second.startBeats + lead->second.durationBeats + (mouseBeat - dragStartBeat_));
                    const auto delta = end - (lead->second.startBeats + lead->second.durationBeats);
                    for (const auto& [id, origin] : origins_) {
                        const auto duration = std::max(minimum, origin.durationBeats + delta);
                        (void) engine.updateMidiNote(track.midiId, clip.id, id, origin.noteNumber, origin.startBeats, duration,
                                                     origin.velocity);
                        if (id == dragNote_)
                            lastLength_ = duration;
                    }
                }
            }
        } else {
            if (drag_ == Drag::move) {
                const auto lead = origins_.find(dragNote_);
                if (lead != origins_.end())
                    lastLength_ = lead->second.durationBeats;
            }
            endEdit();
            drag_ = Drag::none;
            playKey(0, false);
        }
    }
    if (drag_ == Drag::box) {
        // Its start where it was in the roll, however far it has scrolled.
        dragStart_ = ImVec2(beatToX(dragStartBeat_), gridMin_.y + dragStartY_ - scrollY_);
        const ImVec2 a(std::min(dragStart_.x, mouse.x), std::min(dragStart_.y, mouse.y));
        const ImVec2 b(std::max(dragStart_.x, mouse.x), std::max(dragStart_.y, mouse.y));
        if (b.x - a.x > 3.0f || b.y - a.y > 3.0f) {
            if (!io.KeyCtrl && !io.KeyShift)
                selection_.clear();
            for (const auto& note : clip.notes) {
                const auto x0 = beatToX(note.startBeats);
                const auto x1 = beatToX(note.startBeats + note.durationBeats);
                const auto y0 = noteToY(note.noteNumber);
                if (x1 >= a.x && x0 <= b.x && y0 + rowH >= a.y && y0 <= b.y)
                    selection_.insert(note.id);
            }
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            drag_ = Drag::none;
    }

    // Notes (read again: this frame may have changed them).
    const auto* current = findMidiClip(engine, track.midiId, clip.id);
    const auto& notes = current != nullptr ? current->notes : clip.notes;
    for (const auto& note : notes) {
        const auto x0 = beatToX(note.startBeats);
        const auto x1 = std::max(x0 + 3.0f, beatToX(note.startBeats + note.durationBeats));
        const auto y0 = noteToY(note.noteNumber);
        if (x1 < gridMin_.x || x0 > gridMax_.x || y0 + rowH < gridMin_.y || y0 > gridMax_.y)
            continue;
        const auto selected = selection_.contains(note.id);
        auto fill = theme::mix(theme::mix(colour, p.bg0 | 0xff000000u, 0.55f), colour, 0.35f + 0.65f * note.velocity);
        if (curveMode)
            fill = theme::alpha(fill, 0.45f);
        list->AddRectFilled(ImVec2(x0 + 0.5f, y0 + 1.0f), ImVec2(x1 - 0.5f, y0 + rowH - 1.0f), selected ? theme::brighter(fill, 0.25f) : fill,
                            dp(2.0f));
        // The note's gain along it, a thin line in its body.
        if (!note.gain.empty() && rowH > dp(8.0f)) {
            std::vector<ImVec2> line;
            const auto gainY = [&](const float db) { return y0 + rowH - 2.0f - (db + 48.0f) / 60.0f * (rowH - 4.0f); };
            line.emplace_back(x0, gainY(note.gain.front().value));
            for (const auto& point : note.gain)
                line.emplace_back(x0 + point.position * (x1 - x0), gainY(point.value));
            line.emplace_back(x1, gainY(note.gain.back().value));
            list->AddPolyline(line.data(), static_cast<int>(line.size()), theme::alpha(p.bg0, 0.55f), 1.0f);
        }
        list->AddRect(ImVec2(x0 + 0.5f, y0 + 1.0f), ImVec2(x1 - 0.5f, y0 + rowH - 1.0f), selected ? p.text : theme::alpha(p.bg0, 0.6f),
                      dp(2.0f), selected ? 1.5f : 1.0f);
        if (rowH > dp(9.0f) && x1 - x0 > dp(22.0f)) {
            ImGui::PushFont(theme::boldFont(), std::min(ui::currentFontBase() * 0.74f, rowH * 0.82f / ImGui::GetStyle().FontScaleDpi));
            list->PushClipRect(ImVec2(x0, y0), ImVec2(x1 - dp(2.0f), y0 + rowH), true);
            list->AddText(ImVec2(x0 + dp(3.0f), y0 + (rowH - ImGui::GetFontSize()) * 0.5f), IM_COL32(12, 12, 14, 225),
                          noteName(note.noteNumber).c_str());
            list->PopClipRect();
            ImGui::PopFont();
        }
    }
    // Pitch curves: drawn in pitch mode over every note; elsewhere only
    // where a note has one.
    for (const auto& note : notes) {
        const auto x0 = beatToX(note.startBeats);
        const auto x1 = beatToX(note.startBeats + note.durationBeats);
        if (x1 < gridMin_.x || x0 > gridMax_.x || (!curveMode && note.pitch.empty()))
            continue;
        const auto selected = selection_.contains(note.id);
        const auto hot = pitchHit.note != nullptr && pitchHit.note->id == note.id;
        if (curveMode)
            drawCurve(note, Expression::pitch, selected || hot ? p.text : theme::brighter(colour, 0.35f),
                      selected || hot ? dp(2.0f) : dp(1.4f), true);
        else
            drawCurve(note, Expression::pitch, theme::alpha(p.text, 0.55f), dp(1.2f), false);
    }
    // Moving notes: the lead one's name beside the pointer.
    if (drag_ == Drag::move && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
        for (const auto& note : notes)
            if (note.id == dragNote_) {
                ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.9f);
                const auto name = noteName(note.noteNumber);
                const auto size = ImGui::CalcTextSize(name.c_str());
                const ImVec2 at(mouse.x + dp(14.0f), mouse.y - size.y - dp(6.0f));
                list->AddRectFilled(ImVec2(at.x - dp(4.0f), at.y - dp(2.0f)), ImVec2(at.x + size.x + dp(4.0f), at.y + size.y + dp(2.0f)),
                                    p.bg4, dp(3.0f));
                list->AddText(at, p.text, name.c_str());
                ImGui::PopFont();
            }
    // Box.
    if (drag_ == Drag::box) {
        const ImVec2 a(std::min(dragStart_.x, mouse.x), std::min(dragStart_.y, mouse.y));
        const ImVec2 b(std::max(dragStart_.x, mouse.x), std::max(dragStart_.y, mouse.y));
        list->AddRectFilled(a, b, theme::alpha(p.accent, 0.08f));
        list->AddRect(a, b, theme::alpha(p.accent, 0.7f));
    }
    // Playhead: where the launcher plays this clip (a clip of the launcher,
    // from its own start, as a stop and a play start it again), or where
    // the arrangement does (a clip of the arrangement, while it plays).
    std::optional<double> playing;
    if (engine.isPlaying()) {
        if (clip.launcherScene >= 0) {
            if (studio_.playingScene(track) == clip.launcherScene)
                playing = clip.playStartBeats + engine.midiLauncherClipPositionSeconds(track.midiId) / spb;
        } else if (engine.arrangementPlaybackEnabled()) {
            playing = engine.position() / spb - clip.startBeats;
        }
        if (playing && clip.looping && clip.loopEndBeats > clip.loopStartBeats && *playing > clip.loopEndBeats)
            playing = clip.loopStartBeats + std::fmod(*playing - clip.loopStartBeats, clip.loopEndBeats - clip.loopStartBeats);
    }
    if (playing) {
        const auto local = *playing;
        if (local >= 0.0 && local <= clip.lengthBeats) {
            const auto x = beatToX(local);
            list->AddLine(ImVec2(x, gridMin_.y), ImVec2(x, gridMax_.y), p.play, dp(1.0f));
        }
    }
    // Hover hint: the note under the pointer.
    if (hovered && drag_ == Drag::none)
        list->AddRectFilled(ImVec2(gridMin_.x, noteToY(mouseNote)), ImVec2(gridMin_.x + dp(3.0f), noteToY(mouseNote) + rowH), p.accent);
    list->PopClipRect();

    // The bands: time over the ruler, the rows' height beside them.
    {
        auto position = scrollBeats_;
        auto zoom = static_cast<double>(ppb);
        if (ui::timelineBand("##timeBand", ImVec2(gridMin_.x, origin.y), ImVec2(gridMax_.x, origin.y + navigatorHeight), position, zoom,
                             6.0 * ImGui::GetStyle().FontScaleDpi, 600.0 * ImGui::GetStyle().FontScaleDpi, 1.0,
                             studio_.engine().beatsPerBar(), -1.0e6)) {
            // Double click: the whole clip.
            zoom = std::clamp((gridMax_.x - gridMin_.x) / (clip.lengthBeats + 0.5), 6.0 * ImGui::GetStyle().FontScaleDpi,
                              600.0 * ImGui::GetStyle().FontScaleDpi);
            position = -0.25;
        }
        scrollBeats_ = position;
        pixelsPerBeat_ = static_cast<float>(zoom / ImGui::GetStyle().FontScaleDpi);
        if (ui::rowsBand("##rowsBand", ImVec2(keysRight, gridMin_.y), ImVec2(gridMin_.x, gridMax_.y), scrollY_, rowHeight_, 6.0f, 30.0f,
                         true)) {
            rowHeight_ = 13.0f;
            scrollY_ = -1.0f;
        }
        if (ui::bandCorner("##bandCorner", ImVec2(keysRight, origin.y), ImVec2(gridMin_.x, origin.y + navigatorHeight))) {
            // The whole clip, the rows at their size round its notes.
            pixelsPerBeat_ = std::clamp(static_cast<float>((gridMax_.x - gridMin_.x) / (clip.lengthBeats + 0.5) / ImGui::GetStyle().FontScaleDpi),
                                        6.0f, 600.0f);
            scrollBeats_ = -0.25;
            rowHeight_ = 13.0f;
            scrollY_ = -1.0f;
        }
        list->AddRectFilled(ImVec2(keysRight, origin.y + navigatorHeight), ImVec2(gridMin_.x, gridMin_.y), p.bg1);
    }
    // Ruler.
    const auto rulerTop = origin.y + navigatorHeight;
    list->AddRectFilled(ImVec2(gridMin_.x, rulerTop), ImVec2(gridMax_.x, gridMin_.y), p.bg1);
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
    list->PushClipRect(ImVec2(gridMin_.x, rulerTop), ImVec2(gridMax_.x, gridMin_.y), true);
    auto labelStep = 1.0;
    while (labelStep * ppb < 34.0)
        labelStep *= 2.0;
    for (auto beat = std::floor(xToBeat(gridMin_.x) / labelStep) * labelStep; beat < xToBeat(gridMax_.x); beat += labelStep) {
        const auto x = beatToX(beat);
        char text[24];
        // Before the clip too, as the audio editor counts them: -1, -2…
        // (dimmer).
        const auto barBeats = std::max(1.0, static_cast<double>(studio_.engine().beatsPerBar()));
        const auto bar = static_cast<int>(std::floor(beat / barBeats + 1.0e-9));
        std::snprintf(text, sizeof(text), "%d.%d", bar >= 0 ? bar + 1 : bar, static_cast<int>(beat - bar * barBeats + 1.0e-9) + 1);
        list->AddLine(ImVec2(x, gridMin_.y - dp(5.0f)), ImVec2(x, gridMin_.y), p.textFaint);
        list->AddText(ImVec2(x + dp(2.0f), rulerTop + dp(2.0f)), beat < 0.0 ? p.textFaint : p.textDim, text);
    }
    list->PopClipRect();
    ImGui::PopFont();

    // Keyboard.
    list->PushClipRect(ImVec2(origin.x, gridMin_.y), ImVec2(keysRight, gridMax_.y), true);
    for (auto note = 0; note < 128; ++note) {
        const auto y = noteToY(note);
        if (y + rowH < gridMin_.y || y > gridMax_.y)
            continue;
        const auto black = isBlack(note);
        const auto pressed = auditioning_ == note;
        const auto pointed = pointedNote && *pointedNote == note;
        const auto picked = selectedPitches.contains(note);
        auto key = black ? IM_COL32(28, 30, 34, 255) : IM_COL32(220, 224, 230, 255);
        if (picked)
            key = theme::mix(key, p.accent, black ? 0.55f : 0.4f);
        if (pointed)
            key = theme::mix(key, p.accent, 0.7f);
        if (pressed)
            key = p.accent;
        list->AddRectFilled(ImVec2(origin.x, y), ImVec2(keysRight - (black ? dp(26.0f) : 1.0f), y + rowH - 1.0f), key, dp(2.0f),
                            ImDrawFlags_RoundCornersRight);
        // C's name always (bold); the other white keys' when there is room;
        // the pointed key's, black or white.
        const auto named = note % 12 == 0 || pointed || (!black && rowH >= dp(11.0f));
        if (named) {
            ImGui::PushFont(note % 12 == 0 || pointed ? theme::boldFont() : nullptr,
                            std::min(ui::currentFontBase() * 0.74f, std::max(rowH, dp(9.0f)) * 0.85f / ImGui::GetStyle().FontScaleDpi));
            const auto name = noteName(note);
            const auto width = ImGui::CalcTextSize(name.c_str()).x;
            const auto onBlack = black && !pointed && !pressed;
            list->AddText(ImVec2(keysRight - width - dp(5.0f), y + (rowH - ImGui::GetFontSize()) * 0.5f),
                          onBlack ? IM_COL32(230, 232, 236, 255)
                          : note % 12 == 0 || pointed ? IM_COL32(20, 22, 26, 255)
                                                      : IM_COL32(90, 96, 106, 255),
                          name.c_str());
            ImGui::PopFont();
        }
    }
    list->PopClipRect();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, gridMin_.y));
    ImGui::InvisibleButton("##keys", ImVec2(keyboardWidth, std::max(1.0f, gridMax_.y - gridMin_.y)));
    if (ImGui::IsItemActive()) {
        const auto note = yToNote(io.MousePos.y);
        if (note != auditioning_)
            playKey(note, true);
    } else if (ImGui::IsItemDeactivated()) {
        playKey(0, false);
    }
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f)
        scrollY_ -= io.MouseWheel * rowH * 3.0f;

    // The bar between the notes and the lane: dragged, the lane taller or
    // shorter (a double click: its usual height).
    {
        const ImVec2 barMin(origin.x, gridMax_.y);
        ImGui::SetCursorScreenPos(barMin);
        ImGui::InvisibleButton("##laneSplitter", ImVec2(std::max(1.0f, size.x), splitter));
        const auto hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (hot)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive() && io.MouseDelta.y != 0.0f)
            state_.velocityLaneHeight = std::max(20.0f, (velocityHeight - io.MouseDelta.y) / dp(1.0f));
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            state_.velocityLaneHeight = 0.0f;
        ui::tooltip(L("Glisser : agrandir ou réduire le couloir du bas · double-clic : sa taille de base",
                      "Drag: make the lane below taller or shorter · double click: its usual size"));
        const auto y = barMin.y + splitter * 0.5f;
        list->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y), hot ? p.accent : p.line, hot ? dp(2.0f) : dp(1.0f));
        for (auto dot = -2; dot <= 2; ++dot)
            list->AddCircleFilled(ImVec2(gridMin_.x + (gridMax_.x - gridMin_.x) * 0.5f + static_cast<float>(dot) * dp(6.0f), y), dp(1.5f),
                                  hot ? p.accent : p.textFaint);
    }

    // Velocity lane (the gain lane in that mode).
    list->AddRectFilled(ImVec2(origin.x, laneMin.y), laneMax, p.bg1);
    if (expression_ == Expression::gain) {
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.72f);
        list->AddText(ImVec2(origin.x + dp(6.0f), laneMin.y + dp(4.0f)), p.textFaint, L("VOLUME", "GAIN"));
        for (const auto db : { 12.0f, 0.0f, -12.0f, -24.0f, -36.0f }) {
            const auto y = laneMax.y - dp(3.0f) - (db + 48.0f) / 60.0f * laneSpan;
            char text[16];
            std::snprintf(text, sizeof(text), "%+.0f", db);
            list->AddText(ImVec2(laneMin.x - dp(24.0f), y - ImGui::GetFontSize() * 0.5f), p.textFaint, text);
            list->AddLine(ImVec2(laneMin.x, y), ImVec2(laneMax.x, y), db == 0.0f ? p.gridStrong : p.grid);
        }
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(laneMin);
        ImGui::InvisibleButton("##gainLane", ImVec2(std::max(1.0f, laneMax.x - laneMin.x), std::max(1.0f, laneMax.y - laneMin.y)),
                               ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const auto laneHovered = ImGui::IsItemHovered();
        CurveHit gainHit;
        if (laneHovered && drag_ == Drag::none)
            gainHit = hitCurve(Expression::gain);
        if (laneHovered && gainHit.note != nullptr)
            ImGui::SetMouseCursor(gainHit.point >= 0 ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
        if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && gainHit.note != nullptr) {
            state_.focused = Panel::editor;
            pressCurve(Expression::gain, gainHit);
        }
        if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            shapeTarget_ = gainHit.note != nullptr ? gainHit.note->id : 0;
            openShapes_ = true;
        }
        if (drag_ == Drag::curve && curveKind_ == Expression::gain)
            dragCurve();
        if (drag_ == Drag::bend && curveKind_ == Expression::gain)
            dragBend();
        if (curveKind_ == Expression::gain)
            dragLabel();
        const auto* now = findMidiClip(engine, track.midiId, clip.id);
        list->PushClipRect(laneMin, laneMax, true);
        for (const auto& note : now != nullptr ? now->notes : notes) {
            const auto x0 = beatToX(note.startBeats);
            const auto x1 = beatToX(note.startBeats + note.durationBeats);
            if (x1 < laneMin.x || x0 > laneMax.x)
                continue;
            const auto selected = selection_.contains(note.id);
            const auto hot = gainHit.note != nullptr && gainHit.note->id == note.id;
            drawCurve(note, Expression::gain,
                      selected || hot ? p.text : note.gain.empty() ? theme::alpha(colour, 0.45f) : theme::brighter(colour, 0.3f),
                      selected || hot ? dp(2.0f) : dp(1.3f), true);
        }
        list->PopClipRect();
        ImGui::EndChild();
        drawMidiPopups(track, clip);
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            handleMidiKeys(track, clip);
        return;
    }
    drawVelocityLane(track, clip, laneMin, laneMax, colour);

    ImGui::EndChild();
    drawMidiPopups(track, clip);
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        handleMidiKeys(track, clip);
}

// ---- The velocity lane ----------------------------------------------------------------------

void ClipEditorView::report(const juce::String& text)
{
    state_.status = text;
    state_.statusIsError = false;
    state_.statusTime = ImGui::GetTime();
}

void ClipEditorView::drawVelocityLane(const TrackRef& track, const MidiClip& clip, const ImVec2 laneMin, const ImVec2 laneMax,
                                      const ImU32 colour)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    auto& io = ImGui::GetIO();
    auto* list = ImGui::GetWindowDrawList();
    const auto mouse = io.MousePos;
    // 127 at the top, nothing at the bottom.
    const auto top = laneMin.y + dp(8.0f);
    const auto bottom = laneMax.y - dp(2.0f);
    const auto span = std::max(1.0f, bottom - top);
    const auto yOf = [&](const float velocity) { return bottom - span * velocity; };
    const auto stalkX = [&](const MidiNote& note) { return beatToX(note.startBeats) + dp(1.5f); };
    const auto readable = [](const float velocity) { return static_cast<int>(std::lround(velocity * 127.0f)); };
    const auto* current = findMidiClip(engine, track.midiId, clip.id);
    const auto notes = current != nullptr ? current->notes : clip.notes;

    // Its name, at the foot of the keys' column; its scale beside the lane.
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.68f);
    list->AddText(ImVec2(laneMin.x - dp(66.0f), laneMax.y - ImGui::GetFontSize() - dp(4.0f)), p.textFaint, L("VÉLOCITÉ", "VELOCITY"));
    for (const auto value : { 127, 96, 64, 32 }) {
        const auto y = std::round(yOf(static_cast<float>(value) / 127.0f));
        list->AddLine(ImVec2(laneMin.x, y), ImVec2(laneMax.x, y), value == 64 ? p.gridStrong : p.grid);
        char text[8];
        std::snprintf(text, sizeof(text), "%d", value);
        list->AddText(ImVec2(laneMin.x - dp(4.0f) - ImGui::CalcTextSize(text).x, y - ImGui::GetFontSize() * 0.5f), p.textFaint, text);
    }
    ImGui::PopFont();

    ImGui::SetCursorScreenPos(laneMin);
    ImGui::InvisibleButton("##velocity", ImVec2(std::max(1.0f, laneMax.x - laneMin.x), std::max(1.0f, laneMax.y - laneMin.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const auto laneHovered = ImGui::IsItemHovered();
    // The point under the pointer: the stalk nearest it (of a chord's, the
    // head nearest the pointer, a picked one first).
    const MidiNote* hit = nullptr;
    if (laneHovered && velocityDrag_ == VelocityDrag::none) {
        auto best = 1.0e9f;
        for (const auto& note : notes) {
            const auto dx = std::abs(stalkX(note) - mouse.x);
            if (dx > dp(5.0f))
                continue;
            const auto score = dx + std::abs(yOf(note.velocity) - mouse.y) * 0.2f - (selection_.contains(note.id) ? 1.0f : 0.0f);
            if (score < best) {
                best = score;
                hit = &note;
            }
        }
    }
    const auto setVelocity = [&](const MidiNote& note, const float velocity) {
        (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber, note.startBeats, note.durationBeats,
                                     std::clamp(velocity, 1.0f / 127.0f, 1.0f));
    };
    // One step of the history, from the first change.
    const auto startChange = [&] {
        if (!velocityMoved_) {
            (void) engine.beginMidiEdit(track.midiId, clip.id);
            velocityMoved_ = true;
        }
    };
    const auto endChange = [&] {
        if (velocityMoved_) {
            (void) engine.endMidiEdit();
            studio_.markDirty();
        }
        velocityMoved_ = false;
        velocityDrag_ = VelocityDrag::none;
    };

    if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state_.focused = Panel::editor;
        velocityFocus_ = true;
        velocityFrom_ = mouse;
        velocityLast_ = mouse;
        velocityFromBeat_ = xToBeat(mouse.x);
        velocityMoved_ = false;
        velocityDelta_ = 0.0f;
        velocityPressed_ = 0;
        if (io.KeyAlt) {
            velocityDrag_ = VelocityDrag::paint;
        } else if (hit != nullptr) {
            if (io.KeyCtrl) {
                if (selection_.contains(hit->id))
                    selection_.erase(hit->id);
                else
                    selection_.insert(hit->id);
            } else if (io.KeyShift) {
                selection_.insert(hit->id);
            } else if (!selection_.contains(hit->id)) {
                selection_ = { hit->id };
            }
            velocityPressed_ = hit->id;
            if (selection_.contains(hit->id)) {
                velocityDrag_ = VelocityDrag::move;
                velocityOrigins_.clear();
                for (const auto& note : notes)
                    if (selection_.contains(note.id))
                        velocityOrigins_[note.id] = note;
            }
        } else {
            if (!io.KeyCtrl && !io.KeyShift)
                selection_.clear();
            velocityBase_ = selection_;
            velocityDrag_ = VelocityDrag::frame;
        }
    }
    if (laneHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        state_.focused = Panel::editor;
        velocityFocus_ = true;
        if (hit != nullptr && !selection_.contains(hit->id))
            selection_ = { hit->id };
        ImGui::OpenPopup("##velocityMenu");
    }

    switch (velocityDrag_) {
    case VelocityDrag::move:
        // The picked go up or down together, each from where it was.
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            velocityDelta_ += -io.MouseDelta.y / span * (io.KeyShift ? 0.2f : 1.0f);
            if (velocityMoved_ || ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                startChange();
                for (const auto& [id, origin] : velocityOrigins_)
                    setVelocity(origin, origin.velocity + velocityDelta_);
            }
        } else {
            // A click without a move on one of several picked: that one alone.
            if (!velocityMoved_ && !io.KeyCtrl && !io.KeyShift && velocityPressed_ != 0)
                selection_ = { velocityPressed_ };
            endChange();
        }
        break;
    case VelocityDrag::paint:
        // Alt: drawn over, each stalk the pointer passes takes its height
        // (only the picked ones when some are).
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const auto from = std::min(velocityLast_.x, mouse.x) - dp(3.0f);
            const auto to = std::max(velocityLast_.x, mouse.x) + dp(3.0f);
            for (const auto& note : notes) {
                const auto x = stalkX(note);
                if (x < from || x > to || (!selection_.empty() && !selection_.contains(note.id)))
                    continue;
                const auto along = std::abs(mouse.x - velocityLast_.x) > 0.5f
                                       ? std::clamp((x - velocityLast_.x) / (mouse.x - velocityLast_.x), 0.0f, 1.0f)
                                       : 1.0f;
                const auto value = std::clamp((bottom - (velocityLast_.y + (mouse.y - velocityLast_.y) * along)) / span, 1.0f / 127.0f, 1.0f);
                if (std::abs(value - note.velocity) > 1.0e-4f) {
                    startChange();
                    setVelocity(note, value);
                }
            }
            velocityLast_ = mouse;
        } else {
            endChange();
        }
        break;
    case VelocityDrag::frame: {
        // Its start where it was in time, however far the view has moved.
        const ImVec2 from(beatToX(velocityFromBeat_), velocityFrom_.y);
        const ImVec2 a(std::min(from.x, mouse.x), std::min(from.y, mouse.y));
        const ImVec2 b(std::max(from.x, mouse.x), std::max(from.y, mouse.y));
        if (b.x - a.x > 3.0f || b.y - a.y > 3.0f) {
            selection_ = velocityBase_;
            for (const auto& note : notes) {
                const auto x = stalkX(note);
                if (x >= a.x && x <= b.x && yOf(note.velocity) - dp(3.0f) <= b.y && bottom >= a.y)
                    selection_.insert(note.id);
            }
            list->AddRectFilled(a, b, theme::alpha(p.accent, 0.08f));
            list->AddRect(a, b, theme::alpha(p.accent, 0.7f));
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            velocityDrag_ = VelocityDrag::none;
        break;
    }
    case VelocityDrag::none: break;
    }
    if (hit != nullptr)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    // The points as they are now: the picked over the others.
    const auto* now = findMidiClip(engine, track.midiId, clip.id);
    const auto& shown = now != nullptr ? now->notes : notes;
    list->PushClipRect(laneMin, laneMax, true);
    struct Label {
        float x {};
        float head {};
        int value {};
        bool hot {};
    };
    std::vector<Label> labels;
    for (const auto pass : { false, true })
        for (const auto& note : shown) {
            const auto selected = selection_.contains(note.id);
            if (selected != pass)
                continue;
            const auto x = stalkX(note);
            if (x < laneMin.x - dp(6.0f) || x > laneMax.x + dp(6.0f))
                continue;
            const auto head = yOf(note.velocity);
            const auto hot = hit != nullptr && hit->id == note.id;
            const auto ink = selected ? p.accent : hot ? theme::brighter(colour, 0.35f) : colour;
            list->AddLine(ImVec2(x, bottom), ImVec2(x, head), ink, selected ? dp(2.5f) : dp(2.0f));
            list->AddCircleFilled(ImVec2(x, head), selected || hot ? dp(4.0f) : dp(3.0f), ink);
            if (selected)
                list->AddCircle(ImVec2(x, head), dp(4.5f), p.text, 0, 1.2f);
            if (selected || hot)
                labels.push_back({ x, head, readable(note.velocity), hot });
        }
    // Their values over them, where there is room (the one under the pointer
    // always).
    std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.x < b.x; });
    ImGui::PushFont(theme::boldFont(), ui::currentFontBase() * 0.68f);
    auto lastRight = -1.0e9f;
    for (const auto& label : labels) {
        char text[8];
        std::snprintf(text, sizeof(text), "%d", label.value);
        const auto size = ImGui::CalcTextSize(text);
        auto at = ImVec2(label.x - size.x * 0.5f, label.head - size.y - dp(5.0f));
        if (at.y < laneMin.y + dp(1.0f))
            at = ImVec2(label.x + dp(7.0f), label.head - size.y * 0.5f);
        if (!label.hot && at.x < lastRight + dp(2.0f))
            continue;
        list->AddRectFilled(ImVec2(at.x - dp(2.0f), at.y - dp(1.0f)), ImVec2(at.x + size.x + dp(2.0f), at.y + size.y + dp(1.0f)),
                            theme::alpha(p.bg0, 0.85f), dp(2.0f));
        list->AddText(at, label.hot ? p.text : p.accent, text);
        lastRight = at.x + size.x;
    }
    ImGui::PopFont();
    list->PopClipRect();
    if (hit != nullptr && velocityDrag_ == VelocityDrag::none && !ImGui::IsPopupOpen("##velocityMenu"))
        ImGui::SetTooltip("%s  \xc2\xb7  %s %d", noteName(hit->noteNumber).c_str(), L("vélocité", "velocity"), readable(hit->velocity));
    else if (laneHovered && hit == nullptr)
        ui::tooltip(L("Clic : choisir un point (Ctrl, Maj : plusieurs ; cadre dans le vide). Glisser : monter ou baisser les choisis "
                      "(Maj : finement). Alt+glisser : dessiner. Ctrl+C / Ctrl+V : copier, coller leurs valeurs. Clic droit : menu.",
                      "Click: pick a point (Ctrl, Shift: several; a frame in the empty lane). Drag: raise or lower the picked "
                      "(Shift: finely). Alt+drag: draw. Ctrl+C / Ctrl+V: copy, paste their values. Right click: menu."));

    if (ImGui::BeginPopup("##velocityMenu")) {
        const auto picked = std::count_if(shown.begin(), shown.end(), [this](const MidiNote& note) { return selection_.contains(note.id); });
        ImGui::TextDisabled("%d %s", static_cast<int>(picked), L("point(s) choisi(s)", "point(s) picked"));
        if (ImGui::MenuItem(L("Copier les vélocités", "Copy the velocities"), "Ctrl+C", false, picked > 0) && current != nullptr)
            copyVelocities(*current);
        if (ImGui::MenuItem(L("Coller les vélocités", "Paste the velocities"), "Ctrl+V", false, picked > 0 && !velocityClipboard().empty())
            && current != nullptr)
            pasteVelocities(track, *current);
        if (ImGui::MenuItem(L("Tout choisir", "Pick all"), "Ctrl+A"))
            for (const auto& note : shown)
                selection_.insert(note.id);
        ImGui::Separator();
        if (ImGui::MenuItem(L("Monter de 10", "Raise by 10"), L("Maj+Haut", "Shift+Up"), false, picked > 0) && current != nullptr)
            nudgeVelocities(track, *current, 10.0f);
        if (ImGui::MenuItem(L("Baisser de 10", "Lower by 10"), L("Maj+Bas", "Shift+Down"), false, picked > 0) && current != nullptr)
            nudgeVelocities(track, *current, -10.0f);
        ImGui::EndPopup();
    }
}

void ClipEditorView::copyVelocities(const MidiClip& clip)
{
    const auto picked = pickedInOrder(clip, selection_);
    if (picked.empty())
        return;
    auto& values = velocityClipboard();
    values.clear();
    for (const auto& note : picked)
        values.push_back(note.velocity);
    report(juce::String(static_cast<int>(values.size())) + ui::Ls(" vélocités copiées", " velocities copied"));
}

void ClipEditorView::pasteVelocities(const TrackRef& track, const MidiClip& clip)
{
    const auto& values = velocityClipboard();
    const auto picked = pickedInOrder(clip, selection_);
    if (values.empty() || picked.empty())
        return;
    // In time order, the copied values again from the first when there are
    // fewer of them.
    auto& engine = studio_.engine();
    (void) engine.beginMidiEdit(track.midiId, clip.id);
    for (std::size_t index = 0; index < picked.size(); ++index) {
        const auto& note = picked[index];
        (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber, note.startBeats, note.durationBeats,
                                     std::clamp(values[index % values.size()], 1.0f / 127.0f, 1.0f));
    }
    (void) engine.endMidiEdit();
    studio_.markDirty();
    report(juce::String(static_cast<int>(picked.size())) + ui::Ls(" vélocités collées", " velocities pasted"));
}

void ClipEditorView::nudgeVelocities(const TrackRef& track, const MidiClip& clip, const float amount)
{
    const auto picked = pickedInOrder(clip, selection_);
    if (picked.empty())
        return;
    auto& engine = studio_.engine();
    (void) engine.beginMidiEdit(track.midiId, clip.id);
    for (const auto& note : picked)
        (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber, note.startBeats, note.durationBeats,
                                     std::clamp(note.velocity + amount / 127.0f, 1.0f / 127.0f, 1.0f));
    (void) engine.endMidiEdit();
    studio_.markDirty();
}

void ClipEditorView::copyNotes(const MidiClip& clip)
{
    auto picked = pickedInOrder(clip, selection_);
    if (picked.empty())
        return;
    auto first = picked.front().startBeats;
    auto last = first;
    for (const auto& note : picked)
        last = std::max(last, note.startBeats + note.durationBeats);
    for (auto& note : picked)
        note.startBeats -= first;
    const auto grid = gridBeats_ > 0.0 ? gridBeats_ : 0.25;
    auto& copied = noteClipboard();
    copied.notes = std::move(picked);
    copied.span = std::max(grid, std::ceil((last - first) / grid - 1.0e-9) * grid);
    pasteNext_ = first + copied.span;
    report(juce::String(static_cast<int>(copied.notes.size())) + ui::Ls(" notes copiées", " notes copied"));
}

void ClipEditorView::pasteNotes(const TrackRef& track, const MidiClip& clip)
{
    const auto& copied = noteClipboard();
    if (copied.notes.empty())
        return;
    // Under the pointer (on the grid's line before it), else after the
    // last ones copied or pasted.
    auto at = pasteNext_;
    if (gridHovered_)
        at = gridBeats_ > 0.0 && ui::snapping(state_.pianoRollSnap) ? std::floor(mouseBeat_ / gridBeats_) * gridBeats_ : mouseBeat_;
    at = std::max(0.0, at);
    auto& engine = studio_.engine();
    std::set<std::uint64_t> pasted;
    (void) engine.beginMidiEdit(track.midiId, clip.id);
    for (const auto& note : copied.notes)
        if (const auto id = engine.copyMidiNote(track.midiId, clip.id, note, at + note.startBeats); id != 0)
            pasted.insert(id);
    (void) engine.endMidiEdit();
    selection_ = pasted;
    pasteNext_ = at + copied.span;
    studio_.markDirty();
    report(juce::String(static_cast<int>(pasted.size())) + ui::Ls(" notes collées", " notes pasted"));
}

void ClipEditorView::drawMidiPopups(const TrackRef& track, const MidiClip& clip)
{
    if (openShapes_) {
        ImGui::OpenPopup("##shapes");
        openShapes_ = false;
    }
    if (ImGui::BeginPopup("##shapes")) {
        const auto count = shapeTarget_ != 0 && !selection_.contains(shapeTarget_) ? std::size_t { 1 } : selection_.size();
        if (count == 0) {
            ImGui::TextDisabled("%s", L("Sélectionnez des notes, ou faites un clic droit sur une note.",
                                        "Select notes, or right click a note."));
        } else {
            ImGui::TextDisabled(count == 1 ? L("Pour cette note", "For this note") : L("Pour %d notes", "For %d notes"),
                                static_cast<int>(count));
            ui::sectionTitle(L("HAUTEUR", "PITCH"));
            const struct {
                Shape shape;
                const char* french;
                const char* english;
            } pitchShapes[] = {
                { Shape::glideNext, "Glisser vers la note suivante", "Glide to the next note" },
                { Shape::glideFrom, "Glisser depuis la note précédente", "Glide from the previous note" },
                { Shape::scoop, "Scoop (attaque par en dessous)", "Scoop (attack from below)" },
                { Shape::slide, "Glissé depuis le bas", "Slide from below" },
                { Shape::vibrato, "Vibrato retardé", "Delayed vibrato" },
                { Shape::wideVibrato, "Vibrato large", "Wide vibrato" },
                { Shape::fall, "Chute (fall)", "Fall" },
                { Shape::doit, "Montée finale (doit)", "Doit (rise at the end)" },
                { Shape::clearPitch, "Effacer la hauteur", "Clear the pitch" },
            };
            for (const auto& item : pitchShapes)
                if (ImGui::MenuItem(L(item.french, item.english)))
                    applyShape(track, clip, item.shape);
            ui::sectionTitle(L("VOLUME", "GAIN"));
            const struct {
                Shape shape;
                const char* french;
                const char* english;
            } gainShapes[] = {
                { Shape::swell, "Souffle (monte puis retombe)", "Swell (rises then settles)" },
                { Shape::fade, "Decrescendo", "Decrescendo" },
                { Shape::accent, "Accent d'attaque", "Attack accent" },
                { Shape::clearGain, "Effacer le volume", "Clear the gain" },
            };
            for (const auto& item : gainShapes)
                if (ImGui::MenuItem(L(item.french, item.english)))
                    applyShape(track, clip, item.shape);
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##humanise")) {
        ImGui::TextUnformatted(L("Humaniser", "Humanise"));
        ImGui::TextDisabled("%s", selection_.empty() ? L("Toutes les notes du clip", "Every note of the clip")
                                                     : L("Les notes sélectionnées", "The selected notes"));
        const auto row = [](const char* label, const char* id, float* value, const float maximum, const char* format) {
            ImGui::TextDisabled("%s", label);
            ImGui::SameLine(dp(92.0f));
            (void) ui::dragNumber(id, value, 0.2f, 0.0f, maximum, format, dp(90.0f));
        };
        row(L("Placement", "Timing"), "##humanTiming", &humanTiming_, 60.0f, "± %.0f ms");
        row(L("Vélocité", "Velocity"), "##humanVelocity", &humanVelocity_, 50.0f, "± %.0f %%");
        row(L("Longueur", "Length"), "##humanLength", &humanLength_, 50.0f, "± %.0f %%");
        if (ui::primaryButton(L("Appliquer", "Apply"))) {
            humanise(track, clip);
            ImGui::CloseCurrentPopup();
        }
        ui::tooltip(L("Chaque clic tire de nouvelles variations (Ctrl+Z pour revenir).",
                      "Each click draws new variations (Ctrl+Z to go back)."));
        ImGui::EndPopup();
    }
}

void ClipEditorView::applyShape(const TrackRef& track, const MidiClip& clip, const Shape shape)
{
    auto& engine = studio_.engine();
    const auto* current = findMidiClip(engine, track.midiId, clip.id);
    if (current == nullptr)
        return;
    const auto onlyOne = shapeTarget_ != 0 && !selection_.contains(shapeTarget_);
    const auto secondsPerBeat = 60.0 / std::max(1.0, engine.tempo());
    const auto notes = current->notes;
    (void) engine.beginMidiEdit(track.midiId, clip.id);
    for (const auto& note : notes) {
        if (onlyOne ? note.id != shapeTarget_ : !selection_.contains(note.id))
            continue;
        const auto seconds = std::max(1.0e-3, note.durationBeats * secondsPerBeat);
        const auto at = [seconds](const double time) { return static_cast<float>(std::clamp(time / seconds, 0.0, 1.0)); };
        std::vector<NotePoint> add;
        auto pitch = true;
        // The note before or after it: the nearest start, of a chord the
        // nearest key.
        const auto neighbour = [&](const bool after) -> const MidiNote* {
            const MidiNote* best = nullptr;
            for (const auto& other : notes) {
                if (other.id == note.id || (after ? other.startBeats <= note.startBeats + 1.0e-6 : other.startBeats >= note.startBeats - 1.0e-6))
                    continue;
                const auto closer = best == nullptr || (after ? other.startBeats < best->startBeats - 1.0e-9 : other.startBeats > best->startBeats + 1.0e-9);
                const auto tied = best != nullptr && std::abs(other.startBeats - best->startBeats) <= 1.0e-9
                                  && std::abs(other.noteNumber - note.noteNumber) < std::abs(best->noteNumber - note.noteNumber);
                if (closer || tied)
                    best = &other;
            }
            return best;
        };
        switch (shape) {
        case Shape::glideNext: {
            // Held, then gliding to where the next note starts (or to its own
            // end first): quick, then settling on the key.
            const auto* next = neighbour(true);
            if (next == nullptr)
                break;
            const auto offset = static_cast<float>(std::clamp(next->noteNumber - note.noteNumber, -24, 24));
            if (offset == 0.0f)
                break;
            const auto arrive = static_cast<float>(std::clamp((next->startBeats - note.startBeats) / std::max(1.0e-6, note.durationBeats), 0.05, 1.0));
            const auto span = std::min(static_cast<double>(arrive) * seconds * 0.5, 0.25);
            const auto leave = std::max(0.0f, arrive - at(span));
            add = { { 0.0f, 0.0f }, { leave, 0.0f, 0.55f }, { arrive, offset } };
            if (arrive < 1.0f)
                add.push_back({ 1.0f, offset });
            break;
        }
        case Shape::glideFrom: {
            // From the previous note's key into its own.
            const auto* previous = neighbour(false);
            if (previous == nullptr)
                break;
            const auto offset = static_cast<float>(std::clamp(previous->noteNumber - note.noteNumber, -24, 24));
            if (offset == 0.0f)
                break;
            const auto span = std::min(seconds * 0.5, 0.2);
            add = { { 0.0f, offset, 0.55f }, { at(span), 0.0f } };
            break;
        }
        case Shape::scoop: {
            const auto span = std::min(0.09, seconds * 0.3);
            add = { { 0.0f, -1.0f }, { at(span * 0.45), -0.3f }, { at(span), 0.0f } };
            break;
        }
        case Shape::slide: {
            const auto span = std::min(0.2, seconds * 0.4);
            add = { { 0.0f, -3.0f }, { at(span * 0.5), -0.9f }, { at(span), 0.0f } };
            break;
        }
        case Shape::vibrato:
        case Shape::wideVibrato: {
            const auto depth = shape == Shape::vibrato ? 0.22 : 0.45;
            const auto rate = shape == Shape::vibrato ? 5.4 : 4.8;
            const auto onset = std::min(0.3, seconds * 0.3);
            const auto rise = std::min(0.4, std::max(0.05, (seconds - onset) * 0.5));
            if (seconds - onset < 1.0 / rate)
                break;
            auto step = 1.0 / (8.0 * rate);
            if ((seconds - onset) / step > 200.0)
                step = 1.0 / (4.0 * rate);
            for (auto time = onset; time <= seconds + 1.0e-9; time += step) {
                const auto depthNow = depth * std::min(1.0, (time - onset) / rise);
                add.push_back({ at(time), static_cast<float>(depthNow * std::sin(2.0 * juce::MathConstants<double>::pi * rate * (time - onset))) });
            }
            break;
        }
        case Shape::fall: {
            const auto span = std::min(0.3, seconds * 0.35);
            add = { { at(seconds - span), 0.0f }, { at(seconds - span * 0.45), -0.7f }, { 1.0f, -4.0f } };
            break;
        }
        case Shape::doit: {
            const auto span = std::min(0.25, seconds * 0.3);
            add = { { at(seconds - span), 0.0f }, { at(seconds - span * 0.4), 0.5f }, { 1.0f, 2.5f } };
            break;
        }
        case Shape::swell:
            pitch = false;
            add = { { 0.0f, -10.0f }, { 0.5f, 0.0f }, { 1.0f, -4.0f } };
            break;
        case Shape::fade:
            pitch = false;
            add = { { 0.0f, 0.0f }, { 1.0f, -14.0f } };
            break;
        case Shape::accent: {
            pitch = false;
            const auto span = std::min(0.12, seconds * 0.3);
            add = { { 0.0f, 3.0f }, { at(span), -1.5f } };
            break;
        }
        case Shape::clearPitch:
            (void) engine.setMidiNoteExpression(track.midiId, clip.id, note.id, {}, note.gain);
            continue;
        case Shape::clearGain:
            (void) engine.setMidiNoteExpression(track.midiId, clip.id, note.id, note.pitch, {});
            continue;
        }
        if (add.empty())
            continue;
        // Added to what the note has: a scoop and a vibrato and a fall (a
        // note without one takes the shape as it is, its bends kept).
        const auto& old = pitch ? note.pitch : note.gain;
        if (old.empty()) {
            if (pitch)
                (void) engine.setMidiNoteExpression(track.midiId, clip.id, note.id, add, note.gain);
            else
                (void) engine.setMidiNoteExpression(track.midiId, clip.id, note.id, note.pitch, add);
            continue;
        }
        std::vector<float> positions;
        curvePlaces(old, positions);
        curvePlaces(add, positions);
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end(), [](const float a, const float b) { return b - a < 1.0e-4f; }),
                        positions.end());
        std::vector<NotePoint> merged;
        for (const auto position : positions)
            merged.push_back({ position, curveAt(old, position) + curveAt(add, position) });
        if (pitch)
            (void) engine.setMidiNoteExpression(track.midiId, clip.id, note.id, std::move(merged), note.gain);
        else
            (void) engine.setMidiNoteExpression(track.midiId, clip.id, note.id, note.pitch, std::move(merged));
    }
    (void) engine.endMidiEdit();
    studio_.markDirty();
}

void ClipEditorView::humanise(const TrackRef& track, const MidiClip& clip)
{
    auto& engine = studio_.engine();
    const auto* current = findMidiClip(engine, track.midiId, clip.id);
    if (current == nullptr)
        return;
    const auto secondsPerBeat = 60.0 / std::max(1.0, engine.tempo());
    const auto uniform = [this] {
        humanSeed_ ^= humanSeed_ << 13;
        humanSeed_ ^= humanSeed_ >> 17;
        humanSeed_ ^= humanSeed_ << 5;
        return static_cast<double>(humanSeed_ & 0xffffffu) / 16'777'215.0 * 2.0 - 1.0;
    };
    // Small moves more often than large ones.
    const auto soft = [&uniform] { return (uniform() + uniform()) * 0.5; };
    const auto notes = current->notes;
    (void) engine.beginMidiEdit(track.midiId, clip.id);
    for (const auto& note : notes) {
        if (!selection_.empty() && !selection_.contains(note.id))
            continue;
        const auto start = std::max(0.0, note.startBeats + soft() * humanTiming_ / 1'000.0 / secondsPerBeat);
        const auto velocity = std::clamp(note.velocity * static_cast<float>(1.0 + soft() * humanVelocity_ / 100.0), 0.05f, 1.0f);
        const auto length = std::max(0.03, note.durationBeats * (1.0 + soft() * humanLength_ / 100.0));
        (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber, start, length, velocity);
    }
    (void) engine.endMidiEdit();
    studio_.markDirty();
}

void ClipEditorView::handleMidiKeys(const TrackRef& track, const MidiClip& clip)
{
    auto& io = ImGui::GetIO();
    if (io.WantTextInput)
        return;
    auto& engine = studio_.engine();
    const auto* current = findMidiClip(engine, track.midiId, clip.id);
    if (current == nullptr)
        return;
    const auto edit = [&](auto&& change) {
        (void) engine.beginMidiEdit(track.midiId, clip.id);
        for (const auto& note : current->notes)
            if (selection_.contains(note.id))
                change(note);
        (void) engine.endMidiEdit();
        studio_.markDirty();
    };
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
        const auto ids = selection_;
        (void) engine.beginMidiEdit(track.midiId, clip.id);
        for (const auto id : ids)
            (void) engine.removeMidiNote(track.midiId, clip.id, id);
        (void) engine.endMidiEdit();
        selection_.clear();
        studio_.markDirty();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        for (const auto& note : current->notes)
            selection_.insert(note.id);
    // Copy and paste: the velocities when the lane was clicked last, else
    // the notes.
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        if (velocityFocus_)
            copyVelocities(*current);
        else
            copyNotes(*current);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && !velocityFocus_ && !selection_.empty()) {
        copyNotes(*current);
        const auto ids = selection_;
        (void) engine.beginMidiEdit(track.midiId, clip.id);
        for (const auto id : ids)
            (void) engine.removeMidiNote(track.midiId, clip.id, id);
        (void) engine.endMidiEdit();
        selection_.clear();
        studio_.markDirty();
        return;
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        if (velocityFocus_)
            pasteVelocities(track, *current);
        else
            pasteNotes(track, *current);
        return;
    }
    // Up and down in the lane: the velocities (Shift: by 10).
    if (velocityFocus_ && !selection_.empty()) {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
            nudgeVelocities(track, *current, io.KeyShift ? 10.0f : 1.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
            nudgeVelocities(track, *current, io.KeyShift ? -10.0f : -1.0f);
    }
    const auto transpose = [&](const int semitones) {
        edit([&](const MidiNote& note) {
            (void) engine.updateMidiNote(track.midiId, clip.id, note.id, std::clamp(note.noteNumber + semitones, 0, 127),
                                         note.startBeats, note.durationBeats, note.velocity);
        });
    };
    if (!velocityFocus_ && ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && !selection_.empty())
        transpose(io.KeyShift ? 12 : 1);
    if (!velocityFocus_ && ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && !selection_.empty())
        transpose(io.KeyShift ? -12 : -1);
    const auto nudge = gridBeats_ > 0.0 ? gridBeats_ : 0.25;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) && !selection_.empty())
        edit([&](const MidiNote& note) {
            (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber, note.startBeats + nudge, note.durationBeats,
                                         note.velocity);
        });
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) && !selection_.empty())
        edit([&](const MidiNote& note) {
            (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber, std::max(0.0, note.startBeats - nudge),
                                         note.durationBeats, note.velocity);
        });
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false) && !selection_.empty())
        edit([&](const MidiNote& note) {
            (void) engine.updateMidiNote(track.midiId, clip.id, note.id, note.noteNumber,
                                         std::max(0.0, std::round(note.startBeats / nudge) * nudge), note.durationBeats, note.velocity);
        });
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && !selection_.empty()) {
        auto first = 1.0e9;
        auto last = 0.0;
        for (const auto& note : current->notes)
            if (selection_.contains(note.id)) {
                first = std::min(first, note.startBeats);
                last = std::max(last, note.startBeats + note.durationBeats);
            }
        const auto shift = std::max(nudge, std::ceil((last - first) / nudge) * nudge);
        std::set<std::uint64_t> copies;
        const auto notes = current->notes;
        (void) engine.beginMidiEdit(track.midiId, clip.id);
        for (const auto& note : notes)
            if (selection_.contains(note.id)) {
                const auto id = engine.copyMidiNote(track.midiId, clip.id, note, note.startBeats + shift);
                if (id != 0)
                    copies.insert(id);
            }
        (void) engine.endMidiEdit();
        selection_ = copies;
        studio_.markDirty();
    }
}

// ---- Audio ------------------------------------------------------------------------------

void ClipEditorView::drawAudio(const TrackRef& track, const app::AudioTrackController::AudioClip& clip)
{
    if (audio_ == nullptr)
        audio_ = std::make_unique<AudioEditorView>(studio_, state_, waveforms_, host_);
    audio_->draw(track, clip);
}

bool ClipEditorView::undoLauncherAudio(const bool redo)
{
    return audio_ != nullptr && state_.editedClip && state_.editedClip->launcherAudio() && audio_->undoLauncher(redo);
}

void ClipEditorView::drawLauncherAudio(const TrackRef& track, const int scene, const app::AudioTrackController::LauncherClip& clip)
{
    if (audio_ == nullptr)
        audio_ = std::make_unique<AudioEditorView>(studio_, state_, waveforms_, host_);
    audio_->drawLauncher(track, scene, clip);
}

} // namespace cydaw::v2
