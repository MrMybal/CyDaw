#include "ProcessingView.h"

#include "FoundryPlayer.h"
#include "Icons.h"
#include "ImGuiHost.h"
#include "ModeProcessing.h"
#include "Studio.h"
#include "Theme.h"
#include "Widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cydaw::v2 {

using ui::dp;
using ui::L;
namespace proc = processing;
using engine::foundry::Sound;

namespace {

double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

// A button's label: its icon, then its words.
juce::String withIcon(const char* glyph, const char* words)
{
    return juce::String::fromUTF8(glyph) + "  " + juce::String::fromUTF8(words);
}

// The before / after buttons: hear one, the other, switch, stop.
enum class Listen { none, before, after, compare, stop };
Listen drawListenButtons(const bool afterReady, const char* compareTip)
{
    auto pressed = Listen::none;
    if (ImGui::Button(withIcon(icon::play, L("Avant", "Before")).toRawUTF8()))
        pressed = Listen::before;
    ImGui::SameLine();
    ImGui::BeginDisabled(!afterReady);
    if (ImGui::Button(withIcon(icon::play, L("Après", "After")).toRawUTF8()))
        pressed = Listen::after;
    ImGui::SameLine();
    if (ImGui::Button(withIcon(icon::sync, L("Comparer avant / après", "Compare before / after")).toRawUTF8()))
        pressed = Listen::compare;
    ImGui::EndDisabled();
    ui::tooltip(compareTip);
    ImGui::SameLine();
    if (ImGui::Button(withIcon(icon::stop, L("Arrêter", "Stop")).toRawUTF8()))
        pressed = Listen::stop;
    return pressed;
}

juce::String secondsText(const double seconds)
{
    if (seconds < 60.0)
        return juce::String(seconds, 2) + " s";
    const auto minutes = static_cast<int>(seconds / 60.0);
    return juce::String(minutes) + " min " + juce::String(seconds - minutes * 60.0, 1) + " s";
}

// A sound's waves over a box, `total` frames across its width.
void drawWave(ImDrawList* list, const ImVec2 min, const ImVec2 max, const Sound& sound, const double total, const ImU32 colour)
{
    const auto width = static_cast<int>(max.x - min.x);
    if (width <= 0 || total <= 0.0)
        return;
    const auto middle = 0.5f * (min.y + max.y);
    const auto half = 0.5f * (max.y - min.y) - 1.0f;
    for (int x = 0; x < width; ++x) {
        const auto from = static_cast<std::int64_t>(total * x / width);
        const auto to = std::min<std::int64_t>(sound.frames(), static_cast<std::int64_t>(total * (x + 1) / width) + 1);
        if (from >= sound.frames())
            break;
        auto low = 0.0f;
        auto high = 0.0f;
        for (int c = 0; c < sound.channels(); ++c) {
            const auto [a, b] = sound.range(c, from, to);
            low = std::min(low, a);
            high = std::max(high, b);
        }
        list->AddLine(ImVec2(min.x + x + 0.5f, middle - std::clamp(high, -1.0f, 1.0f) * half),
                      ImVec2(min.x + x + 0.5f, middle - std::clamp(low, -1.0f, 1.0f) * half + 1.0f), colour);
    }
}

// The settings, as sliders and choices; true when one moved.
bool drawSettings(const std::vector<proc::Parameter>& parameters, proc::Values& values)
{
    auto moved = false;
    if (parameters.empty()) {
        ImGui::TextDisabled("%s", L("Aucun réglage.", "No settings."));
        return false;
    }
    const auto labelWidth = dp(230.0f);
    for (const auto& parameter : parameters) {
        ImGui::PushID(parameter.id.toRawUTF8());
        auto& value = values[parameter.id];
        if (values.find(parameter.id) == values.end() || std::isnan(value))
            value = parameter.value;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(parameter.label.toRawUTF8());
        ImGui::SameLine(labelWidth);
        ImGui::SetNextItemWidth(dp(260.0f));
        if (!parameter.choices.isEmpty()) {
            auto index = static_cast<int>(std::lround(value - parameter.minimum));
            index = std::clamp(index, 0, parameter.choices.size() - 1);
            if (ImGui::BeginCombo("##choice", parameter.choices[index].toRawUTF8())) {
                for (int i = 0; i < parameter.choices.size(); ++i)
                    if (ImGui::Selectable(parameter.choices[i].toRawUTF8(), i == index)) {
                        value = parameter.minimum + i;
                        moved = true;
                    }
                ImGui::EndCombo();
            }
        } else {
            auto shown = static_cast<float>(value);
            const auto integral = parameter.step >= 1.0;
            if (ImGui::SliderFloat("##value", &shown, static_cast<float>(parameter.minimum), static_cast<float>(parameter.maximum),
                                   integral ? "%.0f" : "%.2f")) {
                value = parameter.step > 0.0 ? std::round(shown / parameter.step) * parameter.step : shown;
                moved = true;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                value = parameter.value;
                moved = true;
            }
        }
        ImGui::PopID();
    }
    return moved;
}

} // namespace

// ---- AudioTrial -------------------------------------------------------------------------

AudioTrial::~AudioTrial()
{
    for (auto& job : jobs_) {
        job->cancel = true;
        if (job->thread.joinable())
            job->thread.join();
    }
}

void AudioTrial::setSound(engine::repair::Channels channels, const double rate, const juce::String& name)
{
    source_ = std::move(channels);
    rate_ = rate;
    soundName_ = name;
    before_ = std::make_shared<Sound>(source_, rate_);
    after_.reset();
    result_ = {};
    changed();
}

void AudioTrial::setProcessing(const proc::Processing& processing)
{
    processing_ = processing;
    values_.clear();
    for (const auto& parameter : proc::parametersOf(processing_))
        values_[parameter.id] = parameter.value;
    after_.reset();
    result_ = {};
    changed();
}

void AudioTrial::processingChanged(const proc::Processing& processing)
{
    // Its settings are edited in its steps: the values follow them.
    processing_ = processing;
    values_.clear();
    for (const auto& parameter : proc::parametersOf(processing_))
        values_[parameter.id] = parameter.value;
    changed();
}

void AudioTrial::changed()
{
    ++generation_;
    dueAt_ = now() + 0.25;
}

void AudioTrial::start()
{
    for (auto& job : jobs_)
        job->cancel = true;
    auto job = std::make_unique<Job>();
    job->generation = generation_;
    auto prepared = proc::prepare(processing_, values_);
    auto* raw = job.get();
    job->thread = std::thread([raw, prepared = std::move(prepared), source = source_, rate = rate_]() mutable {
        raw->result = proc::renderAudio(prepared, std::move(source), rate, [raw](const double done) { raw->progress = done; }, &raw->cancel);
        raw->done = true;
    });
    jobs_.push_back(std::move(job));
}

void AudioTrial::tick()
{
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        auto& job = **it;
        if (!job.done.load()) {
            ++it;
            continue;
        }
        if (job.thread.joinable())
            job.thread.join();
        if (job.generation == generation_ && !job.cancel.load()) {
            result_ = std::move(job.result);
            after_ = result_.ok ? std::make_shared<Sound>(result_.channels, rate_) : nullptr;
        }
        it = jobs_.erase(it);
    }
    if (dueAt_ >= 0.0 && now() >= dueAt_ && before_ != nullptr) {
        dueAt_ = -1.0;
        start();
    }
}

