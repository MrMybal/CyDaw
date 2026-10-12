#include "AudioEditorView.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Theme.h"
#include "Widgets.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace cydaw::v2 {

using ui::dp;
using ui::L;

namespace {

using AudioClip = app::AudioTrackController::AudioClip;

// Seconds of source per second of the clip's timeline.
double speedOf(const AudioClip& clip)
{
    const auto source = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
    return source / std::max(1.0e-6, clip.durationSeconds);
}

// Where in its source a moment of the clip plays (through its warp
// markers), as the engine reads it.
double sourceAt(const AudioClip& clip, const double timeline)
{
    return app::AudioTrackController::clipSourceTime(clip, timeline);
}

// A take's colour (the comp row and its lane).
ImU32 takeColour(const std::size_t take)
{
    static const ImU32 colours[] = { IM_COL32(255, 155, 47, 255), IM_COL32(90, 176, 240, 255), IM_COL32(82, 209, 162, 255),
                                     IM_COL32(199, 146, 234, 255), IM_COL32(232, 118, 90, 255), IM_COL32(232, 197, 71, 255),
                                     IM_COL32(240, 98, 146, 255),  IM_COL32(140, 158, 255, 255) };
    return colours[take % (sizeof(colours) / sizeof(colours[0]))];
}

// The passages a clip plays, as a comp (one passage of the active take
// when it has none).
std::vector<app::AudioTrackController::CompSegment> passagesOf(const AudioClip& clip)
{
    if (!clip.comp.empty())
        return clip.comp;
    return { { 0.0, clip.durationSeconds, clip.activeTakeIndex } };
}

} // namespace

AudioEditorView::AudioEditorView(Studio& studio, UiState& state, WaveformCache& waveforms, ImGuiHost& host)
    : studio_(studio), state_(state), waveforms_(waveforms), host_(host)
{
}

void AudioEditorView::report(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

float AudioEditorView::xOf(const double seconds) const
{
    return waveLeft_ + static_cast<float>((seconds - scroll_) * pixelsPerSecond_);
}

double AudioEditorView::secondsAt(const float x) const
{
    return scroll_ + static_cast<double>(x - waveLeft_) / std::max(1.0e-9, pixelsPerSecond_);
}

std::pair<double, double> AudioEditorView::target(const AudioClip& clip) const
{
    if (selectionEnd_ - selectionStart_ > 1.0e-4)
        return { selectionStart_, selectionEnd_ - selectionStart_ };
    const auto source = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
    return { clip.sourceOffsetSeconds, source };
}

// ---- The panel --------------------------------------------------------------------------------

void AudioEditorView::draw(const TrackRef& track, const AudioClip& clip)
{
    if (const auto face = std::exchange(state_.audioEditorFace, -1); face >= 0)
        mode_ = face == 1 ? Mode::takes : face == 2 ? Mode::stretch : face == 3 ? Mode::events : Mode::sample;
    if (shownClip_ != clip.id) {
        shownClip_ = clip.id;
        pixelsPerSecond_ = 0.0;
        scroll_ = 0.0;
        selectionStart_ = selectionEnd_ = clip.sourceOffsetSeconds;
        compLane_ = -1;
        selectedEvent_ = 0;
        eventCursor_ = 0.0;
        eventDrag_ = EventDrag::none;
        // A clip of events opens on them.
        if (clip.usesEventTimeline)
            mode_ = Mode::events;
    }
    // Another face: another timeline (the source's, the clip's), fitted.
    if (shownMode_ != mode_) {
        shownMode_ = mode_;
        pixelsPerSecond_ = 0.0;
        scroll_ = 0.0;
    }
    // Hearing a selection stops at its end.
    auto& engine = studio_.engine();
    if (previewEnd_ && (!engine.isSamplePreviewing() || engine.samplePreviewPositionSeconds() >= *previewEnd_)) {
        engine.stopSamplePreview();
        previewEnd_.reset();
    }
    if (previewEnd_ || engine.isAudioClipPreviewing())
        host_.wake(2);
    drawHeader(track, clip);
    switch (mode_) {
    case Mode::sample:
        drawSample(track, clip);
        break;
    case Mode::takes:
        drawTakes(track, clip);
        break;
    case Mode::stretch:
        drawStretch(track, clip);
        break;
    case Mode::events:
        drawEvents(track, clip);
        break;
    }
}

void AudioEditorView::drawHeader(const TrackRef& track, const AudioClip& clip)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted((clip.name.isNotEmpty() ? clip.name : clip.sourceFile.getFileNameWithoutExtension()).toRawUTF8());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s  |  %s", studio_.trackName(track).toRawUTF8(), clip.sourceFile.getFileName().toRawUTF8());
    // Its faces.
    ImGui::SameLine(0.0f, dp(18.0f));
    const auto face = [this](const char* label, const Mode mode) {
        auto on = mode_ == mode;
        if (ui::toggle(label, &on, theme::palette().accent))
            mode_ = mode;
        ImGui::SameLine(0.0f, dp(2.0f));
    };
    face(L("Échantillon", "Sample"), Mode::sample);
    char takes[48];
    std::snprintf(takes, sizeof(takes), "%s (%d)", L("Prises", "Takes"), static_cast<int>(std::max<std::size_t>(1, clip.takes.size())));
    face(takes, Mode::takes);
    face(L("Étirement", "Stretch"), Mode::stretch);
    char eventsLabel[48];
    std::snprintf(eventsLabel, sizeof(eventsLabel), "%s (%d)", L("Événements", "Events"),
                  static_cast<int>(clip.usesEventTimeline ? clip.events.size() : 1));
    face(eventsLabel, Mode::events);
    // Hearing the clip as the arrangement plays it.
    ImGui::SameLine(0.0f, dp(16.0f));
    const auto playing = engine.isAudioClipPreviewing();
    if (ui::iconButton(playing ? icon::stop : icon::play,
                       playing ? L("Arrêter l'écoute", "Stop listening")
                               : L("Écouter le clip comme la piste le joue (étiré, transposé, ses prises)",
                                   "Hear the clip as the track plays it (stretched, transposed, its takes)"),
                       playing, p.play)) {
        if (playing)
            engine.stopAudioClipPreview();
        else if (const auto started = engine.startAudioClipPreview(track.audioIndex, clip.id, 0.0); started.failed())
            report(started.getErrorMessage(), true);
    }

    // Its place, length and fades (the stretch kept).
    auto start = static_cast<float>(clip.startSeconds);
    auto duration = static_cast<float>(clip.durationSeconds);
    auto offset = static_cast<float>(clip.sourceOffsetSeconds);
    auto fadeIn = static_cast<float>(clip.fadeInSeconds);
    auto fadeOut = static_cast<float>(clip.fadeOutSeconds);
    auto changed = false;
    const auto field = [&](const char* label, const char* id, float* value, const float maximum) {
        ImGui::TextDisabled("%s", label);
        ImGui::SameLine();
        const auto edited = ui::dragNumber(id, value, 0.005f, 0.0f, maximum, "%.3f s", dp(80.0f));
        ImGui::SameLine(0.0f, dp(12.0f));
        return edited;
    };
    changed |= field(L("Début", "Start"), "##start", &start, 36'000.0f);
    changed |= field(L("Durée", "Length"), "##duration", &duration, 36'000.0f);
    // A clip of events reads no source of its own: each event has its offset.
    if (!clip.usesEventTimeline)
        changed |= field(L("Décalage", "Offset"), "##offset", &offset, static_cast<float>(std::max(0.0, clip.sourceLengthSeconds)));
    changed |= field(L("Fondu entrée", "Fade in"), "##fadeIn", &fadeIn, duration * 0.5f);
    changed |= field(L("Fondu sortie", "Fade out"), "##fadeOut", &fadeOut, duration * 0.5f);
    ImGui::NewLine();
    if (changed) {
        const auto length = std::max(0.02f, duration);
        (void) engine.updateAudioClip(track.audioIndex, clip.id, start, offset, length, fadeIn, fadeOut, length * speedOf(clip));
        studio_.markDirty();
    }
}

// ---- The source, large -------------------------------------------------------------------------

std::pair<ImVec2, ImVec2> AudioEditorView::sourceView(const char* id, const double total, const float height)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto top = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto bandHeight = dp(14.0f);
    const auto secondsHeight = dp(16.0f);
    const ImVec2 origin(top.x, top.y + bandHeight + secondsHeight);
    const ImVec2 size(width, std::max(dp(40.0f), height - bandHeight - secondsHeight));
    waveLeft_ = origin.x;
    const auto fitted = static_cast<double>(size.x) / std::max(0.01, total);
    if (pixelsPerSecond_ <= 0.0) {
        pixelsPerSecond_ = fitted;
        scroll_ = 0.0;
    }
    if (ui::timelineBand(id, top, ImVec2(top.x + size.x, top.y + bandHeight), scroll_, pixelsPerSecond_, fitted * 0.5, 48'000.0, 0.1,
                         1.0)) {
        pixelsPerSecond_ = fitted;
        scroll_ = 0.0;
    }
    scroll_ = std::clamp(scroll_, 0.0, std::max(0.0, total - static_cast<double>(size.x) / pixelsPerSecond_ * 0.5));
    // Its seconds.
    const ImVec2 rulerMin(top.x, top.y + bandHeight);
    list->AddRectFilled(rulerMin, ImVec2(top.x + size.x, origin.y), p.bg1);
    auto step = 600.0;
    for (const auto candidate : { 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 120.0 })
        if (candidate * pixelsPerSecond_ >= 60.0) {
            step = candidate;
            break;
        }
    ImGui::PushFont(nullptr, ui::currentFontBase() * 0.76f);
    list->PushClipRect(rulerMin, ImVec2(top.x + size.x, origin.y), true);
    for (auto seconds = std::floor(scroll_ / step) * step; xOf(seconds) < top.x + size.x; seconds += step) {
        const auto x = xOf(seconds);
        char text[24];
        std::snprintf(text, sizeof(text), step < 0.1 ? "%.3f" : step < 1.0 ? "%.2f" : "%.1f", seconds);
        list->AddLine(ImVec2(x, origin.y - dp(5.0f)), ImVec2(x, origin.y), p.textFaint);
        list->AddText(ImVec2(x + dp(2.0f), rulerMin.y + dp(1.0f)), p.textDim, text);
    }
    list->PopClipRect();
    ImGui::PopFont();
    // The wheel: Ctrl zooms around the pointer, otherwise it moves.
    if (ImGui::IsMouseHoveringRect(origin, ImVec2(origin.x + size.x, origin.y + size.y)) && ImGui::IsWindowHovered()
        && ImGui::GetIO().MouseWheel != 0.0f) {
        const auto& io = ImGui::GetIO();
        if (io.KeyCtrl) {
            const auto at = secondsAt(io.MousePos.x);
            pixelsPerSecond_ = std::clamp(pixelsPerSecond_ * (io.MouseWheel > 0.0f ? 1.25 : 1.0 / 1.25), fitted * 0.5, 48'000.0);
            scroll_ = std::max(0.0, at - static_cast<double>(io.MousePos.x - origin.x) / pixelsPerSecond_);
        } else {
            scroll_ = std::max(0.0, scroll_ - io.MouseWheel * 60.0 / pixelsPerSecond_);
        }
        ui::takeWheel();
    }
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), p.bg0, dp(4.0f));
    return { origin, size };
}