double AudioTrial::progress() const noexcept
{
    return jobs_.empty() ? 1.0 : jobs_.back()->progress.load();
}

bool AudioTrial::drawParameters(const std::vector<proc::Parameter>& parameters)
{
    const auto moved = drawSettings(parameters, values_);
    if (moved)
        changed();
    return moved;
}

bool AudioTrial::drawTail(engine::repair::TailSettings& tail)
{
    auto moved = ImGui::Checkbox(L("Laisser sonner la fin (réverbe, écho)", "Let the end ring (reverb, echo)"), &tail.allow);
    ui::tooltip(L("Le son traité peut durer plus longtemps que l'original : sa fin est attendue jusqu'à ce qu'elle retombe sous le seuil. "
                  "Protection : une fin qui ne décroît plus (un bruit continu) est coupée là où elle a cessé de décroître, et jamais "
                  "au-delà de la durée maximale.",
                  "The processed sound may last longer than the original: its end is waited for until it falls under the threshold. "
                  "Protection: an end that no longer decays (a continuous noise) is cut where it stopped decaying, and never past the "
                  "longest allowed."));
    if (tail.allow) {
        ImGui::SetNextItemWidth(dp(220.0f));
        moved |= ImGui::SliderFloat(L("Seuil de silence", "Silence threshold"), &tail.thresholdDb, -96.0f, -30.0f, "%.0f dB");
        ui::tooltip(L("Sous la crête du son : la fin est finie quand elle reste plus bas.", "Under the sound's peak: the end is over once it stays lower."));
        ImGui::SetNextItemWidth(dp(220.0f));
        moved |= ImGui::SliderFloat(L("Durée max de la fin", "Longest end"), &tail.maxSeconds, 1.0f, 60.0f, "%.0f s");
        ImGui::SetNextItemWidth(dp(220.0f));
        moved |= ImGui::SliderFloat(L("Tenue du silence", "Silence held for"), &tail.holdSeconds, 0.05f, 2.0f, "%.2f s");
        ImGui::SetNextItemWidth(dp(220.0f));
        moved |= ImGui::SliderFloat(L("Fondu final", "Final fade"), &tail.fadeMs, 0.0f, 500.0f, "%.0f ms");
    }
    if (moved) {
        processing_.tail = tail;
        changed();
    }
    return moved;
}

std::shared_ptr<const engine::foundry::Session> AudioTrial::sessionOf(const int which) const
{
    const auto& sound = which == 1 ? after_ : before_;
    if (sound == nullptr)
        return {};
    auto session = std::make_shared<engine::foundry::Session>();
    session->rate = sound->rate();
    engine::foundry::Track track;
    track.id = session->newId();
    track.name = which == 1 ? "After" : "Before";
    engine::foundry::Clip clip;
    clip.id = session->newId();
    clip.sound = sound;
    clip.length = sound->frames();
    track.clips.push_back(std::move(clip));
    session->tracks.push_back(std::move(track));
    session->span = sound->frames();
    return session;
}

void AudioTrial::stop(app::FoundryPlayer& player)
{
    if (player.playing())
        player.stop();
}

void AudioTrial::drawBeforeAfter(app::FoundryPlayer& player, ImGuiHost& host, const float width)
{
    const auto& p = theme::palette();
    if (before_ == nullptr) {
        ImGui::TextDisabled("%s", L("Aucun son à traiter.", "No sound to process."));
        return;
    }
    // What the processing did, or how far it is.
    if (rendering() || dueAt_ >= 0.0) {
        ImGui::ProgressBar(static_cast<float>(progress()), ImVec2(dp(240.0f), 0.0f), L("Traitement…", "Processing…"));
        host.wake(2);
    } else if (!result_.ok && result_.error.isNotEmpty()) {
        ImGui::TextColored(theme::vec(p.record), "%s", result_.error.toRawUTF8());
    } else if (after_ != nullptr) {
        auto line = ui::Ls("Après : ", "After: ") + secondsText(static_cast<double>(after_->frames()) / rate_);
        if (result_.tailFrames > 0)
            line << ui::Ls(" (fin : +", " (end: +") << secondsText(static_cast<double>(result_.tailFrames) / rate_) << ")";
        if (result_.notes.size() > 0)
            line << "  \xc2\xb7  " << result_.notes.joinIntoString(", ");
        ImGui::TextColored(theme::vec(p.textDim), "%s", line.toRawUTF8());
        if (result_.endless) {
            ImGui::SameLine();
            ImGui::TextColored(theme::vec(p.warn), "%s", L("fin coupée : elle ne retombait pas (bruit continu ?)",
                                                          "end cut: it did not fall away (a continuous noise?)"));
        }
    }
    const auto total = static_cast<double>(std::max(before_->frames(), after_ != nullptr ? after_->frames() : std::int64_t {}));
    const auto labelWidth = dp(64.0f);
    const auto boxWidth = std::max(dp(120.0f), width - labelWidth - dp(8.0f));
    const auto boxHeight = dp(64.0f);
    auto* list = ImGui::GetWindowDrawList();
    const auto playing = player.playing();
    for (int which = 0; which < 2; ++which) {
        const auto& sound = which == 1 ? after_ : before_;
        ImGui::PushID(which);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(which == heard_ && playing ? p.accent : p.textDim), "%s",
                           which == 0 ? L("Avant", "Before") : L("Après", "After"));
        ImGui::SameLine(labelWidth);
        const auto min = ImGui::GetCursorScreenPos();
        const auto max = ImVec2(min.x + boxWidth, min.y + boxHeight);
        ImGui::InvisibleButton("##wave", ImVec2(boxWidth, boxHeight));
        const auto clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        list->AddRectFilled(min, max, p.bg1, dp(3.0f));
        list->AddLine(ImVec2(min.x, 0.5f * (min.y + max.y)), ImVec2(max.x, 0.5f * (min.y + max.y)), p.line);
        if (sound != nullptr) {
            drawWave(list, min, max, *sound, total, which == 1 ? p.accent : p.textDim);
            // The original's end, on the processed sound: what rings past it.
            if (which == 1 && before_->frames() < sound->frames()) {
                const auto x = min.x + boxWidth * static_cast<float>(before_->frames() / total);
                list->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), p.warn, 1.0f);
            }
        } else {
            list->AddText(ImVec2(min.x + dp(8.0f), min.y + dp(8.0f)), p.textFaint, L("en cours…", "working…"));
        }
        if (playing && heard_ == which) {
            const auto x = min.x + boxWidth * static_cast<float>(std::clamp(player.position() / total, 0.0, 1.0));
            list->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), p.text, 1.5f);
        }
        list->AddRect(min, max, p.lineStrong, dp(3.0f), 1.0f);
        // A click: heard from there.
        if (clicked && sound != nullptr) {
            const auto at = static_cast<std::int64_t>(total * std::clamp((ImGui::GetIO().MousePos.x - min.x) / boxWidth, 0.0f, 1.0f));
            heard_ = which;
            player.setSession(sessionOf(which));
            player.play(at, 0, false);
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(labelWidth, 0.0f));
    ImGui::SameLine(labelWidth);
    const auto pressed = drawListenButtons(after_ != nullptr, L("Passe de l'un à l'autre au même endroit, pendant la lecture.",
                                                                "Switches from one to the other at the same place, while playing."));
    if (pressed == Listen::before || pressed == Listen::after) {
        heard_ = pressed == Listen::after ? 1 : 0;
        player.setSession(sessionOf(heard_));
        player.play(0, 0, false);
    } else if (pressed == Listen::compare) {
        // The other one, from the same place.
        heard_ = 1 - heard_;
        player.setSession(sessionOf(heard_));
        if (!playing)
            player.play(0, 0, false);
    } else if (pressed == Listen::stop) {
        player.stop();
    }
    // A new result while "after" plays: heard at once.
    if (playing && heard_ == 1 && after_ != nullptr) {
        if (playedSound_ != after_.get()) {
            playedSound_ = after_.get();
            player.setSession(sessionOf(1));
        }
    }
    if (playing)
        host.wake(2);
}

// ---- MidiTrial --------------------------------------------------------------------------

MidiTrial::~MidiTrial() { stopListening(); }

void MidiTrial::setClip(const TrackRef& track, const std::uint64_t clipId)
{
    stopListening();
    track_ = track;
    clipId_ = clipId;
    before_ = studio_.midiClipNotes(track, clipId, clipLength_);
    changed();
}

void MidiTrial::clear()
{
    stopListening();
    clipId_ = 0;
    before_.clear();
    result_ = {};
}

void MidiTrial::setProcessing(const proc::Processing& processing)
{
    processing_ = processing;
    values_.clear();
    for (const auto& parameter : proc::parametersOf(processing_))
        values_[parameter.id] = parameter.value;
    changed();
}

void MidiTrial::processingChanged(const proc::Processing& processing)
{
    processing_ = processing;
    proc::Values kept;
    for (const auto& parameter : proc::parametersOf(processing_)) {
        const auto it = values_.find(parameter.id);
        kept[parameter.id] = it != values_.end() ? it->second : parameter.value;
    }
    values_ = std::move(kept);
    changed();
}

void MidiTrial::changed()
{
    if (clipId_ == 0)
        return;
    result_ = proc::renderNotes(processing_, values_, before_, studio_.engine().tempo(), clipLength_, seed_);
    if (heard_ == 1 && result_.ok)
        (void) studio_.setMidiClipNotes(track_, clipId_, result_.notes, false);
}

bool MidiTrial::drawParameters(const std::vector<proc::Parameter>& parameters)
{
    const auto moved = drawSettings(parameters, values_);
    if (ImGui::Button(L("Autre tirage", "Another draw"))) {
        ++seed_;
        changed();
    }
    ui::tooltip(L("Les choix au hasard du script (random, gauss) tirés de nouveau.", "The script's random choices (random, gauss) drawn again."));
    if (moved)
        changed();
    return moved;
}

void MidiTrial::hear(const int which)
{
    if (clipId_ == 0)
        return;
    if (which == 1 && result_.ok)
        (void) studio_.setMidiClipNotes(track_, clipId_, result_.notes, false);
    else
        studio_.restoreMidiClipNotes(track_, clipId_);
    auto& engine = studio_.engine();
    if (!engine.isMidiClipEditorPreviewing(track_.midiId, clipId_))
        (void) engine.startMidiClipEditorPreview(track_.midiId, clipId_, 0.0);
    heard_ = which;
}

void MidiTrial::stopListening()
{
    if (clipId_ == 0)
        return;
    if (heard_ >= 0)
        (void) studio_.engine().stopMidiClipEditorPreview(track_.midiId);
    studio_.restoreMidiClipNotes(track_, clipId_);
    heard_ = -1;
}

juce::Result MidiTrial::apply()
{
    if (!result_.ok)
        return juce::Result::fail(result_.error.isNotEmpty() ? result_.error : ui::Ls("Le script ne tourne pas", "The script does not run"));
    stopListening();
    const auto applied = studio_.setMidiClipNotes(track_, clipId_, result_.notes, true);
    if (applied.wasOk())
        before_ = studio_.midiClipNotes(track_, clipId_, clipLength_);
    return applied;
}

void MidiTrial::drawBeforeAfter(const float width)
{
    const auto& p = theme::palette();
    if (clipId_ == 0) {
        ImGui::TextDisabled("%s", L("Aucun clip MIDI.", "No MIDI clip."));
        return;
    }
    if (!result_.ok) {
        ImGui::TextColored(theme::vec(p.record), "%s", result_.error.isNotEmpty() ? result_.error.toRawUTF8() : L("Le script ne tourne pas.", "The script does not run."));
    } else {
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           (ui::Ls("Avant : ", "Before: ") + juce::String(static_cast<int>(before_.size())) + ui::Ls(" notes · après : ", " notes · after: ")
                            + juce::String(static_cast<int>(result_.notes.size())) + " notes")
                               .toRawUTF8());
    }
    // A common range: beats across, pitches up.
    auto low = 127;
    auto high = 0;
    auto end = std::max(clipLength_, 1.0);
    for (const auto* set : { &before_, &result_.notes })
        for (const auto& note : *set) {
            low = std::min(low, note.pitch);
            high = std::max(high, note.pitch);
            end = std::max(end, note.start + note.length);
        }
    if (low > high) {
        low = 48;
        high = 72;
    }
    low -= 1;
    high += 1;
    auto& engine = studio_.engine();
    const auto previewing = engine.isMidiClipEditorPreviewing(track_.midiId, clipId_);
    if (!previewing && heard_ >= 0) {
        studio_.restoreMidiClipNotes(track_, clipId_);
        heard_ = -1;
    }
    const auto labelWidth = dp(64.0f);
    const auto boxWidth = std::max(dp(120.0f), width - labelWidth - dp(8.0f));
    const auto boxHeight = dp(72.0f);
    auto* list = ImGui::GetWindowDrawList();
    for (int which = 0; which < 2; ++which) {
        const auto& notes = which == 0 ? before_ : result_.notes;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(which == heard_ ? p.accent : p.textDim), "%s", which == 0 ? L("Avant", "Before") : L("Après", "After"));
        ImGui::SameLine(labelWidth);
        const auto min = ImGui::GetCursorScreenPos();
        const auto max = ImVec2(min.x + boxWidth, min.y + boxHeight);
        ImGui::Dummy(ImVec2(boxWidth, boxHeight));
        list->AddRectFilled(min, max, p.bg1, dp(3.0f));
        for (int beat = 0; beat <= static_cast<int>(end); ++beat) {
            const auto x = min.x + boxWidth * static_cast<float>(beat / end);
            list->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), beat % 4 == 0 ? p.gridStrong : p.grid);
        }
        const auto rowHeight = boxHeight / static_cast<float>(high - low + 1);
        for (const auto& note : notes) {
            const auto x0 = min.x + boxWidth * static_cast<float>(note.start / end);
            const auto x1 = std::max(x0 + 2.0f, min.x + boxWidth * static_cast<float>((note.start + note.length) / end));
            const auto y = max.y - rowHeight * static_cast<float>(note.pitch - low + 1);
            const auto colour = theme::alpha(which == 1 ? p.accent : p.midi, 0.35f + 0.65f * note.velocity);
            list->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + std::max(2.0f, rowHeight - 1.0f)), colour, 1.0f);
        }
        if (previewing && heard_ == which) {
            const auto beats = engine.midiClipEditorPreviewPositionSeconds(track_.midiId) * engine.tempo() / 60.0;
            const auto x = min.x + boxWidth * static_cast<float>(std::clamp(std::fmod(beats, std::max(clipLength_, 0.001)) / end, 0.0, 1.0));
            list->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), p.text, 1.5f);
        }
        list->AddRect(min, max, p.lineStrong, dp(3.0f), 1.0f);
    }
    ImGui::Dummy(ImVec2(labelWidth, 0.0f));
    ImGui::SameLine(labelWidth);
    switch (drawListenButtons(result_.ok, L("Le clip joué par sa piste, ses notes d'avant ou d'après pendant la lecture.",
                                            "The clip played by its track, its notes before or after while it plays."))) {
    case Listen::before: hear(0); break;
    case Listen::after: hear(1); break;
    case Listen::compare: hear(heard_ == 1 ? 0 : 1); break;
    case Listen::stop: stopListening(); break;
    case Listen::none: break;
    }
}