void AudioEditorView::drawSourceWave(const juce::File& file, const double total, const ImVec2 origin, const ImVec2 size,
                                     const ImU32 colour, const double playedFrom, const double playedTo)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    auto* thumbnail = waveforms_.get(file);
    if (thumbnail == nullptr)
        return;
    if (!thumbnail->isFullyLoaded())
        host_.wake(2);
    list->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    const auto channels = std::max(1, std::min(2, thumbnail->getNumChannels()));
    const auto laneHeight = size.y / static_cast<float>(channels);
    for (int channel = 0; channel < channels; ++channel) {
        const auto centre = origin.y + laneHeight * (static_cast<float>(channel) + 0.5f);
        const auto half = laneHeight * 0.46f;
        list->AddLine(ImVec2(origin.x, centre), ImVec2(origin.x + size.x, centre), theme::alpha(p.textFaint, 0.25f));
        for (auto x = 0.0f; x < size.x; x += 1.0f) {
            const auto s0 = secondsAt(origin.x + x);
            const auto s1 = secondsAt(origin.x + x + 1.0f);
            if (s0 >= total)
                break;
            if (s1 < 0.0)
                continue;
            float low = 0.0f;
            float high = 0.0f;
            thumbnail->getApproximateMinMax(std::max(0.0, s0), s1, channel, low, high);
            const auto inside = s0 >= playedFrom && s0 <= playedTo;
            list->AddRectFilled(ImVec2(origin.x + x, centre - high * half), ImVec2(origin.x + x + 1.0f, centre - low * half + 1.0f),
                                inside ? theme::brighter(colour, 0.2f) : theme::alpha(p.textFaint, 0.55f));
        }
    }
    // The part the clip plays.
    const auto x0 = xOf(playedFrom);
    const auto x1 = xOf(playedTo);
    list->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + size.y), theme::alpha(colour, 0.06f));
    list->AddRect(ImVec2(x0, origin.y), ImVec2(x1, origin.y + size.y), theme::alpha(colour, 0.8f), 0.0f, dp(1.2f));
    list->PopClipRect();
}

// ---- ÉCHANTILLON ------------------------------------------------------------------------------

void AudioEditorView::apply(const TrackRef& track, const AudioClip& clip, const app::AudioEditOperation operation, const char* label,
                            const float levelDb)
{
    using Op = app::AudioEditOperation;
    const auto hasSelection = selectionEnd_ - selectionStart_ > 1.0e-4;
    const auto needsSelection = operation == Op::cut || operation == Op::crop || operation == Op::silence || operation == Op::fadeIn
                                || operation == Op::fadeOut || operation == Op::insertSilence;
    if (needsSelection && !hasSelection) {
        report(ui::Ls("Sélectionnez d'abord un passage : glissez sur la forme d'onde.", "Select a passage first: drag over the waveform."),
               true);
        return;
    }
    auto [start, length] = target(clip);
    if (operation == Op::paste) {
        start = selectionStart_;
        length = 0.0;
    }
    app::AudioEditRequest request;
    request.operation = operation;
    request.levelDb = levelDb;
    request.curve = static_cast<app::FadeCurve>(std::clamp(fadeCurve_, 0, 3));
    const auto done = studio_.editAudioClip(track, clip.id, request, start, length, juce::String::fromUTF8(label));
    if (done.failed()) {
        report(done.getErrorMessage(), true);
        return;
    }
    report(juce::String::fromUTF8(label) + ui::Ls(" : fait (Ctrl+Z l'annule)", ": done (Ctrl+Z undoes it)"), false);
    // A cut or a crop leaves a place, not a passage.
    if (operation == Op::cut || operation == Op::paste)
        selectionEnd_ = selectionStart_;
    else if (operation == Op::crop)
        selectionStart_ = selectionEnd_ = 0.0;
}

void AudioEditorView::drawSampleToolbar(const TrackRef& track, const AudioClip& clip)
{
    using Op = app::AudioEditOperation;
    const auto hasSelection = selectionEnd_ - selectionStart_ > 1.0e-4;
    ImGui::AlignTextToFramePadding();
    if (hasSelection)
        ImGui::TextDisabled("%s %.3f \xe2\x80\x93 %.3f s (%.3f s)", L("Sélection", "Selection"), selectionStart_, selectionEnd_,
                            selectionEnd_ - selectionStart_);
    else
        ImGui::TextDisabled("%s %.3f s", L("Curseur", "Cursor"), selectionStart_);
    ImGui::SameLine(0.0f, dp(10.0f));
    const auto button = [](const char* label, const char* tip) {
        const auto clicked = ImGui::SmallButton(label);
        ui::tooltip(tip);
        ImGui::SameLine(0.0f, dp(3.0f));
        return clicked;
    };
    if (button(L("Couper", "Cut"), L("Retire la sélection du fichier et la copie (Ctrl+X)", "Takes the selection out of the file, copied (Ctrl+X)"))) {
        const auto [start, length] = target(clip);
        studio_.copyAudio(clip.sourceFile, start, length);
        apply(track, clip, Op::cut, "Couper");
    }
    if (button(L("Copier", "Copy"), L("Copie la sélection (Ctrl+C)", "Copies the selection (Ctrl+C)"))) {
        const auto [start, length] = target(clip);
        studio_.copyAudio(clip.sourceFile, start, length);
        report(ui::Ls("Passage copié", "Passage copied"), false);
    }
    ImGui::BeginDisabled(!studio_.audioClipboard().has_value());
    if (button(L("Coller", "Paste"), L("Insère le passage copié au curseur (Ctrl+V)", "Inserts the copied passage at the cursor (Ctrl+V)")))
        apply(track, clip, Op::paste, "Coller");
    ImGui::EndDisabled();
    if (button(L("Silence", "Silence"), L("Rend la sélection muette (Suppr)", "Makes the selection silent (Delete)")))
        apply(track, clip, Op::silence, "Silence");
    if (button(L("Rogner", "Crop"), L("Ne garde que la sélection", "Keeps the selection alone")))
        apply(track, clip, Op::crop, "Rogner");
    if (button(L("Insérer un silence", "Insert silence"),
               L("Insère au début de la sélection un silence aussi long qu'elle", "Inserts at the selection's start a silence as long as it")))
        apply(track, clip, Op::insertSilence, "Insérer un silence");
    ImGui::SameLine(0.0f, dp(8.0f));
    if (button(L("Fondu entrant", "Fade in"), L("Fondu entrant sur la sélection", "Fade in over the selection")))
        apply(track, clip, Op::fadeIn, "Fondu entrant");
    if (button(L("Fondu sortant", "Fade out"), L("Fondu sortant sur la sélection", "Fade out over the selection")))
        apply(track, clip, Op::fadeOut, "Fondu sortant");
    const char* curves[] = { L("Linéaire", "Linear"), L("Puissance égale", "Equal power"), L("Lent", "Slow"), L("Rapide", "Fast") };
    ImGui::SetNextItemWidth(dp(110.0f));
    ImGui::Combo("##curve", &fadeCurve_, curves, 4);
    ui::tooltip(L("La forme des fondus : droite, à puissance égale, lente au début (exponentielle), rapide au début",
                  "The fades' shape: straight, equal power, slow at first (exponential), fast at first"));
    if (button(L("Normaliser", "Normalize"), L("Le pic de la sélection (ou du clip) au niveau choisi", "The selection's (or clip's) peak at the level chosen")))
        apply(track, clip, Op::normalisePeak, "Normaliser", normaliseDb_);
    ImGui::SetNextItemWidth(dp(70.0f));
    ImGui::DragFloat("##normalise", &normaliseDb_, 0.1f, -24.0f, 0.0f, "%.1f dB");
    ImGui::SameLine(0.0f, dp(8.0f));
    if (button(L("Gain", "Gain"), L("Monte ou baisse la sélection (ou le clip)", "Raises or lowers the selection (or the clip)")))
        apply(track, clip, Op::amplify, "Gain", gainDb_);
    ImGui::SetNextItemWidth(dp(70.0f));
    ImGui::DragFloat("##gain", &gainDb_, 0.1f, -24.0f, 24.0f, "%+.1f dB");
    ImGui::SameLine(0.0f, dp(8.0f));
    if (button(L("Inverser", "Reverse"), L("Joue la sélection à l'envers", "Plays the selection backwards")))
        apply(track, clip, Op::reverse, "Inverser");
    if (button(L("Phase", "Polarity"), L("Inverse la polarité", "Flips the polarity")))
        apply(track, clip, Op::invertPolarity, "Inverser la phase");
    if (button(L("Continu", "DC"), L("Retire la composante continue", "Removes the DC offset")))
        apply(track, clip, Op::removeDcOffset, "Retirer le continu");
    if (button(L("G \xe2\x86\x94 D", "L \xe2\x86\x94 R"), L("Échange les canaux gauche et droit", "Swaps the left and right channels")))
        apply(track, clip, Op::swapStereo, "Echanger les canaux");
    ImGui::SameLine(0.0f, dp(8.0f));
    auto& engine = studio_.engine();
    const auto hearing = previewEnd_.has_value();
    if (button(hearing ? L("Arrêter l'écoute", "Stop listening") : L("Écouter", "Listen"),
               L("Écoute la sélection (ou le clip), telle qu'elle est dans le fichier", "Hears the selection (or the clip) as it is in the file"))) {
        if (hearing) {
            engine.stopSamplePreview();
            previewEnd_.reset();
        } else {
            const auto [start, length] = target(clip);
            if (engine.startSamplePreview(clip.sourceFile, start).wasOk())
                previewEnd_ = start + length;
        }
    }
    ImGui::NewLine();

    // The keys, while the editor has the focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        const auto ctrl = ImGui::GetIO().KeyCtrl;
        if (ctrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            const auto [start, length] = target(clip);
            studio_.copyAudio(clip.sourceFile, start, length);
            apply(track, clip, Op::cut, "Couper");
        } else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            const auto [start, length] = target(clip);
            studio_.copyAudio(clip.sourceFile, start, length);
            report(ui::Ls("Passage copié", "Passage copied"), false);
        } else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && studio_.audioClipboard()) {
            apply(track, clip, Op::paste, "Coller");
        } else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
            selectionStart_ = 0.0;
            selectionEnd_ = clip.sourceLengthSeconds;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            apply(track, clip, Op::silence, "Silence");
        }
    }
}