// ---- ProcessingView -------------------------------------------------------------------

ProcessingView::ProcessingView(Studio& studio, UiState& state, ImGuiHost& host)
    : studio_(studio), state_(state), host_(host), midiTrial_(studio), applyMidi_(studio)
{
    reload();
}

ProcessingView::~ProcessingView() = default;

void ProcessingView::reload()
{
    library_ = proc::library();
}

void ProcessingView::select(const int index)
{
    if (index < 0 || index >= static_cast<int>(library_.size()))
        return;
    selected_ = index;
    edited_ = library_[static_cast<std::size_t>(index)];
    kind_ = edited_.kind;
    dirty_ = false;
    step_ = edited_.steps.empty() ? -1 : 0;
    codeFor_ = -2;
    chain_.reset();
    chainFor_ = -1;
    codeError_.clear();
    edited_.name.copyToUTF8(nameBuffer_, sizeof(nameBuffer_));
    edited_.description.copyToUTF8(descriptionBuffer_, sizeof(descriptionBuffer_));
    if (edited_.kind == proc::Kind::audio)
        trial_.setProcessing(edited_);
    else
        midiTrial_.setProcessing(edited_);
}

void ProcessingView::edited()
{
    dirty_ = true;
    if (edited_.kind == proc::Kind::audio)
        trial_.processingChanged(edited_);
    else
        midiTrial_.processingChanged(edited_);
}

juce::String ProcessingView::unsaved() const
{
    return dirty_ ? edited_.name : juce::String();
}

void ProcessingView::stopEverything()
{
    trial_.stop(studio_.engine().processingPlayer());
    midiTrial_.stopListening();
}

void ProcessingView::saveEdited(const bool asNew)
{
    auto copy = edited_;
    copy.factory = false;
    auto file = copy.file;
    if (asNew || edited_.factory || file == juce::File {})
        file = proc::userFolder().getNonexistentChildFile(juce::File::createLegalFileName(copy.name.trim().isNotEmpty() ? copy.name : "Processing"),
                                                          proc::fileExtension, false);
    if (const auto saved = proc::save(copy, file); saved.failed()) {
        state_.status = saved.getErrorMessage();
        state_.statusIsError = true;
        state_.statusTime = ImGui::GetTime();
        return;
    }
    reload();
    for (int i = 0; i < static_cast<int>(library_.size()); ++i)
        if (library_[static_cast<std::size_t>(i)].file == file) {
            const auto step = step_;
            select(i);
            step_ = std::min(step, static_cast<int>(edited_.steps.size()) - 1);
            break;
        }
    state_.status = ui::Ls("Traitement enregistré : ", "Processing saved: ") + file.getFullPathName();
    state_.statusIsError = false;
    state_.statusTime = ImGui::GetTime();
}

void ProcessingView::remove()
{
    if (edited_.factory || edited_.file == juce::File {})
        return;
    (void) edited_.file.moveToTrash();
    reload();
    selected_ = -1;
    dirty_ = false;
    for (int i = 0; i < static_cast<int>(library_.size()); ++i)
        if (library_[static_cast<std::size_t>(i)].kind == kind_) {
            select(i);
            break;
        }
}

void ProcessingView::drawCode(std::vector<char>& buffer, const char* id, const float height)
{
    ImGui::PushFont(theme::monoFont(), dp(theme::baseFontSize()));
    if (ImGui::InputTextMultiline(
            id, buffer.data(), buffer.size(), ImVec2(-1.0f, height), ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
            [](ImGuiInputTextCallbackData* data) {
                auto* code = static_cast<std::vector<char>*>(data->UserData);
                if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                    code->resize(static_cast<std::size_t>(data->BufSize) + 4'096);
                    data->Buf = code->data();
                }
                return 0;
            },
            &buffer)) {
        codeChanged_ = true;
        codeDueAt_ = now() + 0.5;
    }
    ImGui::PopFont();
}

namespace {
void fill(std::vector<char>& buffer, const juce::String& text)
{
    const auto* utf8 = text.toRawUTF8();
    const auto length = std::strlen(utf8);
    buffer.assign(length + 4'096, '\0');
    std::memcpy(buffer.data(), utf8, length);
}
} // namespace

void ProcessingView::drawStep(proc::Step& step, const int index)
{
    const auto& p = theme::palette();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(proc::nameOf(step.type).toRawUTF8());
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textDim), "%s", proc::hintOf(step.type).toRawUTF8());
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (step.type == proc::StepType::code) {
        if (codeFor_ != index) {
            fill(code_, step.code);
            codeFor_ = index;
            codeError_ = proc::inspectJsfx(step.code).error;
        }
        drawCode(code_, "##stepCode", dp(260.0f));
        if (codeChanged_ && now() >= codeDueAt_) {
            codeChanged_ = false;
            step.code = juce::String::fromUTF8(code_.data());
            const auto info = proc::inspectJsfx(step.code);
            codeError_ = info.error;
            if (info.ok) {
                std::map<juce::String, double> settings;
                for (const auto& slider : info.sliders) {
                    const auto key = "slider" + juce::String(slider.index + 1);
                    const auto it = step.settings.find(key);
                    settings[key] = it != step.settings.end() ? it->second : slider.defaultValue;
                }
                step.settings = std::move(settings);
            }
            edited();
        }
        if (codeError_.isNotEmpty())
            ImGui::TextColored(theme::vec(p.record), "%s", codeError_.toRawUTF8());
        else
            ImGui::TextColored(theme::vec(p.play), "%s", L("Le code se compile.", "The code compiles."));
        // Its sliders: the processing's settings, offered again when it is applied.
        std::vector<proc::Parameter> sliders;
        proc::Values values;
        for (const auto& slider : proc::inspectJsfx(step.code).sliders) {
            const auto key = "slider" + juce::String(slider.index + 1);
            const auto it = step.settings.find(key);
            const auto value = it != step.settings.end() ? it->second : slider.defaultValue;
            sliders.push_back({ key, slider.name, slider.minimum, slider.maximum, slider.step, value, slider.choices });
            values[key] = value;
        }
        if (!sliders.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(theme::vec(p.textDim), "%s", L("Ses réglages (les sliders du code) :", "Its settings (the code's sliders):"));
            if (drawSettings(sliders, values)) {
                for (const auto& [key, value] : values)
                    step.settings[key] = value;
                edited();
            }
        }
        return;
    }
    if (step.type == proc::StepType::chain) {
        if (chain_ == nullptr || chainFor_ != index) {
            chain_ = std::make_unique<ModeProcessing>(&host_);
            chain_->restore(step.chain);
            chain_->ensureInput(1, ui::Ls("Son", "Sound"));
            chainFor_ = index;
            chainRevision_ = chain_->revision();
        }
        ImGui::BeginChild("##chainEditor", ImVec2(0.0f, dp(430.0f)), ImGuiChildFlags_Borders);
        chain_->draw();
        ImGui::EndChild();
        if (chain_->revision() != chainRevision_) {
            chainRevision_ = chain_->revision();
            step.chain = chain_->serialise();
            // The knobs shown when it is applied start where they are turned now.
            for (const auto& knob : chain_->parameters(1))
                for (const auto& published : step.published)
                    if (published.binding == juce::String(knob.binding))
                        step.settings[published.binding] = knob.value;
            edited();
        }
        if (ImGui::CollapsingHeader(L("Réglages montrés quand on applique le traitement", "Settings shown when the processing is applied"))) {
            for (const auto& knob : chain_->parameters(1)) {
                if (knob.binding.empty())
                    continue;
                const juce::String binding(knob.binding);
                auto shown = std::any_of(step.published.begin(), step.published.end(), [&](const proc::Published& published) {
                    return published.binding == binding;
                });
                if (ImGui::Checkbox((juce::String(knob.label) + "##" + binding).toRawUTF8(), &shown)) {
                    if (shown) {
                        step.published.push_back({ binding, juce::String(knob.label), knob.minimum, knob.maximum });
                        step.settings[binding] = knob.value;
                    } else {
                        step.published.erase(std::remove_if(step.published.begin(), step.published.end(),
                                                            [&](const proc::Published& published) { return published.binding == binding; }),
                                             step.published.end());
                        step.settings.erase(binding);
                    }
                    edited();
                }
            }
        }
        return;
    }
    // A built-in step: its settings, as it starts.
    proc::Values values;
    auto parameters = proc::builtinSettings(step.type);
    for (auto& parameter : parameters) {
        const auto it = step.settings.find(parameter.id);
        values[parameter.id] = it != step.settings.end() ? it->second : parameter.value;
        parameter.value = values[parameter.id];
    }
    if (drawSettings(parameters, values)) {
        for (const auto& [key, value] : values)
            step.settings[key] = value;
        edited();
    }
}