void AudioEditorView::drawHistory(const TrackRef& track, const AudioClip& clip, const ImVec2 size)
{
    const auto& p = theme::palette();
    ImGui::BeginChild("##history", size, ImGuiChildFlags_Borders);
    ui::sectionTitle(L("HISTORIQUE", "HISTORY"));
    if (clip.versions.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Aucune édition. Chaque édition écrit une nouvelle version du son ; l'original reste.",
                                    "No edit yet. Each edit writes a new version of the sound; the original stays."));
        ImGui::PopTextWrapPos();
    } else {
        for (std::size_t index = 0; index < clip.versions.size(); ++index) {
            const auto& version = clip.versions[index];
            ImGui::PushID(static_cast<int>(index));
            const auto current = index == clip.version;
            char text[160];
            std::snprintf(text, sizeof(text), "%d. %s", static_cast<int>(index), version.label.toRawUTF8());
            if (current)
                ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.accent));
            if (ImGui::Selectable(text, current) && !current) {
                if (studio_.engine().setAudioClipVersion(track.audioIndex, clip.id, index))
                    studio_.markDirty();
            }
            if (current)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%.3f s", version.file.getFileName().toRawUTF8(), version.durationSeconds);
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", L("Un clic rejoue une version ; une édition après elle remplace les suivantes.",
                                    "A click plays a version again; an edit after it replaces the later ones."));
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void AudioEditorView::drawSample(const TrackRef& track, const AudioClip& clip)
{
    const auto& p = theme::palette();
    if (madeOfEvents(clip))
        return;
    drawSampleToolbar(track, clip);
    const auto colour = theme::fromArgb(clip.colour != 0 ? clip.colour : 0xffff9b2fu);
    const auto total = std::max(0.01, clip.sourceLengthSeconds);
    const auto available = ImGui::GetContentRegionAvail();
    const auto historyWidth = std::min(dp(230.0f), available.x * 0.3f);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(available.x - historyWidth - dp(8.0f));
    ImGui::BeginChild("##sampleWave", ImVec2(available.x - historyWidth - dp(8.0f), available.y), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const auto [origin, size] = sourceView("##sampleBand", total, ImGui::GetContentRegionAvail().y);
    const auto played = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
    drawSourceWave(clip.sourceFile, total, origin, size, colour, clip.sourceOffsetSeconds, clip.sourceOffsetSeconds + played);
    auto* list = ImGui::GetWindowDrawList();
    list->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
    // The selection, or the cursor.
    if (selectionEnd_ - selectionStart_ > 1.0e-4) {
        list->AddRectFilled(ImVec2(xOf(selectionStart_), origin.y), ImVec2(xOf(selectionEnd_), origin.y + size.y),
                            theme::alpha(p.accent, 0.22f));
        list->AddLine(ImVec2(xOf(selectionStart_), origin.y), ImVec2(xOf(selectionStart_), origin.y + size.y), p.accent, dp(1.0f));
        list->AddLine(ImVec2(xOf(selectionEnd_), origin.y), ImVec2(xOf(selectionEnd_), origin.y + size.y), p.accent, dp(1.0f));
    } else {
        list->AddLine(ImVec2(xOf(selectionStart_), origin.y), ImVec2(xOf(selectionStart_), origin.y + size.y), p.accent, dp(1.5f));
    }
    if (previewEnd_)
        list->AddLine(ImVec2(xOf(studio_.engine().samplePreviewPositionSeconds()), origin.y),
                      ImVec2(xOf(studio_.engine().samplePreviewPositionSeconds()), origin.y + size.y), p.play, dp(1.5f));
    list->PopClipRect();
    // Dragging selects; a double click selects what the clip plays.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##waveHit", size);
    const auto mouse = std::clamp(secondsAt(ImGui::GetIO().MousePos.x), 0.0, total);
    if (ImGui::IsItemActivated()) {
        selectingFrom_ = ImGui::GetIO().KeyShift ? (std::abs(mouse - selectionStart_) < std::abs(mouse - selectionEnd_) ? selectionEnd_
                                                                                                                   : selectionStart_)
                                                 : mouse;
        selecting_ = true;
    }
    if (selecting_ && ImGui::IsItemActive()) {
        selectionStart_ = std::min(selectingFrom_, mouse);
        selectionEnd_ = std::max(selectingFrom_, mouse);
    }
    if (ImGui::IsItemDeactivated())
        selecting_ = false;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        selectionStart_ = clip.sourceOffsetSeconds;
        selectionEnd_ = clip.sourceOffsetSeconds + played;
    }
    ImGui::EndChild();
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, dp(8.0f));
    drawHistory(track, clip, ImVec2(historyWidth, available.y));
}

// ---- PRISES ------------------------------------------------------------------------------------