void ProcessingView::drawSteps(const float width)
{
    const auto& p = theme::palette();
    ImGui::BeginChild("##steps", ImVec2(width, dp(330.0f)), ImGuiChildFlags_Borders);
    ImGui::TextColored(theme::vec(p.textDim), "%s", L("Étapes, dans l'ordre", "Steps, in order"));
    auto moveFrom = -1;
    auto moveBy = 0;
    auto removeAt = -1;
    for (int i = 0; i < static_cast<int>(edited_.steps.size()); ++i) {
        auto& step = edited_.steps[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        if (ImGui::Checkbox("##on", &step.enabled))
            edited();
        ui::tooltip(L("Étape active", "Step on"));
        ImGui::SameLine();
        if (ImGui::Selectable((juce::String(i + 1) + ". " + proc::nameOf(step.type)).toRawUTF8(), step_ == i, 0,
                              ImVec2(width - dp(110.0f), 0.0f)))
            step_ = i;
        ImGui::SameLine(width - dp(84.0f));
        if (ImGui::SmallButton("\xe2\x86\x91")) {
            moveFrom = i;
            moveBy = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("\xe2\x86\x93")) {
            moveFrom = i;
            moveBy = 1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("\xc3\x97"))
            removeAt = i;
        ImGui::PopID();
    }
    if (moveFrom >= 0) {
        const auto to = moveFrom + moveBy;
        if (to >= 0 && to < static_cast<int>(edited_.steps.size())) {
            std::swap(edited_.steps[static_cast<std::size_t>(moveFrom)], edited_.steps[static_cast<std::size_t>(to)]);
            if (step_ == moveFrom)
                step_ = to;
            codeFor_ = -2;
            chain_.reset();
            edited();
        }
    }
    if (removeAt >= 0) {
        edited_.steps.erase(edited_.steps.begin() + removeAt);
        step_ = std::min(step_, static_cast<int>(edited_.steps.size()) - 1);
        codeFor_ = -2;
        chain_.reset();
        edited();
    }
    ImGui::Spacing();
    if (ImGui::Button(L("+ Ajouter une étape", "+ Add a step")))
        ImGui::OpenPopup("##addStep");
    if (ImGui::BeginPopup("##addStep")) {
        for (const auto type : { proc::StepType::chain, proc::StepType::code, proc::StepType::denoise, proc::StepType::hum,
                                 proc::StepType::declick, proc::StepType::highpass, proc::StepType::lowpass, proc::StepType::gain,
                                 proc::StepType::normalise, proc::StepType::trim }) {
            if (ImGui::MenuItem(proc::nameOf(type).toRawUTF8())) {
                edited_.steps.push_back(proc::newStep(type));
                step_ = static_cast<int>(edited_.steps.size()) - 1;
                codeFor_ = -2;
                chain_.reset();
                edited();
            }
            ui::tooltip(proc::hintOf(type).toRawUTF8());
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
}

void ProcessingView::drawScript()
{
    const auto& p = theme::palette();
    if (codeFor_ != -1) {
        fill(code_, edited_.script);
        codeFor_ = -1;
        codeError_ = proc::checkNoteScript(edited_.script);
    }
    drawCode(code_, "##noteScript", dp(300.0f));
    if (codeChanged_ && now() >= codeDueAt_) {
        codeChanged_ = false;
        edited_.script = juce::String::fromUTF8(code_.data());
        codeError_ = proc::checkNoteScript(edited_.script);
        edited();
    }
    if (codeError_.isNotEmpty())
        ImGui::TextColored(theme::vec(p.record), "%s", codeError_.toRawUTF8());
    else
        ImGui::TextColored(theme::vec(p.play), "%s", L("Le script se compile.", "The script compiles."));
    if (ImGui::CollapsingHeader(L("Aide du script de notes", "Note script help"))) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(theme::vec(p.textDim), "%s",
                           L("Langage EEL2 (celui des JSFX). Sections : @init (une fois), @note (chaque note, dans l'ordre de leur début), "
                             "@end (une fois à la fin). Réglages : lignes « paramN:défaut<min,max,pas>Nom » en tête (param1..param16).\n"
                             "Par note : pitch (0..127), start et length (en temps depuis le début du clip), velocity et chance (0..1), "
                             "index ; keep = 0 retire la note. Autour : count, tempo, clip_length, prev_start, next_start, prev_pitch, "
                             "next_pitch.\nFonctions : random() (0..1), gauss() (moyenne 0, écart 1), ms_to_beats(ms), note_start(i), "
                             "note_pitch(i), note_length(i), note_velocity(i), add_note(pitch, start, length, velocity).",
                             "EEL2 language (JSFX's own). Sections: @init (once), @note (each note, in order of start), @end (once at the "
                             "end). Settings: \"paramN:default<min,max,step>Name\" lines at the top (param1..param16).\nPer note: pitch "
                             "(0..127), start and length (beats from the clip's start), velocity and chance (0..1), index; keep = 0 takes "
                             "the note out. Around it: count, tempo, clip_length, prev_start, next_start, prev_pitch, next_pitch.\n"
                             "Functions: random() (0..1), gauss() (mean 0, deviation 1), ms_to_beats(ms), note_start(i), note_pitch(i), "
                             "note_length(i), note_velocity(i), add_note(pitch, start, length, velocity)."));
        ImGui::PopTextWrapPos();
    }
}

void ProcessingView::chooseTestSound()
{
    chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Un son pour essayer le traitement", "A sound to try the processing on"), juce::File {},
                                                   "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& chooser) {
        const auto file = chooser.getResult();
        if (file == juce::File {})
            return;
        (void) dropFiles(juce::StringArray { file.getFullPathName() });
    });
}