void AudioEditorView::drawTakes(const TrackRef& track, const AudioClip& clip)
{
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    if (madeOfEvents(clip))
        return;
    // Its takes: the clip's own source when it has none listed.
    struct TakeView {
        juce::String name;
        juce::File file;
        double shift {};
    };
    std::vector<TakeView> takes;
    for (const auto& take : clip.takes)
        takes.push_back({ take.name, take.sourceFile != juce::File {} ? take.sourceFile : clip.sourceFile, take.shiftSeconds });
    if (takes.empty())
        takes.push_back({ clip.name, clip.sourceFile, 0.0 });
    // The toolbar.
    if (ImGui::SmallButton(L("+ Prise depuis un fichier\xe2\x80\xa6", "+ Take from a file\xe2\x80\xa6"))) {
        const auto clipId = clip.id;
        chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Une prise pour ce clip", "A take for this clip"), juce::File(),
                                                       "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, track, clipId](const juce::FileChooser& chosen) {
                                  const auto file = chosen.getResult();
                                  if (file == juce::File())
                                      return;
                                  std::vector<app::AudioTrackController::AudioTake> added { { {}, file, 0.0, 0.0 } };
                                  const auto done = studio_.engine().addAudioTakes(track.audioIndex, clipId, std::move(added), true);
                                  report(done.wasOk() ? ui::Ls("Prise ajoutée : ", "Take added: ") + file.getFileName() : done.getErrorMessage(),
                                         done.failed());
                                  studio_.markDirty();
                              });
    }
    ui::tooltip(L("Un fichier comme nouvelle prise, calé sur le début du clip", "A file as a new take, lined up with the clip's start"));
    ImGui::SameLine();
    ImGui::BeginDisabled(clip.takes.size() < 2 || clip.comp.empty());
    if (ImGui::SmallButton(L("Mettre à plat", "Flatten"))) {
        const auto done = studio_.flattenAudioClipComp(track, clip.id);
        report(done.wasOk() ? ui::Ls("Prises mises à plat en un fichier (Ctrl+Z l'annule)", "Takes flattened into one file (Ctrl+Z undoes it)")
                            : done.getErrorMessage(),
               done.failed());
    }
    ImGui::EndDisabled();
    ui::tooltip(L("Les meilleurs passages réunis dans un seul fichier, comme ils jouent : le clip garde cette prise seule (les "
                  "éditions d'Échantillon s'y appliquent ensuite)",
                  "The best passages joined into one file, as they play: the clip keeps that take alone (the Sample "
                  "edits then apply to it)"));
    ImGui::SameLine();
    auto onClips = studio_.recordTakesOnClips();
    if (ImGui::Checkbox(L("Enregistrer sur un clip ajoute une prise", "Recording over a clip adds a take"), &onClips))
        studio_.setRecordTakesOnClips(onClips);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", L("En boucle, chaque tour devient une prise. Glissez sur une prise pour jouer ce passage-là ; un clic sur son nom la joue en entier.",
                                "Looped, each pass becomes a take. Drag over a take to play that passage from it; a click on its name plays it all."));

    const auto available = ImGui::GetContentRegionAvail();
    const auto nameWidth = dp(150.0f);
    const auto compHeight = dp(24.0f);
    const auto laneHeight = std::clamp((available.y - compHeight - dp(8.0f)) / static_cast<float>(takes.size()), dp(34.0f), dp(96.0f));
    const auto left = ImGui::GetCursorScreenPos().x + nameWidth;
    const auto width = std::max(dp(80.0f), available.x - nameWidth - dp(4.0f));
    const auto duration = std::max(0.01, clip.durationSeconds);
    const auto xAt = [&](const double t) { return left + static_cast<float>(t / duration) * width; };
    const auto tAt = [&](const float x) { return std::clamp(static_cast<double>(x - left) / width * duration, 0.0, duration); };
    const auto passages = passagesOf(clip);
    auto* list = ImGui::GetWindowDrawList();
    // The comp: which take plays where.
    {
        const auto top = ImGui::GetCursorScreenPos();
        list->AddText(ImVec2(top.x + dp(4.0f), top.y + dp(4.0f)), p.textDim, L("CE QUI JOUE", "WHAT PLAYS"));
        list->AddRectFilled(ImVec2(left, top.y + dp(2.0f)), ImVec2(left + width, top.y + compHeight - dp(2.0f)), p.bg0, dp(3.0f));
        for (const auto& passage : passages) {
            const auto colour = takeColour(passage.take);
            list->AddRectFilled(ImVec2(xAt(passage.startSeconds), top.y + dp(3.0f)), ImVec2(xAt(passage.endSeconds), top.y + compHeight - dp(3.0f)),
                                theme::alpha(colour, 0.85f), dp(2.0f));
            if (xAt(passage.endSeconds) - xAt(passage.startSeconds) > dp(30.0f)) {
                char text[16];
                std::snprintf(text, sizeof(text), "%d", static_cast<int>(passage.take) + 1);
                list->AddText(ImVec2(xAt(passage.startSeconds) + dp(4.0f), top.y + dp(5.0f)), IM_COL32(20, 20, 24, 255), text);
            }
        }
        ImGui::Dummy(ImVec2(available.x, compHeight));
    }
    // Each take: its waveform under the clip's timeline, bright where it plays.
    const auto speed = speedOf(clip);
    std::optional<std::size_t> removing;
    for (std::size_t index = 0; index < takes.size(); ++index) {
        const auto& take = takes[index];
        ImGui::PushID(static_cast<int>(index));
        const auto top = ImGui::GetCursorScreenPos();
        const auto colour = takeColour(index);
        // Its name: a click plays it all.
        const auto isActive = clip.comp.empty() && index == clip.activeTakeIndex;
        ImGui::PushStyleColor(ImGuiCol_Button, isActive ? theme::alpha(colour, 0.45f) : theme::alpha(colour, 0.15f));
        char name[160];
        std::snprintf(name, sizeof(name), "%d  %s##take", static_cast<int>(index) + 1, take.name.toRawUTF8());
        if (ImGui::Button(name, ImVec2(nameWidth - dp(6.0f), laneHeight - dp(4.0f)))) {
            if (engine.setActiveAudioTake(track.audioIndex, clip.id, index) || index == clip.activeTakeIndex) {
                (void) engine.setAudioClipComp(track.audioIndex, clip.id, {});
                studio_.markDirty();
            }
        }
        ImGui::PopStyleColor();
        ui::tooltip(L("Jouer cette prise en entier (clic droit : la retirer)", "Play this take all through (right click: remove it)"));
        if (ImGui::BeginPopupContextItem("##takeMenu")) {
            ImGui::BeginDisabled(takes.size() < 2);
            if (ImGui::MenuItem(L("Retirer cette prise", "Remove this take")))
                removing = index;
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
        // Its wave.
        const ImVec2 waveMin(left, top.y);
        const ImVec2 waveMax(left + width, top.y + laneHeight - dp(4.0f));
        list->AddRectFilled(waveMin, waveMax, p.bg0, dp(3.0f));
        auto* thumbnail = waveforms_.get(take.file);
        if (thumbnail != nullptr) {
            if (!thumbnail->isFullyLoaded())
                host_.wake(2);
            const auto centre = (waveMin.y + waveMax.y) * 0.5f;
            const auto half = (waveMax.y - waveMin.y) * 0.45f;
            for (auto x = 0.0f; x < width; x += 1.0f) {
                const auto t0 = static_cast<double>(x) / width * duration;
                const auto t1 = static_cast<double>(x + 1.0f) / width * duration;
                const auto s0 = clip.sourceOffsetSeconds + t0 * speed + take.shift;
                const auto s1 = clip.sourceOffsetSeconds + t1 * speed + take.shift;
                if (s1 <= 0.0)
                    continue;
                float low = 0.0f;
                float high = 0.0f;
                thumbnail->getApproximateMinMax(std::max(0.0, s0), s1, 0, low, high);
                auto plays = false;
                for (const auto& passage : passages)
                    plays = plays || (passage.take == index && t0 >= passage.startSeconds && t0 < passage.endSeconds);
                list->AddRectFilled(ImVec2(left + x, centre - high * half), ImVec2(left + x + 1.0f, centre - low * half + 1.0f),
                                    plays ? colour : theme::alpha(p.textFaint, 0.45f));
            }
        }
        // Dragging over it: that passage from this take.
        ImGui::SetCursorScreenPos(waveMin);
        ImGui::InvisibleButton("##lane", ImVec2(width, waveMax.y - waveMin.y));
        const auto mouseT = tAt(ImGui::GetIO().MousePos.x);
        if (ImGui::IsItemActivated()) {
            compLane_ = static_cast<int>(index);
            compFrom_ = compTo_ = mouseT;
        }
        if (compLane_ == static_cast<int>(index) && ImGui::IsItemActive())
            compTo_ = mouseT;
        if (compLane_ == static_cast<int>(index)) {
            const auto a = std::min(compFrom_, compTo_);
            const auto b = std::max(compFrom_, compTo_);
            list->AddRectFilled(ImVec2(xAt(a), waveMin.y), ImVec2(xAt(b), waveMax.y), theme::alpha(p.accent, 0.25f));
        }
        if (compLane_ == static_cast<int>(index) && ImGui::IsItemDeactivated()) {
            const auto a = std::min(compFrom_, compTo_);
            const auto b = std::max(compFrom_, compTo_);
            compLane_ = -1;
            if (b - a > 0.01) {
                // The passage taken from the others, given to this take.
                std::vector<app::AudioTrackController::CompSegment> next;
                for (const auto& passage : passages) {
                    if (passage.endSeconds <= a || passage.startSeconds >= b) {
                        next.push_back(passage);
                        continue;
                    }
                    if (passage.startSeconds < a)
                        next.push_back({ passage.startSeconds, a, passage.take });
                    if (passage.endSeconds > b)
                        next.push_back({ b, passage.endSeconds, passage.take });
                }
                next.push_back({ a, b, index });
                if (engine.setAudioClipComp(track.audioIndex, clip.id, std::move(next)))
                    studio_.markDirty();
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + laneHeight));
        ImGui::PopID();
    }
    if (removing) {
        if (engine.removeAudioTake(track.audioIndex, clip.id, *removing)) {
            (void) engine.setAudioClipComp(track.audioIndex, clip.id, {});
            studio_.markDirty();
        }
    }
}

// ---- ÉTIREMENT ---------------------------------------------------------------------------------