bool ProcessingView::dropFiles(const juce::StringArray& files)
{
    for (const auto& path : files) {
        const juce::File file(path);
        engine::repair::Channels channels;
        double rate {};
        if (const auto read = proc::readAudioFile(file, channels, rate); read.wasOk()) {
            trial_.stop(studio_.engine().processingPlayer());
            trial_.setSound(std::move(channels), rate, file.getFileName());
            if (kind_ != proc::Kind::audio) {
                kind_ = proc::Kind::audio;
                for (int i = 0; i < static_cast<int>(library_.size()); ++i)
                    if (library_[static_cast<std::size_t>(i)].kind == kind_) {
                        select(i);
                        break;
                    }
            }
            return true;
        }
    }
    return false;
}

void ProcessingView::drawTrial()
{
    const auto& p = theme::palette();
    auto& player = studio_.engine().processingPlayer();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(L("Essai", "Trial"));
    ImGui::PopFont();
    const auto parameters = proc::parametersOf(edited_);
    if (edited_.kind == proc::Kind::audio) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(trial_.hasSound() ? p.text : p.textFaint), "%s",
                           trial_.hasSound() ? trial_.soundName().toRawUTF8()
                                             : L("Aucun son : déposez un fichier ici, choisissez-en un, ou prenez le clip sélectionné.",
                                                 "No sound: drop a file here, choose one, or take the selected clip."));
        ImGui::SameLine();
        if (ImGui::Button(L("Choisir un son…", "Choose a sound…")))
            chooseTestSound();
        ImGui::SameLine();
        std::optional<ClipRef> clip;
        for (const auto& selected : state_.selectedClips)
            if (!selected.track.midi && selected.scene < 0)
                clip = selected;
        ImGui::BeginDisabled(!clip);
        if (ImGui::Button(L("Prendre le clip sélectionné", "Take the selected clip")) && clip) {
            engine::repair::Channels channels;
            double rate {};
            if (const auto read = studio_.readAudioClipSound(clip->track, clip->clipId, channels, rate); read.wasOk())
                trial_.setSound(std::move(channels), rate, studio_.trackName(clip->track));
            else {
                state_.status = read.getErrorMessage();
                state_.statusIsError = true;
                state_.statusTime = ImGui::GetTime();
            }
        }
        ImGui::EndDisabled();
        trial_.drawBeforeAfter(player, host_, ImGui::GetContentRegionAvail().x);
        if (const auto* result = trial_.result(); result != nullptr && result->ok) {
            if (ImGui::Button(L("Enregistrer le résultat…", "Save the result…"))) {
                const auto channels = result->channels;
                const auto rate = trial_.rate();
                chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Le son traité", "The processed sound"),
                                                               proc::userFolder().getChildFile("Results").getChildFile(
                                                                   trial_.soundName().upToLastOccurrenceOf(".", false, false) + " (" + edited_.name + ").wav"),
                                                               "*.wav");
                chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                      [this, channels, rate](const juce::FileChooser& chooser) {
                                          const auto file = chooser.getResult();
                                          if (file == juce::File {})
                                              return;
                                          (void) file.getParentDirectory().createDirectory();
                                          const auto written = proc::writeAudioFile(file.withFileExtension("wav"), channels, rate);
                                          state_.status = written.wasOk() ? ui::Ls("Enregistré : ", "Saved: ") + file.getFullPathName()
                                                                          : written.getErrorMessage();
                                          state_.statusIsError = written.failed();
                                          state_.statusTime = ImGui::GetTime();
                                      });
            }
        }
        trial_.tick();
        return;
    }
    // MIDI: a clip of the project, heard through its track.
    std::optional<ClipRef> clip;
    for (const auto& selected : state_.selectedClips)
        if (selected.track.midi)
            clip = selected;
    struct Choice {
        TrackRef track;
        std::uint64_t id {};
        juce::String label;
    };
    std::vector<Choice> choices;
    for (const auto& track : studio_.tracks()) {
        if (!track.midi)
            continue;
        if (const auto* clips = studio_.engine().midiClipsView(track.midiId))
            for (const auto& item : *clips)
                choices.push_back({ track, item.id,
                                    studio_.trackName(track) + " \xc2\xb7 " + juce::String(item.name)
                                        + (item.launcherScene >= 0 ? ui::Ls(" (lanceur ", " (launcher ") + juce::String(item.launcherScene + 1) + ")"
                                                                   : juce::String()) });
    }
    if (!midiTrial_.hasClip()) {
        if (clip)
            midiTrial_.setClip(clip->track, clip->clipId);
        else if (!choices.empty())
            midiTrial_.setClip(choices.front().track, choices.front().id);
    }
    juce::String current = ui::Ls("Choisir un clip MIDI…", "Choose a MIDI clip…");
    for (const auto& choice : choices)
        if (midiTrial_.hasClip() && midiTrial_.clipId() == choice.id)
            current = choice.label;
    ImGui::SetNextItemWidth(dp(360.0f));
    if (ImGui::BeginCombo("##trialClip", current.toRawUTF8())) {
        for (const auto& choice : choices)
            if (ImGui::Selectable((choice.label + "##" + juce::String(static_cast<juce::int64>(choice.id))).toRawUTF8(),
                                  midiTrial_.hasClip() && midiTrial_.clipId() == choice.id)) {
                midiTrial_.setProcessing(edited_);
                midiTrial_.setClip(choice.track, choice.id);
            }
        if (choices.empty())
            ImGui::TextDisabled("%s", L("Aucun clip MIDI dans le projet.", "No MIDI clip in the project."));
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!clip);
    if (ImGui::Button(L("Prendre le clip sélectionné", "Take the selected clip")) && clip)
        midiTrial_.setClip(clip->track, clip->clipId);
    ImGui::EndDisabled();
    midiTrial_.drawParameters(parameters);
    midiTrial_.drawBeforeAfter(ImGui::GetContentRegionAvail().x);
    if (midiTrial_.hasClip() && midiTrial_.result().ok) {
        if (ImGui::Button(L("Appliquer au clip", "Apply to the clip"))) {
            const auto applied = midiTrial_.apply();
            state_.status = applied.wasOk() ? ui::Ls("Traitement appliqué (Ctrl+Z l'annule)", "Processing applied (Ctrl+Z undoes it)")
                                            : applied.getErrorMessage();
            state_.statusIsError = applied.failed();
            state_.statusTime = ImGui::GetTime();
        }
    }
}

void ProcessingView::drawLibrary(const float width)
{
    const auto& p = theme::palette();
    ImGui::BeginChild("##processingLibrary", ImVec2(width, 0.0f), ImGuiChildFlags_Borders);
    for (const auto kind : { proc::Kind::audio, proc::Kind::midi }) {
        const auto active = kind_ == kind;
        ImGui::PushStyleColor(ImGuiCol_Button, active ? p.accentDim : p.bg2);
        if (ImGui::Button(kind == proc::Kind::audio ? L("Audio", "Audio") : L("MIDI", "MIDI"), ImVec2((width - dp(24.0f)) / 2.0f, 0.0f))
            && !active) {
            stopEverything();
            kind_ = kind;
            for (int i = 0; i < static_cast<int>(library_.size()); ++i)
                if (library_[static_cast<std::size_t>(i)].kind == kind_) {
                    select(i);
                    break;
                }
        }
        ImGui::PopStyleColor();
        if (kind == proc::Kind::audio)
            ImGui::SameLine();
    }
    ImGui::Spacing();
    for (const auto factory : { true, false }) {
        ImGui::TextColored(theme::vec(p.textFaint), "%s", factory ? L("De CyDAW", "CyDAW's") : L("Les miens", "Mine"));
        auto any = false;
        for (int i = 0; i < static_cast<int>(library_.size()); ++i) {
            const auto& item = library_[static_cast<std::size_t>(i)];
            if (item.kind != kind_ || item.factory != factory)
                continue;
            any = true;
            ImGui::PushID(i);
            if (ImGui::Selectable(item.name.toRawUTF8(), selected_ == i && !dirty_ ? true : selected_ == i))
                if (selected_ != i) {
                    stopEverything();
                    select(i);
                }
            ui::tooltip(item.description.toRawUTF8());
            ImGui::PopID();
        }
        if (!any)
            ImGui::TextDisabled("%s", L("(aucun)", "(none)"));
        ImGui::Spacing();
    }
    ImGui::Separator();
    if (ImGui::Button(L("Nouveau", "New"))) {
        stopEverything();
        proc::Processing fresh;
        fresh.kind = kind_;
        fresh.name = kind_ == proc::Kind::audio ? ui::Ls("Mon traitement", "My processing") : ui::Ls("Mes notes", "My notes");
        if (kind_ == proc::Kind::midi)
            fresh.script = "param1:10<0,50,1>Timing (ms)\n\n@note\nstart += (random() * 2 - 1) * ms_to_beats(param1);\n";
        else
            fresh.steps.push_back(proc::newStep(proc::StepType::chain));
        library_.push_back(fresh);
        select(static_cast<int>(library_.size()) - 1);
        library_.pop_back();
        selected_ = -1;
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Dupliquer", "Duplicate"))) {
        auto copy = edited_;
        copy.name << ui::Ls(" (copie)", " (copy)");
        copy.factory = false;
        copy.file = juce::File {};
        library_.push_back(copy);
        select(static_cast<int>(library_.size()) - 1);
        library_.pop_back();
        selected_ = -1;
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(edited_.factory || edited_.file == juce::File {});
    if (ImGui::Button(L("Supprimer", "Delete")))
        ImGui::OpenPopup("##deleteProcessing");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("##deleteProcessing")) {
        ImGui::TextUnformatted((ui::Ls("Mettre « ", "Move \"") + edited_.name + ui::Ls(" » à la corbeille ?", "\" to the recycle bin?")).toRawUTF8());
        if (ImGui::Button(L("Supprimer", "Delete"))) {
            remove();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(L("Annuler", "Cancel")))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, p.textFaint);
    ImGui::TextWrapped("%s", proc::userFolder().getFullPathName().toRawUTF8());
    ImGui::PopStyleColor();
    ui::tooltip(L("Le dossier de vos traitements (Options › Emplacements).", "The folder of your processings (Options › Locations)."));
    ImGui::EndChild();
}

void ProcessingView::drawEditor()
{
    const auto& p = theme::palette();
    ImGui::BeginChild("##processingEditor", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    ImGui::SetNextItemWidth(dp(320.0f));
    if (ImGui::InputText(L("Nom", "Name"), nameBuffer_, sizeof(nameBuffer_))) {
        edited_.name = juce::String::fromUTF8(nameBuffer_);
        dirty_ = true;
    }
    ImGui::SameLine();
    if (ui::primaryButton(edited_.factory ? L("Enregistrer dans les miens", "Save to mine") : L("Enregistrer", "Save")))
        saveEdited(false);
    ImGui::SameLine();
    if (ImGui::Button(L("Enregistrer comme nouveau", "Save as new")))
        saveEdited(true);
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored(theme::vec(p.warn), "%s", L("\xe2\x80\xa2 modifié", "\xe2\x80\xa2 changed"));
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##description", descriptionBuffer_, sizeof(descriptionBuffer_))) {
        edited_.description = juce::String::fromUTF8(descriptionBuffer_);
        dirty_ = true;
    }
    ImGui::Spacing();
    if (edited_.kind == proc::Kind::audio) {
        const auto stepsWidth = dp(300.0f);
        drawSteps(stepsWidth);
        ImGui::SameLine();
        ImGui::BeginChild("##stepEditor", ImVec2(0.0f, edited_.steps.empty() || step_ < 0 ? dp(330.0f) : 0.0f),
                          ImGuiChildFlags_Borders | (edited_.steps.empty() || step_ < 0 ? 0 : ImGuiChildFlags_AutoResizeY));
        if (step_ >= 0 && step_ < static_cast<int>(edited_.steps.size()))
            drawStep(edited_.steps[static_cast<std::size_t>(step_)], step_);
        else
            ImGui::TextDisabled("%s", L("Ajoutez une étape : une chaîne d'effets, du code JSFX, ou un traitement prêt (débruitage…).",
                                        "Add a step: an effect chain, JSFX code, or a ready treatment (noise reduction…)."));
        ImGui::EndChild();
        if (ImGui::CollapsingHeader(L("Fin du son", "The sound's end"), ImGuiTreeNodeFlags_DefaultOpen)) {
            auto tail = edited_.tail;
            if (trial_.drawTail(tail)) {
                edited_.tail = tail;
                dirty_ = true;
            }
        }
    } else {
        drawScript();
    }
    ImGui::Separator();
    drawTrial();
    ImGui::EndChild();
}

void ProcessingView::draw()
{
    if (selected_ < 0 && !dirty_)
        for (int i = 0; i < static_cast<int>(library_.size()); ++i)
            if (library_[static_cast<std::size_t>(i)].kind == kind_) {
                select(i);
                break;
            }
    state_.focused = Panel::processing;
    const auto width = dp(260.0f);
    drawLibrary(width);
    ImGui::SameLine();
    drawEditor();
}

// ---- The apply window -------------------------------------------------------------------