void AudioEditorView::drawStretch(const TrackRef& track, const AudioClip& clip)
{
    using Mode = app::AudioTrackController::StretchMode;
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto index = track.audioIndex;
    if (madeOfEvents(clip))
        return;
    // The algorithm.
    struct Choice {
        Mode mode;
        const char* french;
        const char* english;
        const char* tipFrench;
        const char* tipEnglish;
    };
    const Choice choices[] = {
        { Mode::repitch, "Vitesse", "Repitch", "Lu plus vite ou plus lentement : la hauteur suit, comme une bande.",
          "Read faster or slower: the pitch follows, as a tape does." },
        { Mode::tonal, "Tonal", "Tonal", "Garde la hauteur : voix, accords, nappes (vocodeur de phase aux phases liées).",
          "Keeps the pitch: voices, chords, pads (a phase vocoder, its phases locked)." },
        { Mode::beats, "Rythme", "Beats", "Coupe aux attaques et joue chaque tranche telle quelle : les batteries gardent leur punch.",
          "Cuts at the attacks and plays each slice as it is: drums keep their punch." },
        { Mode::texture, "Texture", "Texture", "Étale le son en nappe, pour étirer dix fois et plus.",
          "Smears the sound into a pad, to stretch ten times and more." },
    };
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", L("Algorithme", "Algorithm"));
    for (const auto& choice : choices) {
        ImGui::SameLine(0.0f, dp(4.0f));
        auto on = clip.stretchMode == choice.mode;
        if (ui::toggle(ui::french() ? choice.french : choice.english, &on, p.accent)
            && engine.setAudioClipStretchSettings(index, clip.id, clip.sourceTempoBpm, choice.mode))
            studio_.markDirty();
        ui::tooltip(ui::french() ? choice.tipFrench : choice.tipEnglish);
    }
    // The transposition.
    ImGui::SameLine(0.0f, dp(18.0f));
    ImGui::TextDisabled("%s", L("Transposition", "Transpose"));
    ImGui::SameLine();
    auto semitones = static_cast<float>(std::round(clip.pitchSemitones));
    auto cents = static_cast<float>(std::round((clip.pitchSemitones - std::round(clip.pitchSemitones)) * 100.0));
    ImGui::BeginDisabled(clip.stretchMode == Mode::repitch);
    auto moved = ui::dragNumber("##semitones", &semitones, 0.1f, -24.0f, 24.0f, "%+.0f st", dp(64.0f));
    ImGui::SameLine();
    moved |= ui::dragNumber("##cents", &cents, 0.5f, -50.0f, 50.0f, "%+.0f ct", dp(64.0f));
    ImGui::EndDisabled();
    if (moved && engine.setAudioClipPitch(index, clip.id, std::round(semitones) + std::round(cents) / 100.0))
        studio_.markDirty();
    if (clip.stretchMode == Mode::repitch)
        ui::tooltip(L("En Vitesse, la hauteur suit la vitesse : choisissez un autre algorithme pour transposer",
                      "With Repitch the pitch follows the speed: pick another algorithm to transpose"));

    // The tempo: its own, found, or the project's.
    auto tempo = static_cast<float>(clip.sourceTempoBpm);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", L("Tempo du son", "Sound's tempo"));
    ImGui::SameLine();
    if (ui::dragNumber("##tempo", &tempo, 0.05f, 0.0f, 400.0f, tempo > 0.0f ? "%.2f BPM" : "?", dp(90.0f))
        && engine.setAudioClipStretchSettings(index, clip.id, tempo, clip.stretchMode))
        studio_.markDirty();
    ImGui::SameLine();
    if (ImGui::SmallButton(L("Détecter", "Detect"))) {
        if (const auto found = app::AudioTrackController::detectAudioTempo(clip.sourceFile)) {
            if (engine.setAudioClipStretchSettings(index, clip.id, found->bpm, clip.stretchMode))
                studio_.markDirty();
            report(ui::Ls("Tempo trouvé : ", "Tempo found: ") + juce::String(found->bpm, 2) + " BPM", false);
        } else {
            report(ui::Ls("Aucun tempo net dans ce son", "No clear tempo in this sound"), true);
        }
    }
    ImGui::SameLine();
    const auto projectTempo = engine.tempo();
    char follow[96];
    std::snprintf(follow, sizeof(follow), "%s (%.1f BPM)", L("Caler sur le tempo du projet", "Fit to the project's tempo"), projectTempo);
    ImGui::BeginDisabled(clip.sourceTempoBpm <= 0.0);
    if (ImGui::SmallButton(follow)) {
        const auto source = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
        if (engine.stretchAudioClip(index, clip.id, source * clip.sourceTempoBpm / std::max(1.0, projectTempo)))
            studio_.markDirty();
    }
    ImGui::EndDisabled();
    ui::tooltip(L("La durée du clip faite pour que son tempo devienne celui du projet", "The clip's length made so its tempo becomes the project's"));
    ImGui::SameLine(0.0f, dp(14.0f));
    auto percent = static_cast<float>(100.0 / speedOf(clip));
    ImGui::TextDisabled("%s", L("Durée", "Length"));
    ImGui::SameLine();
    if (ui::dragNumber("##percent", &percent, 0.2f, 5.0f, 2'000.0f, "%.1f %%", dp(80.0f))) {
        const auto source = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
        if (engine.stretchAudioClip(index, clip.id, source * std::max(5.0f, percent) / 100.0))
            studio_.markDirty();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(clip.stretchMarkers.empty());
    if (ImGui::SmallButton(L("Effacer les repères", "Clear the markers"))) {
        for (const auto& marker : clip.stretchMarkers)
            (void) engine.removeAudioClipStretchMarker(index, clip.id, marker.id);
        studio_.markDirty();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%s", L("Repères : double-clic sur l'onde pour en poser un, glisser sa poignée pour caler ce moment ailleurs, clic droit pour le retirer.",
                                "Markers: double click on the wave to set one, drag its handle to move that moment elsewhere, right click to remove it."));

    // The clip's timeline, its wave read through the markers.
    const auto available = ImGui::GetContentRegionAvail();
    const auto top = ImGui::GetCursorScreenPos();
    const auto duration = std::max(0.01, clip.durationSeconds);
    const ImVec2 waveMin(top.x, top.y + dp(14.0f));
    const ImVec2 waveMax(top.x + available.x, top.y + std::max(dp(60.0f), available.y - dp(4.0f)));
    const auto xAt = [&](const double t) { return waveMin.x + static_cast<float>(t / duration) * (waveMax.x - waveMin.x); };
    const auto tAt = [&](const float x) { return std::clamp(static_cast<double>(x - waveMin.x) / (waveMax.x - waveMin.x) * duration, 0.0, duration); };
    auto* list = ImGui::GetWindowDrawList();
    const auto colour = theme::fromArgb(clip.colour != 0 ? clip.colour : 0xffff9b2fu);
    list->AddRectFilled(waveMin, waveMax, p.bg0, dp(3.0f));
    // Its beats at the project's tempo, to line up with.
    const auto beat = 60.0 / std::max(1.0, projectTempo);
    for (auto t = 0.0; t < duration; t += beat)
        list->AddLine(ImVec2(xAt(t), waveMin.y), ImVec2(xAt(t), waveMax.y), theme::alpha(p.textFaint, 0.18f));
    if (auto* thumbnail = waveforms_.get(clip.sourceFile)) {
        if (!thumbnail->isFullyLoaded())
            host_.wake(2);
        const auto centre = (waveMin.y + waveMax.y) * 0.5f;
        const auto half = (waveMax.y - waveMin.y) * 0.45f;
        const auto width = waveMax.x - waveMin.x;
        for (auto x = 0.0f; x < width; x += 1.0f) {
            const auto s0 = sourceAt(clip, static_cast<double>(x) / width * duration);
            const auto s1 = sourceAt(clip, static_cast<double>(x + 1.0f) / width * duration);
            float low = 0.0f;
            float high = 0.0f;
            thumbnail->getApproximateMinMax(std::min(s0, s1), std::max(s0, s1) + 1.0e-4, 0, low, high);
            list->AddRectFilled(ImVec2(waveMin.x + x, centre - high * half), ImVec2(waveMin.x + x + 1.0f, centre - low * half + 1.0f),
                                theme::brighter(colour, 0.15f));
        }
    }
    // The markers: a line and a handle above.
    std::optional<std::uint64_t> hovered;
    const auto mouse = ImGui::GetIO().MousePos;
    for (const auto& marker : clip.stretchMarkers) {
        const auto x = xAt(marker.timelineSeconds);
        const auto near = std::abs(mouse.x - x) < dp(6.0f) && mouse.y >= top.y && mouse.y <= waveMax.y;
        if (near)
            hovered = marker.id;
        const auto tint = marker.id == markerDragged_ || near ? p.accent : theme::alpha(p.accent, 0.7f);
        list->AddLine(ImVec2(x, waveMin.y), ImVec2(x, waveMax.y), tint, dp(1.4f));
        list->AddTriangleFilled(ImVec2(x - dp(6.0f), top.y), ImVec2(x + dp(6.0f), top.y), ImVec2(x, waveMin.y), tint);
    }
    ImGui::SetCursorScreenPos(top);
    ImGui::InvisibleButton("##stretchHit", ImVec2(available.x, waveMax.y - top.y));
    if (ImGui::IsItemActivated() && hovered)
        markerDragged_ = *hovered;
    if (markerDragged_ != 0 && ImGui::IsItemActive()) {
        // Between its neighbours.
        auto low = 0.001;
        auto high = duration - 0.001;
        for (const auto& marker : clip.stretchMarkers)
            if (marker.id != markerDragged_) {
                const auto at = std::find_if(clip.stretchMarkers.begin(), clip.stretchMarkers.end(),
                                             [this](const auto& m) { return m.id == markerDragged_; });
                if (at == clip.stretchMarkers.end())
                    break;
                if (marker.sourceSeconds < at->sourceSeconds)
                    low = std::max(low, marker.timelineSeconds + 0.005);
                else
                    high = std::min(high, marker.timelineSeconds - 0.005);
            }
        (void) engine.updateAudioClipStretchMarker(index, clip.id, markerDragged_, std::clamp(tAt(mouse.x), low, high));
    }
    if (ImGui::IsItemDeactivated() && markerDragged_ != 0) {
        markerDragged_ = 0;
        studio_.markDirty();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !hovered) {
        const auto t = tAt(mouse.x);
        if (engine.addAudioClipStretchMarker(index, clip.id, sourceAt(clip, t), t) != 0)
            studio_.markDirty();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hovered) {
        (void) engine.removeAudioClipStretchMarker(index, clip.id, *hovered);
        studio_.markDirty();
    }
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
}

// ---- ÉVÉNEMENTS --------------------------------------------------------------------------------

bool AudioEditorView::madeOfEvents(const AudioClip& clip)
{
    if (!clip.usesEventTimeline)
        return false;
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", L("Ce clip est fait de plusieurs sons : chacun se règle dans Événements (sa place, ses bords, son gain, "
                                "ses fondus, son étirement, ses éditions).",
                                "This clip is made of several sounds: each is set in Events (its place, its edges, its gain, its "
                                "fades, its stretch, its edits)."));
    ImGui::PopTextWrapPos();
    if (ImGui::SmallButton(L("Aller aux événements", "Go to the events")))
        mode_ = Mode::events;
    return true;
}

void AudioEditorView::addEvent(const TrackRef& track, const AudioClip& clip, const juce::File& file, const double at)
{
    if (!clip.usesEventTimeline && (clip.takes.size() > 1 || !clip.comp.empty())) {
        report(ui::Ls("Ce clip a plusieurs prises : mettez-les d'abord à plat (face Prises)",
                      "This clip has several takes: flatten them first (Takes face)"),
               true);
        return;
    }
    std::uint64_t added {};
    const auto done = studio_.engine().addAudioClipEventFromFile(track.audioIndex, clip.id, file, std::max(0.0, at), &added);
    if (done.failed()) {
        report(done.getErrorMessage(), true);
        return;
    }
    selectedEvent_ = added;
    studio_.markDirty();
    report(ui::Ls("Son ajouté au clip : ", "Sound added to the clip: ") + file.getFileName(), false);
}

void AudioEditorView::drawEventsToolbar(const TrackRef& track, const AudioClip& clip,
                                        const std::vector<app::AudioTrackController::AudioEvent>& events)
{
    using Mode = app::AudioTrackController::StretchMode;
    auto& engine = studio_.engine();
    const auto index = track.audioIndex;
    const auto button = [](const char* label, const char* tip) {
        const auto clicked = ImGui::SmallButton(label);
        ui::tooltip(tip);
        ImGui::SameLine(0.0f, dp(3.0f));
        return clicked;
    };
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s %.3f s", L("Curseur", "Cursor"), eventCursor_);
    ImGui::SameLine(0.0f, dp(10.0f));
    if (button(L("+ Son…", "+ Sound…"), L("Un fichier ajouté au clip, au curseur (ou glissez un son du navigateur sur la vue)",
                                          "A file added to the clip, at the cursor (or drag a sound from the browser onto the view)"))) {
        const auto clipId = clip.id;
        const auto at = eventCursor_;
        chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Un son pour ce clip", "A sound for this clip"), juce::File(),
                                                       "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, track, clipId, at](const juce::FileChooser& chosen) {
                                  const auto file = chosen.getResult();
                                  if (file == juce::File())
                                      return;
                                  if (const auto clip = studio_.engine().audioClip(track.audioIndex, clipId))
                                      addEvent(track, *clip, file, at);
                              });
    }
    const auto selected = std::find_if(events.begin(), events.end(), [this](const auto& event) { return event.id == selectedEvent_; });
    const auto hasSelected = selected != events.end();
    // The event under the cursor when none is selected.
    auto cutting = selected;
    if (!hasSelected)
        cutting = std::find_if(events.begin(), events.end(), [this](const auto& event) {
            return eventCursor_ > event.startSeconds && eventCursor_ < event.startSeconds + event.durationSeconds;
        });
    ImGui::BeginDisabled(cutting == events.end());
    if (button(L("Couper au curseur", "Split at the cursor"), L("Coupe l'événement en deux au curseur (S)", "Splits the event in two at the cursor (S)"))
        && cutting != events.end()) {
        if (const auto made = engine.splitAudioClipEvent(index, clip.id, cutting->id, eventCursor_); made != 0) {
            selectedEvent_ = made;
            studio_.markDirty();
        }
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!hasSelected);
    if (button(L("Dupliquer", "Duplicate"), L("Une copie de l'événement juste après lui (Ctrl+D)", "A copy of the event right after it (Ctrl+D)"))
        && hasSelected) {
        auto all = events;
        auto copy = *selected;
        copy.id = 0;
        copy.startSeconds = selected->startSeconds + selected->durationSeconds;
        for (auto& marker : copy.stretchMarkers)
            marker.id = 0;
        all.push_back(copy);
        if (const auto done = engine.replaceAudioClipEvents(index, clip.id, std::move(all)); done.wasOk())
            studio_.markDirty();
        else
            report(done.getErrorMessage(), true);
    }
    if (button(L("Supprimer", "Remove"), L("Retire l'événement du clip (Suppr)", "Takes the event out of the clip (Delete)")) && hasSelected) {
        if (engine.removeAudioClipEvent(index, clip.id, selected->id)) {
            selectedEvent_ = 0;
            studio_.markDirty();
        }
    }
    ImGui::EndDisabled();
    if (!hasSelected) {
        ImGui::SameLine(0.0f, dp(10.0f));
        ImGui::TextDisabled("%s", L("Cliquez un événement pour le régler ; glissez-le pour le déplacer, ses bords pour le raccourcir.",
                                    "Click an event to set it; drag it to move it, its edges to shorten it."));
        ImGui::NewLine();
        return;
    }
    ImGui::NewLine();
    // The selected one: its name, gain, fades, stretch, edits.
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted((selected->name.isNotEmpty() ? selected->name : selected->sourceFile.getFileNameWithoutExtension()).toRawUTF8());
    ImGui::PopFont();
    ImGui::SameLine(0.0f, dp(12.0f));
    // One undo step for what a drag of a field does.
    const auto batch = [this, &engine] {
        if (ImGui::IsItemActivated() && !eventFieldBatch_) {
            engine.beginArrangementEditBatch();
            eventFieldBatch_ = true;
        }
        if (ImGui::IsItemDeactivated() && eventFieldBatch_) {
            (void) engine.endArrangementEditBatch();
            eventFieldBatch_ = false;
            studio_.markDirty();
        }
    };
    auto gain = selected->gainDb;
    ImGui::TextDisabled("%s", L("Gain", "Gain"));
    ImGui::SameLine();
    if (ui::dragNumber("##eventGain", &gain, 0.1f, -60.0f, 24.0f, "%+.1f dB", dp(76.0f)))
        (void) engine.setAudioClipEventProperties(index, clip.id, selected->id, gain, selected->sourceTempoBpm, selected->stretchMode);
    batch();
    ImGui::SameLine(0.0f, dp(10.0f));
    auto fadeIn = static_cast<float>(selected->fadeInSeconds);
    auto fadeOut = static_cast<float>(selected->fadeOutSeconds);
    const auto half = static_cast<float>(selected->durationSeconds);
    ImGui::TextDisabled("%s", L("Fondu entrée", "Fade in"));
    ImGui::SameLine();
    if (ui::dragNumber("##eventFadeIn", &fadeIn, 0.002f, 0.0f, half, "%.3f s", dp(70.0f)))
        (void) engine.updateAudioClipEvent(index, clip.id, selected->id, selected->startSeconds, selected->sourceOffsetSeconds,
                                           selected->durationSeconds, selected->sourceDurationSeconds, fadeIn, -1.0);
    batch();
    ImGui::SameLine(0.0f, dp(6.0f));
    ImGui::TextDisabled("%s", L("sortie", "out"));
    ImGui::SameLine();
    if (ui::dragNumber("##eventFadeOut", &fadeOut, 0.002f, 0.0f, half, "%.3f s", dp(70.0f)))
        (void) engine.updateAudioClipEvent(index, clip.id, selected->id, selected->startSeconds, selected->sourceOffsetSeconds,
                                           selected->durationSeconds, selected->sourceDurationSeconds, -1.0, fadeOut);
    batch();
    ImGui::SameLine(0.0f, dp(10.0f));
    const char* modes[] = { L("Vitesse", "Repitch"), L("Tonal", "Tonal"), L("Rythme", "Beats"), L("Texture", "Texture") };
    auto mode = static_cast<int>(selected->stretchMode);
    ImGui::SetNextItemWidth(dp(90.0f));
    if (ImGui::Combo("##eventStretch", &mode, modes, 4)
        && engine.setAudioClipEventProperties(index, clip.id, selected->id, selected->gainDb, selected->sourceTempoBpm,
                                              static_cast<Mode>(std::clamp(mode, 0, 3))))
        studio_.markDirty();
    ui::tooltip(L("Comment il s'étire quand on étire le clip", "How it stretches when the clip is stretched"));
    ImGui::SameLine(0.0f, dp(8.0f));
    // Its own transposition, its length kept (not with Repitch): set when
    // the field is let go, one undo step.
    ImGui::BeginDisabled(selected->stretchMode == Mode::repitch);
    if (!eventPitchEditing_)
        eventPitchEdit_ = static_cast<float>(selected->pitchSemitones);
    auto semitones = eventPitchEdit_;
    if (ui::dragNumber("##eventPitch", &semitones, 0.05f, -24.0f, 24.0f, "%+.1f st", dp(64.0f)))
        eventPitchEdit_ = semitones;
    eventPitchEditing_ = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        const auto pitch = std::round(static_cast<double>(eventPitchEdit_) * 10.0) / 10.0;
        // A clip of one sound: the clip's own transposition.
        if (!clip.usesEventTimeline) {
            if (engine.setAudioClipPitch(index, clip.id, pitch))
                studio_.markDirty();
        } else {
            auto all = events;
            for (auto& event : all)
                if (event.id == selected->id)
                    event.pitchSemitones = pitch;
            if (const auto done = engine.replaceAudioClipEvents(index, clip.id, std::move(all)); done.wasOk())
                studio_.markDirty();
            else
                report(done.getErrorMessage(), true);
        }
    }
    ImGui::EndDisabled();
    ui::tooltip(selected->stretchMode == Mode::repitch
                    ? L("En Vitesse, la hauteur suit la vitesse : choisissez un autre algorithme pour transposer",
                        "With Repitch the pitch follows the speed: pick another algorithm to transpose")
                    : L("Sa transposition, sa durée gardée", "Its transposition, its length kept"));
    ImGui::SameLine(0.0f, dp(10.0f));
    // Edited in place: a new file, its length kept.
    const auto editEvent = [&](const app::AudioEditOperation operation, const float levelDb, const char* label) {
        app::AudioEditRequest request;
        request.operation = operation;
        request.levelDb = levelDb;
        // A clip of one sound is edited as the Sample face does (its
        // history kept); an event of several, on its own file.
        const auto done = clip.usesEventTimeline
                              ? studio_.editAudioEvent(track, clip.id, selected->id, request)
                              : studio_.editAudioClip(track, clip.id, request, selected->sourceOffsetSeconds,
                                                      selected->sourceDurationSeconds, juce::String::fromUTF8(label));
        report(done.wasOk() ? juce::String::fromUTF8(label) + ui::Ls(" : fait (Ctrl+Z l'annule)", ": done (Ctrl+Z undoes it)")
                            : done.getErrorMessage(),
               done.failed());
    };
    if (button(L("Normaliser", "Normalize"), L("Son pic à -1 dB", "Its peak at -1 dB")))
        editEvent(app::AudioEditOperation::normalisePeak, -1.0f, "Normaliser");
    if (button(L("Inverser", "Reverse"), L("Joué à l'envers", "Played backwards")))
        editEvent(app::AudioEditOperation::reverse, 0.0f, "Inverser");
    if (button(L("Phase", "Polarity"), L("Sa polarité inversée", "Its polarity flipped")))
        editEvent(app::AudioEditOperation::invertPolarity, 0.0f, "Inverser la phase");
    if (button(L("Écouter", "Listen"), L("Écoute l'événement tel qu'il est dans son fichier", "Hears the event as it is in its file"))) {
        if (engine.startSamplePreview(selected->sourceFile, selected->sourceOffsetSeconds).wasOk())
            previewEnd_ = selected->sourceOffsetSeconds + selected->sourceDurationSeconds;
    }
    ImGui::NewLine();
}