void ProcessingView::openApply(const ClipRef& clip)
{
    stopEverything();
    applyError_.clear();
    applyClip_ = clip;
    applyClipName_ = studio_.trackName(clip.track);
    const auto kind = clip.track.midi ? proc::Kind::midi : proc::Kind::audio;
    reload();
    applyChoices_ = proc::library(kind);
    if (applyChoices_.empty()) {
        applyError_ = ui::Ls("Aucun traitement de ce genre", "No processing of that kind");
        return;
    }
    applyChoice_ = std::clamp(applyChoice_, 0, static_cast<int>(applyChoices_.size()) - 1);
    if (kind == proc::Kind::audio) {
        if (clip.scene >= 0) {
            applyError_ = ui::Ls("Les clips audio du lanceur ne se traitent pas encore : passez par l'arrangement.",
                                 "Launcher audio clips cannot be processed yet: use the arrangement.");
            return;
        }
        engine::repair::Channels channels;
        double rate {};
        if (const auto read = studio_.readAudioClipSound(clip.track, clip.clipId, channels, rate); read.failed()) {
            applyError_ = read.getErrorMessage();
            return;
        }
        applyTrial_.setSound(std::move(channels), rate, applyClipName_);
        applyTrial_.setProcessing(applyChoices_[static_cast<std::size_t>(applyChoice_)]);
    } else {
        applyMidi_.setProcessing(applyChoices_[static_cast<std::size_t>(applyChoice_)]);
        applyMidi_.setClip(clip.track, clip.clipId);
    }
    applyRequested_ = true;
}

void ProcessingView::drawApply()
{
    // The mode's sound stops when the mode is left.
    if (state_.mode != AppMode::processing && !applyOpen_) {
        auto& player = studio_.engine().processingPlayer();
        if (player.playing())
            player.stop();
        midiTrial_.stopListening();
    }
    if (applyError_.isNotEmpty() && !applyRequested_ && !applyOpen_) {
        state_.status = applyError_;
        state_.statusIsError = true;
        state_.statusTime = ImGui::GetTime();
        applyError_.clear();
    }
    if (applyRequested_) {
        applyRequested_ = false;
        applyOpen_ = true;
        ImGui::OpenPopup("##processingApply");
    }
    if (!applyOpen_)
        return;
    const auto* main = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(std::min(dp(960.0f), main->WorkSize.x * 0.92f), std::min(dp(600.0f), main->WorkSize.y * 0.88f)),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(main->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("##processingApply", nullptr, ImGuiWindowFlags_NoTitleBar)) {
        applyOpen_ = false;
        return;
    }
    const auto& p = theme::palette();
    const auto midi = applyClip_.track.midi;
    auto& player = studio_.engine().processingPlayer();
    auto close = false;
    ImGui::BeginChild("##applyBody", ImVec2(0.0f, -(ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y)));
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.1f);
    ImGui::TextUnformatted((ui::Ls("Appliquer un traitement \xe2\x80\x94 ", "Apply a processing \xe2\x80\x94 ") + applyClipName_).toRawUTF8());
    ImGui::PopFont();
    ImGui::SetNextItemWidth(dp(360.0f));
    const auto& choice = applyChoices_[static_cast<std::size_t>(applyChoice_)];
    if (ImGui::BeginCombo("##applyChoice", choice.name.toRawUTF8())) {
        for (int i = 0; i < static_cast<int>(applyChoices_.size()); ++i) {
            const auto& item = applyChoices_[static_cast<std::size_t>(i)];
            if (ImGui::Selectable((item.name + (item.factory ? juce::String() : ui::Ls("  (les miens)", "  (mine)"))).toRawUTF8(), i == applyChoice_)) {
                applyChoice_ = i;
                if (midi)
                    applyMidi_.setProcessing(item);
                else
                    applyTrial_.setProcessing(item);
            }
            ui::tooltip(item.description.toRawUTF8());
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(L("Modifier dans Traitements…", "Edit in Processings…"))) {
        for (int i = 0; i < static_cast<int>(library_.size()); ++i)
            if (library_[static_cast<std::size_t>(i)].file == choice.file)
                select(i);
        state_.mode = AppMode::processing;
        close = true;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(theme::vec(p.textDim), "%s", choice.description.toRawUTF8());
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    const auto parameters = proc::parametersOf(midi ? choice : applyTrial_.processing());
    if (midi) {
        applyMidi_.drawParameters(parameters);
        ImGui::Separator();
        applyMidi_.drawBeforeAfter(ImGui::GetContentRegionAvail().x);
    } else {
        applyTrial_.drawParameters(parameters);
        if (std::any_of(choice.steps.begin(), choice.steps.end(), [](const proc::Step& step) { return step.enabled && proc::rings(step.type); })) {
            auto tail = applyTrial_.processing().tail;
            (void) applyTrial_.drawTail(tail);
        }
        ImGui::Separator();
        applyTrial_.drawBeforeAfter(player, host_, ImGui::GetContentRegionAvail().x);
        applyTrial_.tick();
    }
    if (applyError_.isNotEmpty())
        ImGui::TextColored(theme::vec(p.record), "%s", applyError_.toRawUTF8());
    ImGui::EndChild();
    ImGui::Separator();
    const auto ready = midi ? applyMidi_.result().ok : (applyTrial_.result() != nullptr && applyTrial_.result()->ok && !applyTrial_.rendering());
    ImGui::BeginDisabled(!ready);
    if (ui::primaryButton(L("Appliquer", "Apply"), ImVec2(dp(120.0f), 0.0f))) {
        juce::Result applied = juce::Result::ok();
        if (midi) {
            applied = applyMidi_.apply();
        } else {
            player.stop();
            applied = studio_.applyAudioProcessing(applyClip_.track, applyClip_.clipId, *applyTrial_.result(), applyTrial_.rate(),
                                                   applyTrial_.sourceFrames(), ui::Ls("Traitement : ", "Processing: ") + choice.name);
        }
        if (applied.failed()) {
            applyError_ = applied.getErrorMessage();
        } else {
            state_.status = ui::Ls("Traitement appliqué : ", "Processing applied: ") + choice.name + ui::Ls(" (Ctrl+Z l'annule)", " (Ctrl+Z undoes it)");
            state_.statusIsError = false;
            state_.statusTime = ImGui::GetTime();
            close = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(L("Annuler", "Cancel"), ImVec2(dp(120.0f), 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
        close = true;
    if (close) {
        player.stop();
        applyMidi_.stopListening();
        applyMidi_.clear();
        applyOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    host_.wake(2);
}

// ---- The `processing` command's interface part ------------------------------------------

juce::var ProcessingView::command(const juce::var& request)
{
    auto* result = new juce::DynamicObject();
    if (static_cast<bool>(request.getProperty("show", false)))
        state_.mode = AppMode::processing;
    if (request.hasProperty("select")) {
        reload();
        const auto name = request.getProperty("select", {}).toString();
        for (int i = 0; i < static_cast<int>(library_.size()); ++i)
            if (library_[static_cast<std::size_t>(i)].name.equalsIgnoreCase(name))
                select(i);
    }
    if (request.hasProperty("testFile"))
        result->setProperty("loaded", dropFiles(juce::StringArray { request.getProperty("testFile", {}).toString() }));
    if (request.hasProperty("reload"))
        reload();
    result->setProperty("mode", state_.mode == AppMode::processing);
    result->setProperty("selected", edited_.name);
    result->setProperty("dirty", dirty_);
    result->setProperty("trialSound", trial_.soundName());
    result->setProperty("rendering", trial_.rendering());
    if (const auto* done = trial_.result()) {
        result->setProperty("trialFrames", done->ok ? static_cast<juce::int64>(done->channels.front().size()) : juce::int64 {});
        result->setProperty("trialTail", static_cast<juce::int64>(done->tailFrames));
        result->setProperty("trialError", done->error);
    }
    result->setProperty("applyOpen", applyOpen_ || applyRequested_);
    return juce::var(result);
}

} // namespace cydaw::v2