void AudioEditorView::drawEvents(const TrackRef& track, const AudioClip& clip)
{
    using Event = app::AudioTrackController::AudioEvent;
    const auto& p = theme::palette();
    auto& engine = studio_.engine();
    const auto index = track.audioIndex;
    // A clip of one sound shows that sound as its only event: moving it or
    // adding another makes the clip a timeline of events.
    std::vector<Event> events = clip.usesEventTimeline ? clip.events : std::vector<Event> {};
    if (!clip.usesEventTimeline && clip.sourceFile != juce::File {}) {
        Event own;
        own.id = clip.id;
        own.name = clip.name.isNotEmpty() ? clip.name : clip.sourceFile.getFileNameWithoutExtension();
        own.sourceOffsetSeconds = clip.sourceOffsetSeconds;
        own.durationSeconds = clip.durationSeconds;
        own.sourceDurationSeconds = clip.sourceDurationSeconds > 0.0 ? clip.sourceDurationSeconds : clip.durationSeconds;
        own.colour = clip.colour;
        own.sourceFile = clip.sourceFile;
        own.sourceLengthSeconds = clip.sourceLengthSeconds;
        own.fadeInSeconds = clip.fadeInSeconds;
        own.fadeOutSeconds = clip.fadeOutSeconds;
        own.stretchMode = clip.stretchMode;
        own.stretchMarkers = clip.stretchMarkers;
        own.pitchSemitones = clip.pitchSemitones;
        events.push_back(own);
    }
    if (std::none_of(events.begin(), events.end(), [this](const Event& event) { return event.id == selectedEvent_; }))
        selectedEvent_ = 0;
    drawEventsToolbar(track, clip, events);
    if (!clip.usesEventTimeline && (clip.takes.size() > 1 || !clip.comp.empty())) {
        ImGui::TextDisabled("%s", L("Ce clip a plusieurs prises : mettez-les à plat (face Prises) pour en faire des événements.",
                                    "This clip has several takes: flatten them (Takes face) to make events of it."));
        return;
    }

    // The view: the clip's own timeline, a little past its end.
    const auto total = std::max(0.05, clip.durationSeconds * 1.08);
    const auto [origin, size] = sourceView("##eventsBand", total, ImGui::GetContentRegionAvail().y);
    auto* list = ImGui::GetWindowDrawList();
    const auto end = ImVec2(origin.x + size.x, origin.y + size.y);
    // Rows: an event goes on the first row free at its start.
    std::vector<std::size_t> order(events.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(), [&events](const std::size_t a, const std::size_t b) {
        return events[a].startSeconds < events[b].startSeconds;
    });
    std::vector<int> rowOf(events.size(), 0);
    std::vector<double> rowEnds;
    for (const auto i : order) {
        auto row = 0;
        while (row < static_cast<int>(rowEnds.size()) && rowEnds[static_cast<std::size_t>(row)] > events[i].startSeconds + 1.0e-6)
            ++row;
        if (row == static_cast<int>(rowEnds.size()))
            rowEnds.push_back(0.0);
        rowEnds[static_cast<std::size_t>(row)] = events[i].startSeconds + events[i].durationSeconds;
        rowOf[i] = row;
    }
    const auto rows = std::max<std::size_t>(1, rowEnds.size());
    const auto rowHeight = std::clamp(size.y / static_cast<float>(rows), dp(30.0f), dp(140.0f));
    // The clip's end, and the project's beats.
    list->PushClipRect(origin, end, true);
    const auto beat = 60.0 / std::max(1.0, engine.tempo());
    const auto firstBeat = std::ceil(clip.startSeconds / beat) * beat - clip.startSeconds;
    for (auto t = firstBeat; t < total; t += beat)
        list->AddLine(ImVec2(xOf(t), origin.y), ImVec2(xOf(t), end.y), theme::alpha(p.textFaint, 0.15f));
    list->AddRectFilled(ImVec2(xOf(clip.durationSeconds), origin.y), end, theme::alpha(p.bg2, 0.6f));

    // Where an event is drawn: as it is, or as the drag puts it.
    struct Shown {
        double start {};
        double offset {};
        double duration {};
        double sourceDuration {};
    };
    const auto shownOf = [this](const Event& event) {
        if (eventDrag_ != EventDrag::none && event.id == draggedEvent_)
            return Shown { dragStart_, dragOffset_, dragDuration_, dragSourceDuration_ };
        return Shown { event.startSeconds, event.sourceOffsetSeconds, event.durationSeconds, event.sourceDurationSeconds };
    };
    const auto mouse = ImGui::GetIO().MousePos;
    std::optional<std::size_t> hovered;
    auto hoveredPart = EventDrag::move;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i];
        const auto shown = shownOf(event);
        const auto top = origin.y + rowHeight * static_cast<float>(rowOf[i]);
        const ImVec2 min(xOf(shown.start), top + dp(1.0f));
        const ImVec2 max(std::max(min.x + 2.0f, xOf(shown.start + shown.duration)), top + rowHeight - dp(1.0f));
        const auto colour = theme::fromArgb(event.colour != 0 ? event.colour : 0xff53d0c2u);
        const auto isSelected = event.id == selectedEvent_;
        list->AddRectFilled(min, max, theme::mix(colour, p.bg0 | 0xff000000u, 0.68f), dp(3.0f));
        // Its wave, through its warp markers, at its gain.
        if (auto* thumbnail = waveforms_.get(event.sourceFile)) {
            if (!thumbnail->isFullyLoaded())
                host_.wake(2);
            auto mapped = event;
            mapped.sourceOffsetSeconds = shown.offset;
            mapped.durationSeconds = shown.duration;
            mapped.sourceDurationSeconds = shown.sourceDuration;
            const auto gain = std::min(2.0f, juce::Decibels::decibelsToGain(event.gainDb));
            const auto centre = (min.y + max.y) * 0.5f + dp(5.0f);
            const auto halfHeight = (max.y - min.y - dp(12.0f)) * 0.48f;
            for (auto x = std::max(min.x, origin.x); x < std::min(max.x, end.x); x += 1.0f) {
                const auto t0 = secondsAt(x) - shown.start;
                const auto t1 = secondsAt(x + 1.0f) - shown.start;
                const auto s0 = app::AudioTrackController::eventSourceTime(mapped, t0);
                const auto s1 = app::AudioTrackController::eventSourceTime(mapped, t1);
                float low = 0.0f;
                float high = 0.0f;
                thumbnail->getApproximateMinMax(std::min(s0, s1), std::max(s0, s1) + 1.0e-4, 0, low, high);
                list->AddRectFilled(ImVec2(x, centre - std::min(1.0f, high * gain) * halfHeight),
                                    ImVec2(x + 1.0f, centre - std::max(-1.0f, low * gain) * halfHeight + 1.0f),
                                    theme::brighter(colour, 0.15f));
            }
        }
        // Its fades.
        if (event.fadeInSeconds > 0.0)
            list->AddLine(ImVec2(min.x, max.y), ImVec2(xOf(shown.start + event.fadeInSeconds), min.y + dp(12.0f)),
                          theme::alpha(p.text, 0.6f));
        if (event.fadeOutSeconds > 0.0)
            list->AddLine(ImVec2(xOf(shown.start + shown.duration - event.fadeOutSeconds), min.y + dp(12.0f)), ImVec2(max.x, max.y),
                          theme::alpha(p.text, 0.6f));
        // Its name, its gain.
        ImGui::PushFont(nullptr, ui::currentFontBase() * 0.78f);
        auto label = event.name.isNotEmpty() ? event.name : event.sourceFile.getFileNameWithoutExtension();
        if (std::abs(event.gainDb) > 0.05f)
            label << "  " << (event.gainDb > 0 ? "+" : "") << juce::String(event.gainDb, 1) << " dB";
        ui::clippedText(list, ImVec2(min.x + dp(4.0f), min.y + dp(1.0f)), max.x - min.x - dp(8.0f), p.text, label.toRawUTF8());
        ImGui::PopFont();
        list->AddRect(min, max, isSelected ? p.text : theme::alpha(colour, 0.9f), dp(3.0f), isSelected ? dp(1.8f) : dp(1.0f));
        if (mouse.x >= min.x - dp(3.0f) && mouse.x <= max.x + dp(3.0f) && mouse.y >= min.y && mouse.y <= max.y) {
            hovered = i;
            const auto edge = std::min(dp(6.0f), (max.x - min.x) * 0.25f);
            hoveredPart = mouse.x < min.x + edge ? EventDrag::trimStart : mouse.x > max.x - edge ? EventDrag::trimEnd : EventDrag::move;
        }
    }
    // The cursor.
    list->AddLine(ImVec2(xOf(eventCursor_), origin.y), ImVec2(xOf(eventCursor_), end.y), p.accent, dp(1.5f));
    if (previewEnd_)
        list->AddLine(ImVec2(xOf(engine.samplePreviewPositionSeconds()), origin.y),
                      ImVec2(xOf(engine.samplePreviewPositionSeconds()), end.y), p.play, dp(1.0f));
    list->PopClipRect();

    // Input: on an event, select it and take it (its body moves it, its
    // edges trim it); elsewhere, the cursor.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##eventsHit", size);
    // A sound dropped from the browser lands where it is let go.
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1)
            addEvent(track, clip, juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data))),
                     std::max(0.0, secondsAt(mouse.x)));
        ImGui::EndDragDropTarget();
    }
    const auto at = std::max(0.0, secondsAt(mouse.x));
    if (ImGui::IsItemActivated()) {
        if (hovered) {
            const auto& event = events[*hovered];
            selectedEvent_ = event.id;
            eventDrag_ = hoveredPart;
            draggedEvent_ = event.id;
            dragGrab_ = at;
            dragOriginStart_ = dragStart_ = event.startSeconds;
            dragOriginOffset_ = dragOffset_ = event.sourceOffsetSeconds;
            dragOriginDuration_ = dragDuration_ = event.durationSeconds;
            dragOriginSourceDuration_ = dragSourceDuration_ = event.sourceDurationSeconds;
            dragSourceLength_ = event.sourceLengthSeconds > 0.0 ? event.sourceLengthSeconds
                                                                : event.sourceOffsetSeconds + event.sourceDurationSeconds;
        } else {
            selectedEvent_ = 0;
            eventCursor_ = std::min(at, clip.durationSeconds);
        }
    }
    if (eventDrag_ != EventDrag::none && ImGui::IsItemActive()) {
        // A magnet: the other events' edges and the cursor, unless Shift.
        const auto snap = [&](const double t) {
            if (ImGui::GetIO().KeyShift)
                return t;
            auto best = t;
            auto distance = static_cast<double>(dp(7.0f)) / std::max(1.0e-9, pixelsPerSecond_);
            const auto consider = [&](const double candidate) {
                if (std::abs(candidate - t) < distance) {
                    distance = std::abs(candidate - t);
                    best = candidate;
                }
            };
            consider(0.0);
            consider(eventCursor_);
            for (const auto& other : events)
                if (other.id != draggedEvent_) {
                    consider(other.startSeconds);
                    consider(other.startSeconds + other.durationSeconds);
                }
            return best;
        };
        const auto ratio = dragOriginSourceDuration_ / std::max(1.0e-6, dragOriginDuration_);
        const auto delta = at - dragGrab_;
        if (eventDrag_ == EventDrag::move) {
            auto start = snap(std::max(0.0, dragOriginStart_ + delta));
            // Its end on a magnet too.
            const auto endSnapped = snap(start + dragOriginDuration_);
            if (std::abs(endSnapped - (start + dragOriginDuration_)) > 1.0e-9 && std::abs(start - (dragOriginStart_ + delta)) < 1.0e-9)
                start = std::max(0.0, endSnapped - dragOriginDuration_);
            dragStart_ = start;
        } else if (eventDrag_ == EventDrag::trimStart) {
            // Its start moved, its sound staying where it is: as far back
            // as its file goes, never past its end.
            const auto earliest = std::max(0.0, dragOriginStart_ - dragOriginOffset_ / std::max(1.0e-6, ratio));
            const auto start = std::clamp(snap(dragOriginStart_ + delta), earliest, dragOriginStart_ + dragOriginDuration_ - 0.01);
            dragStart_ = start;
            dragOffset_ = std::max(0.0, dragOriginOffset_ + (start - dragOriginStart_) * ratio);
            dragDuration_ = dragOriginStart_ + dragOriginDuration_ - start;
            dragSourceDuration_ = dragDuration_ * ratio;
        } else {
            const auto longest = (dragSourceLength_ - dragOriginOffset_) / std::max(1.0e-6, ratio);
            const auto stop = std::clamp(snap(dragOriginStart_ + dragOriginDuration_ + delta), dragOriginStart_ + 0.01,
                                         dragOriginStart_ + std::max(0.01, longest));
            dragDuration_ = stop - dragOriginStart_;
            dragSourceDuration_ = dragDuration_ * ratio;
        }
    }
    if (ImGui::IsItemDeactivated() && eventDrag_ != EventDrag::none) {
        const auto moved = std::abs(dragStart_ - dragOriginStart_) > 1.0e-6 || std::abs(dragDuration_ - dragOriginDuration_) > 1.0e-6;
        if (moved) {
            const auto event = std::find_if(events.begin(), events.end(), [this](const Event& e) { return e.id == draggedEvent_; });
            if (event != events.end()
                && engine.updateAudioClipEvent(index, clip.id, draggedEvent_, dragStart_, dragOffset_, dragDuration_, dragSourceDuration_,
                                               std::min(event->fadeInSeconds, dragDuration_ * 0.5),
                                               std::min(event->fadeOutSeconds, dragDuration_ * 0.5)))
                studio_.markDirty();
        }
        eventDrag_ = EventDrag::none;
        draggedEvent_ = 0;
    }
    if (hovered && eventDrag_ == EventDrag::none)
        ImGui::SetMouseCursor(hoveredPart == EventDrag::move ? ImGuiMouseCursor_Hand : ImGuiMouseCursor_ResizeEW);
    else if (eventDrag_ == EventDrag::trimStart || eventDrag_ == EventDrag::trimEnd)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemHovered())
        ui::tooltip(L("Clic : le curseur, ou l'événement. Glisser : le déplacer, ses bords : le raccourcir (aimanté ; Maj : libre). "
                      "S : couper au curseur ; Suppr : retirer ; Ctrl+D : dupliquer.",
                      "Click: the cursor, or the event. Drag: move it, its edges: shorten it (magnetic; Shift: free). "
                      "S: split at the cursor; Delete: remove; Ctrl+D: duplicate."));

    // The keys, while the editor has the focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        const auto selected = std::find_if(events.begin(), events.end(), [this](const Event& e) { return e.id == selectedEvent_; });
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && selected != events.end()) {
            if (engine.removeAudioClipEvent(index, clip.id, selected->id)) {
                selectedEvent_ = 0;
                studio_.markDirty();
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::GetIO().KeyCtrl) {
            const auto target = selected != events.end() ? selected
                                                         : std::find_if(events.begin(), events.end(), [this](const Event& e) {
                                                               return eventCursor_ > e.startSeconds
                                                                      && eventCursor_ < e.startSeconds + e.durationSeconds;
                                                           });
            if (target != events.end())
                if (const auto made = engine.splitAudioClipEvent(index, clip.id, target->id, eventCursor_); made != 0) {
                    selectedEvent_ = made;
                    studio_.markDirty();
                }
        } else if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false) && selected != events.end()) {
            auto all = events;
            auto copy = *selected;
            copy.id = 0;
            copy.startSeconds = selected->startSeconds + selected->durationSeconds;
            for (auto& marker : copy.stretchMarkers)
                marker.id = 0;
            all.push_back(copy);
            if (engine.replaceAudioClipEvents(index, clip.id, std::move(all)).wasOk())
                studio_.markDirty();
        }
    }
}

} // namespace cydaw::v2
