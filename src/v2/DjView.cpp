#include "DjView.h"
#include "ModeProcessing.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Locations.h"
#include "Studio.h"
#include "Theme.h"
#include "Widgets.h"
#include "BottomPanelControls.h"

#include <cydaw/engine/DjMix.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace cydaw::v2 {

using app::DjEngine;
using ui::dp;
using ui::L;

namespace {

constexpr float targetLoudness = -12.0f;

ImU32 deckColour(const int deck)
{
    static constexpr ImU32 colours[4] { IM_COL32(74, 144, 226, 255), IM_COL32(230, 136, 52, 255), IM_COL32(86, 182, 118, 255),
                                        IM_COL32(168, 114, 214, 255) };
    return colours[std::clamp(deck, 0, 3)];
}

const char* deckLetter(const int deck)
{
    static constexpr const char* letters[4] { "A", "B", "C", "D" };
    return letters[std::clamp(deck, 0, 3)];
}

// The hot cues' colours, as on DJ gear.
ImU32 cueColour(const int index)
{
    static constexpr ImU32 colours[8] { IM_COL32(232, 70, 70, 255),  IM_COL32(240, 150, 40, 255), IM_COL32(236, 212, 60, 255),
                                        IM_COL32(90, 200, 90, 255),  IM_COL32(60, 200, 210, 255), IM_COL32(70, 120, 235, 255),
                                        IM_COL32(170, 90, 230, 255), IM_COL32(230, 90, 180, 255) };
    return colours[index & 7];
}

juce::String clock(double seconds, const bool tenths = true)
{
    const auto negative = seconds < 0.0;
    seconds = std::abs(seconds);
    const auto minutes = static_cast<int>(seconds / 60.0);
    const auto rest = seconds - minutes * 60.0;
    char text[32];
    if (tenths)
        std::snprintf(text, sizeof(text), "%s%d:%04.1f", negative ? "-" : "", minutes, rest);
    else
        std::snprintf(text, sizeof(text), "%s%d:%02d", negative ? "-" : "", minutes, static_cast<int>(rest));
    return text;
}

// A waveform column's colour: blue for the bass, amber for the mids, white
// for the highs, mixed by their share.
ImU32 waveColour(const float low, const float mid, const float high, const float brightness)
{
    const auto total = low + mid + high + 1.0e-6f;
    const auto wl = low / total;
    const auto wm = mid / total;
    const auto wh = high / total;
    const auto r = 50.0f * wl + 245.0f * wm + 235.0f * wh;
    const auto g = 105.0f * wl + 160.0f * wm + 240.0f * wh;
    const auto b = 235.0f * wl + 50.0f * wm + 245.0f * wh;
    const auto k = std::clamp(brightness, 0.25f, 1.0f);
    return IM_COL32(static_cast<int>(r * k), static_cast<int>(g * k), static_cast<int>(b * k), 255);
}

// A button that lights in a colour while on.
bool pad(const char* label, const ImVec2 size, const bool on, const ImU32 colour, const char* tip = nullptr)
{
    const auto& p = theme::palette();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto clicked = ImGui::InvisibleButton(label, size);
    const auto hovered = ImGui::IsItemHovered();
    auto* list = ImGui::GetWindowDrawList();
    const auto fill = on ? colour : (hovered ? p.bg4 : p.bg3);
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), fill, dp(3.0f));
    if (!on)
        list->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), theme::alpha(colour, 0.55f), dp(3.0f), dp(1.0f));
    const auto* end = ImGui::FindRenderedTextEnd(label);
    const auto textSize = ImGui::CalcTextSize(label, end);
    list->AddText(ImVec2(origin.x + (size.x - textSize.x) * 0.5f, origin.y + (size.y - textSize.y) * 0.5f),
                  on ? IM_COL32(20, 20, 20, 255) : (hovered ? p.text : p.textDim), label, end);
    if (tip != nullptr)
        ui::tooltip(tip);
    return clicked;
}

// The keys' names as DJs read them: the wheel's number, then the name.
juce::String keyText(const int key)
{
    if (key < 0)
        return "-";
    return juce::String(engine::dj::camelotName(key)) + " " + engine::dj::keyName(key);
}

double number(const juce::var& request, const char* key, const double fallback)
{
    const auto value = request.getProperty(key, juce::var());
    return value.isVoid() ? fallback : static_cast<double>(value);
}

bool flag(const juce::var& request, const char* key, const bool fallback)
{
    const auto value = request.getProperty(key, juce::var());
    return value.isVoid() ? fallback : static_cast<bool>(value);
}

} // namespace

DjView::DjView(Studio& studio, UiState& state, ImGuiHost& host)
    : studio_(studio), state_(state), host_(host), library_(std::make_unique<DjLibrary>())
{
    for (auto& deck : decks_)
        deck.cues.fill(-1.0);
}

DjView::~DjView()
{
    flush();
}

DjLibrary& DjView::library()
{
    ensureLoaded();
    return *library_;
}

void DjView::flush()
{
    saveProcessing(true);
    // removeAllJobs() discards queued jobs; wait for them to execute instead.
    for (int waits = 0; waits < 10 && processingWriter_.getNumJobs() > 0; ++waits)
        if (auto* job = processingWriter_.getJob(0))
            processingWriter_.waitForJobToFinish(job, 1000);
    if (library_ != nullptr)
        library_->flush();
}

void DjView::tickPersistence() { saveProcessing(); }

bool DjView::undo(const bool redo) { return processing_ && processing_->undo(redo); }

void DjView::saveProcessing(const bool force)
{
    if (!processing_)
        return;
    const auto revision = processing_->revision();
    if (revision == processingRevision_ && !processingWriteFailed_.load())
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (observedProcessingRevision_ != revision) {
        observedProcessingRevision_ = revision;
        processingChangedAt_ = now;
    }
    // The DSP follows immediately; persistence waits until a gesture rests.
    if (!force && ((ImGui::GetCurrentContext() != nullptr && ImGui::IsAnyMouseDown())
                   || now - processingChangedAt_ < 0.4)) {
        host_.wake(3);
        return;
    }
    const auto override = juce::SystemStats::getEnvironmentVariable("CYDAW_DJ_PROCESSING_FILE", {});
    const auto file = override.isNotEmpty() ? juce::File(override)
        : locations::folder(Location::mixes).getChildFile("DJ processing.cydjmix");
    const auto snapshot = processing_->serialise();
    processingRevision_ = revision;
    processingWriteFailed_.store(false);
    // JSON encoding and disk I/O never run in the knob's draw callback.
    processingWriter_.addJob([this, file, snapshot] {
        if (!file.getParentDirectory().createDirectory() || !file.replaceWithText(juce::JSON::toString(snapshot)))
            processingWriteFailed_.store(true);
    });
}

void DjView::ensureLoaded()
{
    // The collection is read the first time the mode shows (or a command
    // asks), not at the start.
    if (loaded_)
        return;
    loaded_ = true;
    library_->load();
    processing_=std::make_shared<ModeProcessing>(&host_);
    const auto override=juce::SystemStats::getEnvironmentVariable("CYDAW_DJ_PROCESSING_FILE",{});
    const auto file=override.isNotEmpty()?juce::File(override):locations::folder(Location::mixes).getChildFile("DJ processing.cydjmix");
    processing_->restore(juce::JSON::parse(file));
    for(int i=0;i<4;++i)processing_->ensureInput(static_cast<std::uint64_t>(i+1),juce::String("Deck ")+deckLetter(i));
    auto* device=studio_.engine().deviceManager().getCurrentAudioDevice();
    processing_->prepare(device?device->getCurrentSampleRate():48000,device?device->getCurrentBufferSizeSamples():512);
    processingRevision_=processing_->revision();studio_.engine().dj().setProcessor(processing_);

}

// ---- Decks and the collection ----------------------------------------------------------------

int DjView::freeDeck() const
{
    auto& dj = studio_.engine().dj();
    const auto count = dj.shownDeckCount();
    for (int deck = 0; deck < count; ++deck)
        if (dj.deck(deck).state == DjEngine::LoadState::empty)
            return deck;
    for (int deck = 0; deck < count; ++deck)
        if (!dj.deck(deck).playing)
            return deck;
    return -1;
}

void DjView::loadFile(const int deck, const juce::File& file)
{
    auto& dj = studio_.engine().dj();
    if (deck < 0 || deck >= DjEngine::deckCount || !file.existsAsFile())
        return;
    ensureLoaded();
    DjEngine::Known known;
    if (const auto track = library_->find(file)) {
        known.hasGrid = (track->analysed || track->gridSet) && track->bpm > 0.0;
        known.bpm = track->bpm;
        known.firstBeat = track->firstBeat;
        known.cuePoint = track->cuePoint;
        known.hotCues = track->hotCues;
    }
    library_->remember(file);
    dj.load(deck, file, known);
    auto& ui = decks_[static_cast<std::size_t>(deck)];
    ui.learned = {};
    ui.cues = known.hotCues;
    ui.cuePoint = known.cuePoint;
    ui.overview.clear();
    ui.overviewOf = nullptr;
    ui.wasPlaying = false;
}

bool DjView::dropFiles(const juce::StringArray& files)
{
    auto deck = freeDeck();
    auto any = false;
    for (const auto& path : files) {
        const juce::File file(path);
        if (file.isDirectory()) {
            library().addFolder(file);
            any = true;
            continue;
        }
        if (deck < 0)
            break;
        loadFile(deck, file);
        any = true;
        deck = freeDeck();
    }
    return any;
}

void DjView::learnFromDecks()
{
    // What the decks found or were given goes into the collection: the
    // analysis once, the cues and the grid as they change, a play.
    auto& dj = studio_.engine().dj();
    for (int index = 0; index < DjEngine::deckCount; ++index) {
        auto& ui = decks_[static_cast<std::size_t>(index)];
        const auto deck = dj.deck(index);
        if (deck.state != DjEngine::LoadState::ready || deck.file == juce::File())
            continue;
        auto track = library_->find(deck.file).value_or(DjTrack {});
        track.file = deck.file;
        auto changed = false;
        if (deck.analysis != nullptr && ui.learned != deck.file) {
            ui.learned = deck.file;
            track.title = deck.title;
            track.artist = deck.artist;
            track.seconds = deck.seconds;
            track.key = deck.analysis->key;
            track.loudness = deck.analysis->loudness;
            if (!track.gridSet) {
                track.bpm = deck.bpm;
                track.firstBeat = deck.firstBeat;
            }
            track.analysed = true;
            changed = true;
            // Every track about as loud as the others, its peaks kept a
            // decibel under full scale.
            if (autoGain_ && deck.analysis->loudness > -60.0) {
                auto channel = dj.channel(index);
                const auto headroom = deck.analysis->peak > 1.0e-6f ? -1.0f - 20.0f * std::log10(deck.analysis->peak) : 12.0f;
                channel.trimDb = std::clamp(std::min(targetLoudness - static_cast<float>(deck.analysis->loudness), headroom), -12.0f, 12.0f);
                dj.setChannel(index, channel);
            }
        }
        if (deck.hotCues != ui.cues || std::abs(deck.cuePoint - ui.cuePoint) > 1.0e-6) {
            ui.cues = deck.hotCues;
            ui.cuePoint = deck.cuePoint;
            track.hotCues = deck.hotCues;
            track.cuePoint = deck.cuePoint;
            changed = true;
        }
        if (deck.playing && !ui.wasPlaying) {
            ++track.plays;
            track.lastPlayed = juce::Time::currentTimeMillis();
            if (history_.empty() || history_.back() != deck.file)
                history_.push_back(deck.file);
            changed = true;
        }
        ui.wasPlaying = deck.playing;
        if (changed)
            library_->update(track);
    }
}

// ---- The window --------------------------------------------------------------------------------

void DjView::draw()
{
    ensureLoaded();
    learnFromDecks();
    library_->tick();
    auto& dj = studio_.engine().dj();
    // A file asked for from elsewhere (a menu of the studio).
    if (state_.djLoad) {
        const auto [deck, file] = *state_.djLoad;
        state_.djLoad.reset();
        loadFile(deck < 0 ? std::max(0, freeDeck()) : deck, file);
    }
    const auto bottomVisible = state_.bottomPanelVisible();
    const auto entire=ImGui::GetContentRegionAvail();
    const auto tools=std::clamp(dp(processingHeight_),dp(180),std::max(dp(180),entire.y*.5f));
    ImGui::BeginChild("##djDecksAndCollection",ImVec2(0,std::max(dp(300),entire.y-(bottomVisible ? tools+dp(7) : ui::bottomPanelRestoreHeight()+ImGui::GetStyle().ItemSpacing.y))),0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    const auto& p = theme::palette();
    const auto count = dj.shownDeckCount();
    const auto avail = ImGui::GetContentRegionAvail();
    const auto spacing = dp(6.0f);
    const auto browserHeight = std::max(dp(150.0f), avail.y * browserShare_);
    const auto topHeight = std::max(dp(240.0f), avail.y - browserHeight - spacing);
    const auto mixerWidth = count == 2 ? dp(260.0f) : dp(440.0f);
    const auto deckWidth = std::max(dp(320.0f), (avail.x - mixerWidth - spacing * 2.0f) * 0.5f);
    const auto deckHeight = count == 2 ? topHeight : (topHeight - spacing) * 0.5f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(spacing, spacing));
    ImGui::BeginGroup();
    drawDeck(0, ImVec2(deckWidth, deckHeight));
    if (count > 2)
        drawDeck(2, ImVec2(deckWidth, deckHeight));
    ImGui::EndGroup();
    ImGui::SameLine();
    drawMixer(ImVec2(mixerWidth, topHeight));
    ImGui::SameLine();
    ImGui::BeginGroup();
    drawDeck(1, ImVec2(deckWidth, deckHeight));
    if (count > 2)
        drawDeck(3, ImVec2(deckWidth, deckHeight));
    ImGui::EndGroup();
    ImGui::PopStyleVar();

    // The line between the decks and the collection: dragged, it moves.
    {
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##djSplit", ImVec2(avail.x, dp(5.0f)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            browserShare_ = std::clamp(browserShare_ - ImGui::GetIO().MouseDelta.y / std::max(1.0f, avail.y), 0.12f, 0.7f);
        ImGui::GetWindowDrawList()->AddLine(ImVec2(at.x, at.y + dp(2.5f)), ImVec2(at.x + avail.x, at.y + dp(2.5f)),
                                            ImGui::IsItemHovered() ? p.accent : p.line, dp(1.0f));
    }
    drawBrowser(ImVec2(avail.x, ImGui::GetContentRegionAvail().y));
    ImGui::EndChild();
    if (bottomVisible) {
        ImGui::InvisibleButton("##djToolsSplit",ImVec2(-1,dp(6)));
        if(ImGui::IsItemHovered()||ImGui::IsItemActive())ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if(ImGui::IsItemActive())processingHeight_=std::clamp(processingHeight_-ImGui::GetIO().MouseDelta.y,180.0f,650.0f);
        ImGui::BeginChild("##djProcessing",ImVec2(0,0),ImGuiChildFlags_Borders);
        processing_->draw([&]{
            ImGui::TextDisabled("%s",L("Ouvrez le son d'une platine dans la Fonderie pour le découper ou le traiter.","Open a deck's sound in the Foundry to edit or process it."));
            for(int i=0;i<dj.shownDeckCount();++i){ImGui::PushID(i);const auto deck=dj.deck(i);ImGui::BeginDisabled(deck.file==juce::File());
                if(ImGui::Button((juce::String("Deck ")+deckLetter(i)+ui::Ls(" → Fonderie"," → Foundry")).toRawUTF8())){state_.foundryOpen=deck.file;state_.mode=AppMode::foundry;}
                ImGui::EndDisabled();ImGui::PopID();ImGui::SameLine();}
        }, true, [&] {
            state_.setBottomPanelVisible(false);
            host_.wake(3);
        });
        ImGui::EndChild();
    } else {
        if (ui::bottomPanelRestoreBar()) {
            state_.setBottomPanelVisible(true);
            host_.wake(3);
        }
        processing_->draw({}, false);
    }
    saveProcessing();
    if (dj.anyPlaying() || library_->progress().waiting > 0 || library_->progress().scanning)
        host_.wake(2);
    for (int deck = 0; deck < count; ++deck)
        if (dj.deck(deck).state == DjEngine::LoadState::loading || (dj.deck(deck).state == DjEngine::LoadState::ready && dj.deck(deck).analysis == nullptr))
            host_.wake(2);
}

void DjView::drawTransport()
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    const auto bpm = dj.masterBpm();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textFaint), "MASTER");
    ImGui::SameLine(0.0f, dp(6.0f));
    ImGui::PushFont(theme::boldFont(), 0.0f);
    if (bpm > 0.0)
        ImGui::TextColored(theme::vec(p.text), "%.2f BPM", bpm);
    else
        ImGui::TextColored(theme::vec(p.textFaint), "--- BPM");
    ImGui::PopFont();
    ui::tooltip(L("Le tempo de la platine maître : les platines en SYNC le suivent",
                  "The master deck's tempo: the decks on SYNC follow it"));
    ui::toolbarSeparator();
    const auto recording = dj.recording();
    if (ui::iconButton(icon::record,
                       recording ? L("Arrêter l'enregistrement du mix", "Stop recording the mix")
                                 : L("Enregistrer le mix dans un fichier (Mixes DJ)", "Record the mix to a file (DJ mixes)"),
                       recording, p.record)) {
        if (recording) {
            const auto file = dj.recordingFile();
            dj.stopRecording();
            state_.status = ui::Ls("Mix enregistré : ", "Mix recorded: ") + file.getFileName();
            state_.statusIsError = false;
            state_.statusTime = ImGui::GetTime();
        } else {
            startRecording();
        }
    }
    if (recording) {
        ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.record), "REC %s", clock(dj.recordedSeconds(), false).toRawUTF8());
        host_.wake(2);
    }
    ui::toolbarSeparator();
    auto quantize = dj.quantize();
    if (ui::toggle("QUANT", &quantize, p.accent, ImVec2(0.0f, 0.0f),
                   L("Quantification : les repères, boucles et sauts tombent sur les temps de la grille",
                     "Quantize: cues, loops and jumps land on the grid's beats")))
        dj.setQuantize(quantize);
    ImGui::SameLine(0.0f, dp(4.0f));
    auto four = dj.shownDeckCount() == 4;
    if (ui::toggle(four ? "4 DECKS" : "2 DECKS", &four, p.accent, ImVec2(0.0f, 0.0f),
                   L("Deux ou quatre platines", "Two or four decks")))
        dj.setDeckCount(four ? 4 : 2);
}

void DjView::startRecording()
{
    auto& dj = studio_.engine().dj();
    const auto folder = locations::folder(Location::mixes);
    const auto name = "Mix " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + ".wav";
    const auto result = dj.startRecording(folder.getChildFile(name));
    state_.status = result.wasOk() ? ui::Ls("Enregistrement du mix : ", "Recording the mix: ") + name : result.getErrorMessage();
    state_.statusIsError = result.failed();
    state_.statusTime = ImGui::GetTime();
}

// ---- A deck ------------------------------------------------------------------------------------

void DjView::drawDeck(const int index, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    ImGui::PushID(index);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(dp(4.0f), dp(4.0f)));
    if (ImGui::BeginChild("##deck", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const auto inner = ImGui::GetContentRegionAvail();
        const auto tempoWidth = dp(46.0f);
        const auto mainWidth = inner.x - tempoWidth - dp(4.0f);
        ImGui::BeginGroup();
        drawDeckHeader(index, mainWidth);
        drawOverview(index, ImVec2(mainWidth, dp(30.0f)));
        // The buttons take what they need; the waveform the rest.
        const auto buttonsHeight = (ImGui::GetFrameHeight() + dp(4.0f)) * 4.0f + dp(30.0f);
        const auto waveHeight = std::max(dp(44.0f), ImGui::GetContentRegionAvail().y - buttonsHeight);
        drawWaveform(index, ImVec2(mainWidth, waveHeight));
        drawDeckButtons(index, mainWidth);
        ImGui::EndGroup();
        ImGui::SameLine();
        drawTempo(index, ImVec2(tempoWidth, ImGui::GetContentRegionAvail().y));
    }
    ImGui::EndChild();
    // A track dropped on it (the collection's, the studio's browser).
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1)
            loadFile(index, juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data))));
        ImGui::EndDragDropTarget();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopID();
    (void) dj;
}

void DjView::drawDeckHeader(const int index, const float width)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    const auto deck = dj.deck(index);
    auto& ui = decks_[static_cast<std::size_t>(index)];
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto height = dp(38.0f);
    ImGui::Dummy(ImVec2(width, height));
    // The letter.
    const auto badge = dp(24.0f);
    list->AddRectFilled(origin, ImVec2(origin.x + badge, origin.y + height), deckColour(index), dp(3.0f));
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.25f);
    ui::centredText(list, origin, ImVec2(origin.x + badge, origin.y + height), IM_COL32(20, 20, 20, 255), deckLetter(index));
    ImGui::PopFont();
    // Its tempo, key and time on the right.
    const auto rightWidth = dp(150.0f);
    const auto right = origin.x + width - rightWidth;
    char bpmText[32];
    if (deck.bpm > 0.0)
        std::snprintf(bpmText, sizeof(bpmText), "%.2f", deck.playedBpm > 0.0 ? deck.playedBpm : deck.bpm * deck.tempo);
    else
        std::snprintf(bpmText, sizeof(bpmText), "---.--");
    ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.35f);
    const auto bpmSize = ImGui::CalcTextSize(bpmText);
    list->AddText(ImVec2(origin.x + width - bpmSize.x, origin.y - dp(1.0f)), deck.master ? deckColour(index) : p.text, bpmText);
    ImGui::PopFont();
    const auto small = ImGui::GetStyle().FontSizeBase * 0.86f;
    const auto timeText = deck.state == DjEngine::LoadState::ready
                              ? (ui.remaining ? clock(deck.position - deck.seconds) : clock(deck.position))
                              : juce::String("-:--.-");
    ImGui::PushFont(theme::monoFont(), small);
    const auto timeSize = ImGui::CalcTextSize(timeText.toRawUTF8());
    list->AddText(ImVec2(origin.x + width - timeSize.x, origin.y + height - timeSize.y), p.textDim, timeText.toRawUTF8());
    const auto key = deck.analysis != nullptr ? keyText(deck.analysis->key) : juce::String("-");
    list->AddText(ImVec2(right, origin.y + height - timeSize.y), p.textDim, key.toRawUTF8());
    ImGui::PopFont();
    // A click on the time: elapsed or remaining.
    ImGui::SetCursorScreenPos(ImVec2(origin.x + width - timeSize.x, origin.y + height - timeSize.y));
    if (ImGui::InvisibleButton("##time", timeSize))
        ui.remaining = !ui.remaining;
    ui::tooltip(L("Temps restant ou écoulé (clic)", "Time left or elapsed (click)"));
    // The title and the artist.
    const auto textX = origin.x + badge + dp(8.0f);
    const auto textWidth = right - textX - dp(8.0f);
    juce::String title;
    juce::String artist;
    switch (deck.state) {
    case DjEngine::LoadState::empty:
        title = ui::Ls("Glissez un morceau ici", "Drop a track here");
        break;
    case DjEngine::LoadState::loading:
        title = ui::Ls("Chargement… ", "Loading… ") + deck.title;
        break;
    case DjEngine::LoadState::failed:
        title = deck.file.getFileName();
        artist = deck.error;
        break;
    case DjEngine::LoadState::ready:
        title = deck.title;
        artist = deck.analysis == nullptr ? ui::Ls("Analyse…", "Analysing…") : deck.artist;
        break;
    }
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ui::clippedText(list, ImVec2(textX, origin.y + dp(2.0f)), textWidth,
                    deck.state == DjEngine::LoadState::empty ? p.textFaint : p.text, title.toRawUTF8());
    ImGui::PopFont();
    ImGui::PushFont(nullptr, small);
    ui::clippedText(list, ImVec2(textX, origin.y + height - ImGui::GetFontSize() - dp(1.0f)), textWidth,
                    deck.state == DjEngine::LoadState::failed ? p.record : p.textDim, artist.toRawUTF8());
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + dp(4.0f)));
}

void DjView::drawOverview(const int index, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    const auto deck = dj.deck(index);
    auto& ui = decks_[static_cast<std::size_t>(index)];
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto end = ImVec2(origin.x + size.x, origin.y + size.y);
    ImGui::InvisibleButton("##overview", size);
    list->AddRectFilled(origin, end, p.bg0, dp(2.0f));
    const auto width = static_cast<int>(size.x);
    if (deck.analysis != nullptr && !deck.analysis->wave.empty() && width > 0) {
        const auto& wave = deck.analysis->wave;
        if (ui.overviewOf != deck.analysis.get() || ui.overviewWidth != width) {
            ui.overviewOf = deck.analysis.get();
            ui.overviewWidth = width;
            ui.overview.assign(static_cast<std::size_t>(width), {});
            for (int x = 0; x < width; ++x) {
                const auto a = wave.size() * static_cast<std::size_t>(x) / static_cast<std::size_t>(width);
                const auto b = std::max(a + 1, wave.size() * static_cast<std::size_t>(x + 1) / static_cast<std::size_t>(width));
                std::array<float, 4> column {};
                for (auto c = a; c < std::min(b, wave.size()); ++c) {
                    column[0] = std::max(column[0], wave[c].peak);
                    column[1] = std::max(column[1], wave[c].low);
                    column[2] = std::max(column[2], wave[c].mid);
                    column[3] = std::max(column[3], wave[c].high);
                }
                ui.overview[static_cast<std::size_t>(x)] = column;
            }
        }
        const auto centre = origin.y + size.y * 0.5f;
        const auto played = deck.seconds > 0.0 ? static_cast<float>(deck.position / deck.seconds) * size.x : 0.0f;
        for (int x = 0; x < width; ++x) {
            const auto& column = ui.overview[static_cast<std::size_t>(x)];
            const auto half = column[0] * size.y * 0.48f;
            const auto colour = waveColour(column[1], column[2], column[3], static_cast<float>(x) < played ? 0.45f : 0.95f);
            list->AddRectFilled(ImVec2(origin.x + x, centre - half), ImVec2(origin.x + x + 1.0f, centre + half + 1.0f), colour);
        }
    }
    if (deck.seconds > 0.0) {
        const auto xOf = [&](const double seconds) { return origin.x + static_cast<float>(seconds / deck.seconds) * size.x; };
        if (deck.looping)
            list->AddRectFilled(ImVec2(xOf(deck.loopStart), origin.y), ImVec2(std::max(xOf(deck.loopStart) + 2.0f, xOf(deck.loopEnd)), end.y),
                                IM_COL32(80, 200, 110, 70));
        for (int c = 0; c < 8; ++c)
            if (const auto at = deck.hotCues[static_cast<std::size_t>(c)]; at >= 0.0)
                list->AddLine(ImVec2(xOf(at), origin.y), ImVec2(xOf(at), end.y), cueColour(c), dp(1.5f));
        const auto cueX = xOf(deck.cuePoint);
        list->AddTriangleFilled(ImVec2(cueX - dp(4.0f), origin.y), ImVec2(cueX + dp(4.0f), origin.y), ImVec2(cueX, origin.y + dp(5.0f)),
                                IM_COL32(240, 150, 40, 255));
        const auto x = xOf(deck.position);
        list->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), p.text, dp(1.5f));
        // A click or a drag: there.
        if (ImGui::IsItemActive()) {
            const auto fraction = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
            dj.seek(index, fraction * deck.seconds);
        }
    }
    list->AddRect(origin, end, p.line, dp(2.0f));
}

void DjView::drawWaveform(const int index, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    const auto deck = dj.deck(index);
    auto& ui = decks_[static_cast<std::size_t>(index)];
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto end = ImVec2(origin.x + size.x, origin.y + size.y);
    ImGui::InvisibleButton("##wave", size);
    const auto hovered = ImGui::IsItemHovered();
    list->AddRectFilled(origin, end, IM_COL32(16, 17, 20, 255), dp(2.0f));
    const auto beat = deck.bpm > 0.0 ? 60.0 / deck.bpm : 0.5;
    const auto span = std::max(0.5, static_cast<double>(ui.zoomBeats) * beat);
    const auto secondsPerPixel = span / std::max(1.0f, size.x);
    const auto left = deck.position - span * 0.5;
    const auto xOf = [&](const double seconds) { return origin.x + static_cast<float>((seconds - left) / secondsPerPixel); };
    list->PushClipRect(origin, end, true);
    // The beats, the bars brighter.
    if (deck.state == DjEngine::LoadState::ready && deck.bpm > 0.0) {
        const auto firstShown = static_cast<long long>(std::floor((left - deck.firstBeat) / beat));
        for (auto k = firstShown; k <= firstShown + static_cast<long long>(ui.zoomBeats) + 2; ++k) {
            const auto x = xOf(deck.firstBeat + static_cast<double>(k) * beat);
            const auto bar = ((k % 4) + 4) % 4 == 0;
            list->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), bar ? IM_COL32(255, 255, 255, 70) : IM_COL32(255, 255, 255, 28),
                          bar ? dp(1.4f) : dp(1.0f));
        }
    }
    // The loop.
    if (deck.looping)
        list->AddRectFilled(ImVec2(xOf(deck.loopStart), origin.y), ImVec2(xOf(deck.loopEnd), end.y), IM_COL32(80, 200, 110, 45));
    // The sound.
    if (deck.analysis != nullptr && !deck.analysis->wave.empty()) {
        const auto& wave = deck.analysis->wave;
        const auto columnSeconds = std::max(1.0e-6, deck.analysis->columnSeconds);
        const auto centre = origin.y + size.y * 0.5f;
        const auto count = static_cast<long long>(wave.size());
        for (int x = 0; x < static_cast<int>(size.x); ++x) {
            const auto t0 = left + x * secondsPerPixel;
            const auto t1 = t0 + secondsPerPixel;
            auto c0 = static_cast<long long>(std::floor(t0 / columnSeconds));
            auto c1 = std::max(c0 + 1, static_cast<long long>(std::floor(t1 / columnSeconds)));
            if (c1 <= 0 || c0 >= count)
                continue;
            c0 = std::max(0LL, c0);
            c1 = std::min(count, c1);
            engine::dj::WaveColumn column;
            for (auto c = c0; c < c1; ++c) {
                const auto& w = wave[static_cast<std::size_t>(c)];
                column.peak = std::max(column.peak, w.peak);
                column.low = std::max(column.low, w.low);
                column.mid = std::max(column.mid, w.mid);
                column.high = std::max(column.high, w.high);
            }
            const auto half = column.peak * size.y * 0.47f;
            const auto past = t0 < deck.position;
            list->AddRectFilled(ImVec2(origin.x + x, centre - half), ImVec2(origin.x + x + 1.0f, centre + half + 1.0f),
                                waveColour(column.low, column.mid, column.high, past ? 0.6f : 1.0f));
        }
    } else if (deck.state == DjEngine::LoadState::ready || deck.state == DjEngine::LoadState::loading) {
        ui::centredText(list, origin, end, p.textFaint, deck.state == DjEngine::LoadState::loading ? L("Chargement…", "Loading…")
                                                                                                     : L("Analyse…", "Analysing…"));
    }
    // The cues.
    for (int c = 0; c < 8; ++c)
        if (const auto at = deck.hotCues[static_cast<std::size_t>(c)]; at >= 0.0) {
            const auto x = xOf(at);
            list->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), theme::alpha(cueColour(c), 0.8f), dp(1.5f));
            list->AddRectFilled(ImVec2(x, origin.y), ImVec2(x + dp(12.0f), origin.y + dp(12.0f)), cueColour(c));
            char number[4];
            std::snprintf(number, sizeof(number), "%d", c + 1);
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.72f);
            list->AddText(ImVec2(x + dp(3.0f), origin.y), IM_COL32(20, 20, 20, 255), number);
            ImGui::PopFont();
        }
    if (deck.state == DjEngine::LoadState::ready) {
        const auto cueX = xOf(deck.cuePoint);
        list->AddTriangleFilled(ImVec2(cueX - dp(6.0f), end.y), ImVec2(cueX + dp(6.0f), end.y), ImVec2(cueX, end.y - dp(8.0f)),
                                IM_COL32(240, 150, 40, 255));
    }
    // Where it plays.
    const auto middle = origin.x + size.x * 0.5f;
    list->AddLine(ImVec2(middle, origin.y), ImVec2(middle, end.y), IM_COL32(255, 60, 60, 230), dp(1.6f));
    // Its beat against the master's: a bar from the middle.
    if (!deck.master && deck.bpm > 0.0 && dj.masterBpm() > 0.0) {
        const auto width = static_cast<float>(deck.phase) * dp(80.0f);
        const auto y = end.y - dp(4.0f);
        list->AddRectFilled(ImVec2(middle + std::min(0.0f, width), y), ImVec2(middle + std::max(0.0f, width), end.y),
                            std::abs(deck.phase) < 0.02 ? IM_COL32(90, 210, 110, 255) : IM_COL32(240, 160, 60, 255));
    }
    list->PopClipRect();
    list->AddRect(origin, end, p.line, dp(2.0f));
    // Held and moved, the track goes with the hand (scratching); the wheel
    // zooms.
    if (deck.state == DjEngine::LoadState::ready) {
        if (ImGui::IsItemActivated()) {
            ui.scratchFrom = deck.position;
            ui.scratchX = ImGui::GetIO().MousePos.x;
            dj.scratch(index, true, deck.position);
        }
        if (ImGui::IsItemActive()) {
            const auto moved = ImGui::GetIO().MousePos.x - ui.scratchX;
            dj.scratch(index, true, std::max(0.0, ui.scratchFrom - static_cast<double>(moved) * secondsPerPixel));
        }
        if (ImGui::IsItemDeactivated())
            dj.scratch(index, false, deck.position);
    }
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        ui.zoomBeats = std::clamp(ui.zoomBeats * (ImGui::GetIO().MouseWheel > 0.0f ? 0.5f : 2.0f), 2.0f, 128.0f);
        ui::takeWheel();
    }
    if (hovered)
        ui::tooltip(L("Tenir et glisser : la piste suit la main (scratch). Molette : zoom.",
                      "Hold and drag: the track follows the hand (scratch). Wheel: zoom."));
}

void DjView::drawDeckButtons(const int index, const float width)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    const auto deck = dj.deck(index);
    auto& ui = decks_[static_cast<std::size_t>(index)];
    const auto colour = deckColour(index);
    const auto height = ImGui::GetFrameHeight();
    const auto unit = std::max(dp(26.0f), (width - dp(4.0f) * 7.0f) / 8.0f);

    // CUE, PLAY, SYNC, MASTER, KEY, beat jumps.
    {
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##cue", ImVec2(unit * 1.2f, height));
        const auto active = ImGui::IsItemActive();
        if (ImGui::IsItemActivated())
            dj.cue(index, true);
        if (ImGui::IsItemDeactivated())
            dj.cue(index, false);
        auto* list = ImGui::GetWindowDrawList();
        const auto end = ImVec2(origin.x + unit * 1.2f, origin.y + height);
        list->AddRectFilled(origin, end, active || deck.previewing ? IM_COL32(240, 150, 40, 255) : p.bg3, dp(3.0f));
        list->AddRect(origin, end, IM_COL32(240, 150, 40, 160), dp(3.0f), dp(1.0f));
        ui::centredText(list, origin, end, active || deck.previewing ? IM_COL32(20, 20, 20, 255) : p.text, "CUE");
        ui::tooltip(L("CUE : en lecture, revient au repère et s'arrête ; à l'arrêt, pose le repère ici et joue tant qu'il est tenu",
                      "CUE: playing, back to the cue point and stopped; stopped, sets the cue point here and plays while held"));
    }
    ImGui::SameLine();
    if (pad((juce::String::fromUTF8(deck.playing ? icon::pause : icon::play) + "##play").toRawUTF8(), ImVec2(unit * 1.2f, height),
            deck.playing, IM_COL32(90, 200, 100, 255), L("Lecture / pause", "Play / pause")))
        dj.setPlaying(index, !deck.playing);
    ImGui::SameLine();
    if (pad("SYNC", ImVec2(unit, height), deck.sync, colour,
            L("SYNC : suit le tempo de la platine maître, temps calés", "SYNC: follows the master deck's tempo, beats aligned")))
        dj.setSync(index, !deck.sync);
    ImGui::SameLine();
    if (pad("MASTER", ImVec2(unit * 1.15f, height), deck.master, colour,
            L("MASTER : le tempo que les autres suivent (automatique sinon)", "MASTER: the tempo the others follow (automatic otherwise)")))
        dj.setMaster(dj.masterDeck() == index ? -1 : index);
    ImGui::SameLine();
    if (pad("KEY", ImVec2(unit * 0.8f, height), deck.keylock, colour,
            L("KEY activé : hauteur conservée. Désactivez KEY pour que le tempo change aussi la hauteur.", "KEY on: pitch is preserved. Turn KEY off to change pitch with tempo.")))
        dj.setKeylock(index, !deck.keylock);
    ImGui::SameLine();
    if (pad((juce::String::fromUTF8(icon::previous) + "##jumpBack").toRawUTF8(), ImVec2(unit * 0.7f, height), false, colour,
            L("Saut en arrière", "Jump back")))
        dj.beatJump(index, -ui.jumpBeats);
    ImGui::SameLine();
    {
        char text[16];
        std::snprintf(text, sizeof(text), ui.jumpBeats < 1.0f ? "1/%.0f" : "%.0f", ui.jumpBeats < 1.0f ? 1.0f / ui.jumpBeats : ui.jumpBeats);
        ImGui::SetNextItemWidth(unit * 0.7f);
        if (ImGui::BeginCombo("##jumpSize", text, ImGuiComboFlags_NoArrowButton)) {
            for (const auto beats : { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f }) {
                char label[16];
                std::snprintf(label, sizeof(label), beats < 1.0f ? "1/%.0f" : "%.0f", beats < 1.0f ? 1.0f / beats : beats);
                if (ImGui::Selectable(label, beats == ui.jumpBeats))
                    ui.jumpBeats = beats;
            }
            ImGui::EndCombo();
        }
        ui::tooltip(L("Temps sautés", "Beats jumped"));
    }
    ImGui::SameLine();
    if (pad((juce::String::fromUTF8(icon::next) + "##jumpOn").toRawUTF8(), ImVec2(unit * 0.7f, height), false, colour,
            L("Saut en avant", "Jump forward")))
        dj.beatJump(index, ui.jumpBeats);

    // The hot cues: a click jumps (or sets an empty one); Shift or a right
    // click removes.
    for (int c = 0; c < 8; ++c) {
        if (c > 0)
            ImGui::SameLine();
        const auto set = deck.hotCues[static_cast<std::size_t>(c)] >= 0.0;
        char label[8];
        std::snprintf(label, sizeof(label), "%d", c + 1);
        ImGui::PushID(c);
        if (pad(label, ImVec2(unit, height), set, cueColour(c),
                L("Hot cue : saute ici, ou le pose s'il est vide. Maj+clic ou clic droit : l'efface.",
                  "Hot cue: jumps there, or sets it when empty. Shift+click or right click: removes it."))) {
            dj.hotCue(index, c, ImGui::GetIO().KeyShift ? DjEngine::HotCueAction::remove : DjEngine::HotCueAction::jumpOrSet);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            dj.hotCue(index, c, DjEngine::HotCueAction::remove);
        ImGui::PopID();
    }

    // Loops.
    {
        const auto length = deck.looping && deck.bpm > 0.0 ? (deck.loopEnd - deck.loopStart) * deck.bpm / 60.0 : 0.0;
        const auto loopUnit = (width - dp(4.0f) * 11.0f) / 12.0f;
        for (const auto beats : { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 }) {
            char label[16];
            std::snprintf(label, sizeof(label), beats < 1.0 ? "1/%.0f" : "%.0f", beats < 1.0 ? 1.0 / beats : beats);
            const auto on = deck.looping && std::abs(length - beats) < 0.01;
            if (pad(label, ImVec2(loopUnit, height), on, IM_COL32(80, 200, 110, 255),
                    L("Boucle d'autant de temps (encore : l'arrête)", "A loop this many beats long (again: off)")))
                dj.loopBeats(index, on ? 0.0 : beats);
            ImGui::SameLine();
        }
        if (pad("/2", ImVec2(loopUnit, height), false, IM_COL32(80, 200, 110, 255), L("Boucle deux fois plus courte", "Loop half as long")))
            dj.loopResize(index, 0.5);
        ImGui::SameLine();
        if (pad("x2", ImVec2(loopUnit, height), false, IM_COL32(80, 200, 110, 255), L("Boucle deux fois plus longue", "Loop twice as long")))
            dj.loopResize(index, 2.0);
        ImGui::SameLine();
        if (pad((juce::String::fromUTF8(icon::chevronLeft) + "##loopBack").toRawUTF8(), ImVec2(loopUnit, height), false,
                IM_COL32(80, 200, 110, 255),
                L("La boucle d'un temps en arrière", "The loop a beat back")))
            dj.loopMove(index, -1.0);
        ImGui::SameLine();
        if (pad((juce::String::fromUTF8(icon::chevronRight) + "##loopOn").toRawUTF8(), ImVec2(loopUnit, height), false,
                IM_COL32(80, 200, 110, 255),
                L("La boucle d'un temps en avant", "The loop a beat on")))
            dj.loopMove(index, 1.0);
    }

    // The deck's effect.
    {
        auto channel = dj.channel(index);
        auto changed = false;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textFaint), "FX");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(dp(104.0f));
        if (ImGui::BeginCombo("##effect", engine::dj::effectInfo(channel.effect.kind).name)) {
            for (int kind = 0; kind < engine::dj::effectKindCount; ++kind) {
                const auto effect = static_cast<engine::dj::EffectKind>(kind);
                if (ImGui::Selectable(engine::dj::effectInfo(effect).name, effect == channel.effect.kind)) {
                    channel.effect.kind = effect;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (pad("ON##fx", ImVec2(dp(34.0f), height), channel.effect.on, colour,
                L("L'effet de la platine, après son fader (un écho continue une fois fermé)",
                  "The deck's effect, after its fader (an echo rings on once it is closed)"))) {
            channel.effect.on = !channel.effect.on;
            changed = true;
        }
        const auto& info = engine::dj::effectInfo(channel.effect.kind);
        const auto slider = [&](const char* id, const char* name, float& value) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(dp(56.0f), (width - dp(170.0f)) / 3.0f - dp(4.0f)));
            char format[48];
            std::snprintf(format, sizeof(format), "%s %%.2f", name);
            if (ImGui::SliderFloat(id, &value, 0.0f, 1.0f, format))
                changed = true;
        };
        slider("##mix", "D/W", channel.effect.mix);
        slider("##first", info.first, channel.effect.first);
        slider("##second", info.second, channel.effect.second);
        if (changed)
            dj.setChannel(index, channel);
    }
}

void DjView::drawTempo(const int index, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    const auto deck = dj.deck(index);
    auto& ui = decks_[static_cast<std::size_t>(index)];
    ImGui::BeginGroup();
    // The range.
    char rangeText[16];
    std::snprintf(rangeText, sizeof(rangeText), "%.0f%%", ui.tempoRange * 100.0f);
    if (ImGui::Button(rangeText, ImVec2(size.x, 0.0f)))
        ImGui::OpenPopup("##range");
    ui::tooltip(L("L'étendue du fader de tempo", "The tempo fader's range"));
    if (ImGui::BeginPopup("##range")) {
        for (const auto range : { 0.04f, 0.08f, 0.16f, 0.5f }) {
            char label[16];
            std::snprintf(label, sizeof(label), "+/- %.0f %%", range * 100.0f);
            if (ImGui::Selectable(label, range == ui.tempoRange))
                ui.tempoRange = range;
        }
        ImGui::EndPopup();
    }
    // The fader: slower at the top, faster at the bottom, as on a turntable.
    const auto synced = deck.sync && !deck.master && dj.masterBpm() > 0.0;
    auto percent = static_cast<float>(((synced ? deck.speed : deck.tempo) - 1.0) * 100.0);
    const auto faderHeight = std::max(dp(60.0f), size.y - ImGui::GetFrameHeight() * 3.0f - dp(36.0f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, synced ? p.textFaint : deckColour(index));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, deckColour(index));
    if (ImGui::VSliderFloat("##tempo", ImVec2(size.x, faderHeight), &percent, ui.tempoRange * 100.0f, -ui.tempoRange * 100.0f, "")
        && !synced)
        dj.setTempo(index, 1.0 + percent / 100.0);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        dj.setTempo(index, 1.0);
    ImGui::PopStyleColor(2);
    ui::tooltip(synced ? L("En SYNC : le tempo suit la platine maître", "On SYNC: the tempo follows the master deck")
                       : L("Tempo (double-clic : 0 %)", "Tempo (double click: 0 %)"));
    char percentText[16];
    std::snprintf(percentText, sizeof(percentText), "%+.1f", static_cast<double>(percent));
    ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 0.8f);
    ImGui::TextColored(theme::vec(p.textDim), "%s", percentText);
    ImGui::PopFont();
    // The nudges, while held.
    const auto nudge = [&](const char* id, const char* text, const double amount) {
        ImGui::InvisibleButton(id, ImVec2(size.x, ImGui::GetFrameHeight()));
        const auto origin = ImGui::GetItemRectMin();
        const auto end = ImGui::GetItemRectMax();
        const auto active = ImGui::IsItemActive();
        ImGui::GetWindowDrawList()->AddRectFilled(origin, end, active ? deckColour(index) : p.bg3, dp(3.0f));
        ui::centredText(ImGui::GetWindowDrawList(), origin, end, active ? IM_COL32(20, 20, 20, 255) : p.textDim, text);
        if (ImGui::IsItemActivated())
            dj.setBend(index, amount);
        if (ImGui::IsItemDeactivated())
            dj.setBend(index, 0.0);
        ui::tooltip(L("Pousse la platine tant qu'il est tenu, pour caler les temps à l'oreille",
                      "Pushes the deck while held, to line the beats up by ear"));
    };
    nudge("##bendUp", "+", 0.04);
    nudge("##bendDown", "-", -0.04);
    ImGui::EndGroup();
}

// ---- The mixer ---------------------------------------------------------------------------------

void DjView::drawMixer(const ImVec2 size)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    const auto count = dj.shownDeckCount();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg2);
    if (ImGui::BeginChild("##mixer", size, ImGuiChildFlags_Borders)) {
        const auto avail = ImGui::GetContentRegionAvail();
        const auto bottom = dp(118.0f);
        const auto channelWidth = (avail.x - dp(4.0f) * (count - 1)) / static_cast<float>(count);
        // The channels in the decks' order on the crossfader: C A | B D.
        const int order4[4] { 2, 0, 1, 3 };
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                ImGui::SameLine(0.0f, dp(4.0f));
            drawChannel(count == 4 ? order4[i] : i, ImVec2(channelWidth, avail.y - bottom));
        }
        // The crossfader.
        auto crossfader = dj.crossfader();
        ImGui::SetNextItemWidth(avail.x);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, p.text);
        if (ImGui::SliderFloat("##crossfader", &crossfader, 0.0f, 1.0f, ""))
            dj.setCrossfader(crossfader);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            dj.setCrossfader(0.5f);
        ImGui::PopStyleColor();
        ui::tooltip(L("Crossfader : la gauche, la droite (double-clic : le milieu)", "Crossfader: left, right (double click: the middle)"));
        // The master, the headphones, the curve.
        ui::KnobStyle style;
        style.diameter = 26.0f;
        style.format = "%.1f";
        auto master = dj.masterGainDb();
        style.label = "MASTER";
        style.unit = "dB";
        if (ui::knob("##master", &master, -24.0f, 6.0f, 0.0f, style))
            dj.setMasterGainDb(master);
        ImGui::SameLine();
        {
            const auto peak = dj.masterPeak();
            ui::meter("##masterMeter", ImVec2(dp(14.0f), dp(48.0f)), peak, peak, peak * 0.7f, peak * 0.7f, true);
        }
        ImGui::SameLine();
        auto curve = dj.crossfaderCurve();
        style.label = "CURVE";
        style.unit = nullptr;
        style.format = "%.2f";
        if (ui::knob("##curve", &curve, 0.0f, 1.0f, 0.0f, style))
            dj.setCrossfaderCurve(curve);
        ImGui::SameLine();
        auto cueMix = dj.cueMix();
        style.label = "CUE MIX";
        if (ui::knob("##cueMix", &cueMix, 0.0f, 1.0f, 0.5f, style))
            dj.setCueMix(cueMix);
        ImGui::SameLine();
        ImGui::BeginGroup();
        auto split = dj.cueMode() == DjEngine::CueMode::split;
        if (ui::toggle("SPLIT", &split, p.accent, ImVec2(dp(52.0f), 0.0f),
                       L("Casque en « split » : le mix à gauche, la pré-écoute (CUE) à droite, en mono, sur la sortie stéréo",
                         "Split headphones: the mix on the left, the cue on the right, in mono, on the stereo output")))
            dj.setCueMode(split ? DjEngine::CueMode::split : DjEngine::CueMode::off);
        auto gain = autoGain_;
        if (ui::toggle("AUTO GAIN", &gain, p.accent, ImVec2(dp(72.0f), 0.0f),
                       L("Gain réglé à l'analyse : chaque morceau aussi fort que les autres",
                         "Gain set by the analysis: every track as loud as the others")))
            autoGain_ = gain;
        auto limiter = dj.limiter();
        const auto reduction = dj.limiterReductionDb();
        if (ui::toggle("LIMIT", &limiter, reduction < -0.5f ? p.warn : p.accent, ImVec2(dp(52.0f), 0.0f),
                       L("Limiteur du master : le mix ne dépasse jamais -0,3 dBFS", "The master's limiter: the mix never goes over -0.3 dBFS")))
            dj.setLimiter(limiter);
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DjView::drawChannel(const int index, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto& dj = studio_.engine().dj();
    auto channel = dj.channel(index);
    const auto deck = dj.deck(index);
    auto& ui = decks_[static_cast<std::size_t>(index)];
    auto changed = false;
    ImGui::PushID(index + 100);
    ImGui::BeginGroup();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto bottom = origin.y + size.y;
    auto* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + dp(18.0f)), deckColour(index), dp(3.0f));
    ui::centredText(list, origin, ImVec2(origin.x + size.x, origin.y + dp(18.0f)), IM_COL32(20, 20, 20, 255), deckLetter(index));
    ImGui::SetCursorScreenPos(origin);
    if(ImGui::InvisibleButton("##selectDeckChain",ImVec2(size.x,dp(18))))processing_->selectInput(static_cast<std::uint64_t>(index+1));
    ui::tooltip(L("Sélectionner les effets de cette platine","Select this deck's effects"));
    ui::KnobStyle style;
    style.diameter = std::min({30.0f, size.x / ImGui::GetStyle().FontScaleDpi * 0.42f, std::clamp((size.y / ImGui::GetStyle().FontScaleDpi-145.0f)/5.0f-18.0f,14.0f,30.0f)});
    style.colour = deckColour(index);
    style.format = "%.1f";
    const auto knobRow = [&](const char* id, const char* label, float* value, const float minimum, const float maximum,
                             const float initial, const int flags, bool* kill) {
        style.label = label;
        style.flags = flags;
        const auto knobWidth = dp(style.diameter + 12.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (size.x - knobWidth - (kill != nullptr ? dp(22.0f) : 0.0f)) * 0.5f));
        if (ui::knob(id, value, minimum, maximum, initial, style))
            changed = true;
        if (kill != nullptr) {
            ImGui::SameLine(0.0f, dp(2.0f));
            ImGui::PushID(id);
            if (pad("K", ImVec2(dp(18.0f), dp(18.0f)), *kill, IM_COL32(232, 70, 70, 255),
                    L("Coupe la bande (kill)", "Cuts the band (kill)"))) {
                *kill = !*kill;
                changed = true;
            }
            ImGui::PopID();
        }
    };
    style.unit = "dB";
    knobRow("##gain", "GAIN", &channel.trimDb, -24.0f, 12.0f, 0.0f, ui::knobBipolar, nullptr);
    style.unit = nullptr;
    style.format = "%.2f";
    knobRow("##high", "HI", &channel.high, 0.0f, 1.0f, 0.5f, ui::knobBipolar, &channel.killHigh);
    knobRow("##mid", "MID", &channel.mid, 0.0f, 1.0f, 0.5f, ui::knobBipolar, &channel.killMid);
    knobRow("##low", "LOW", &channel.low, 0.0f, 1.0f, 0.5f, ui::knobBipolar, &channel.killLow);
    knobRow("##filter", "FILTER", &channel.filter, -1.0f, 1.0f, 0.0f, ui::knobBipolar, nullptr);
    // CUE in the headphones.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (size.x - dp(40.0f)) * 0.5f));
    if (pad("CUE##pfl", ImVec2(dp(40.0f), ImGui::GetFrameHeight()), channel.cue, IM_COL32(240, 150, 40, 255),
            L("Pré-écoute au casque (mode SPLIT)", "Heard in the headphones (SPLIT mode)"))) {
        channel.cue = !channel.cue;
        changed = true;
    }
    // The fader and the meter, down to its crossfader side.
    const auto faderHeight = std::max(dp(50.0f), bottom - ImGui::GetCursorScreenPos().y - ImGui::GetFrameHeight() - dp(8.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (size.x - dp(46.0f)) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, p.text);
    if (ImGui::VSliderFloat("##fader", ImVec2(dp(26.0f), faderHeight), &channel.fader, 0.0f, 1.0f, ""))
        changed = true;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        channel.fader = 1.0f;
        changed = true;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, dp(4.0f));
    ui.meter = std::max(deck.peak, ui.meter * 0.9f);
    ui::meter("##meter", ImVec2(dp(14.0f), faderHeight), deck.peak, deck.peak, deck.peak * 0.7f, deck.peak * 0.7f, true);
    // Its side on the crossfader.
    static constexpr const char* sides[3] { "L", "-", "R" };
    const auto sideWidth = (size.x - dp(4.0f) * 2.0f) / 3.0f;
    for (int side = 0; side < 3; ++side) {
        if (side > 0)
            ImGui::SameLine(0.0f, dp(4.0f));
        ImGui::PushID(side);
        if (pad(sides[side], ImVec2(sideWidth, ImGui::GetFrameHeight() * 0.8f), channel.side == side, deckColour(index),
                L("Le côté du crossfader (- : aucun, le fader seul)", "The crossfader's side (-: none, the fader only)"))) {
            channel.side = side;
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
    if (changed)
        dj.setChannel(index, channel);
}

// ---- The collection ----------------------------------------------------------------------------

void DjView::drawBrowser(const ImVec2 size)
{
    const auto& p = theme::palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg1);
    if (ImGui::BeginChild("##djBrowser", size, ImGuiChildFlags_Borders)) {
        const auto avail = ImGui::GetContentRegionAvail();
        const auto treeWidth = std::min(dp(230.0f), avail.x * 0.3f);
        drawCollectionTree(ImVec2(treeWidth, avail.y));
        ImGui::SameLine();
        drawTrackTable(ImVec2(ImGui::GetContentRegionAvail().x, avail.y));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DjView::chooseFolder()
{
    chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Un dossier de musique", "A music folder"),
                                                   juce::File::getSpecialLocation(juce::File::userMusicDirectory));
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this](const juce::FileChooser& chooser) {
                              if (const auto folder = chooser.getResult(); folder.isDirectory())
                                  library_->addFolder(folder);
                          });
}

void DjView::drawCollectionTree(const ImVec2 size)
{
    const auto& p = theme::palette();
    if (ImGui::BeginChild("##collectionTree", size)) {
        ImGui::PushFont(theme::boldFont(), 0.0f);
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Collection", "Collection"));
        ImGui::PopFont();
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - dp(44.0f));
        if (ui::iconButton(icon::add, L("Ajouter un dossier de musique", "Add a music folder")))
            chooseFolder();
        ImGui::SameLine(0.0f, dp(2.0f));
        if (ui::iconButton(icon::refresh, L("Relire les dossiers", "Read the folders again")))
            library_->rescan();
        const auto folders = library_->folders();
        if (ImGui::Selectable(L("Tous les morceaux", "Every track"), folder_ == -1))
            folder_ = -1;
        if (ImGui::Selectable(L("Joués cette session", "Played this session"), folder_ == -2))
            folder_ = -2;
        ImGui::Separator();
        auto remove = -1;
        for (int i = 0; i < folders.size(); ++i) {
            ImGui::PushID(i);
            const juce::File folder(folders[i]);
            if (ImGui::Selectable((juce::String::fromUTF8(icon::folder) + " " + folder.getFileName()).toRawUTF8(), folder_ == i))
                folder_ = i;
            ui::tooltip(folder.getFullPathName().toRawUTF8());
            if (ImGui::BeginPopupContextItem("##folderMenu")) {
                if (ImGui::MenuItem(L("Retirer de la collection", "Remove from the collection")))
                    remove = i;
                if (ImGui::MenuItem(L("Afficher dans l'explorateur", "Show in the explorer")))
                    folder.revealToUser();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (remove >= 0) {
            library_->removeFolder(juce::File(folders[remove]));
            folder_ = -1;
        }
        if (folders.isEmpty())
            ImGui::TextWrapped("%s", L("Ajoutez un dossier de musique (+), ou glissez des morceaux sur les platines.",
                                       "Add a music folder (+), or drop tracks on the decks."));
    }
    ImGui::EndChild();
}

void DjView::refreshRows()
{
    const juce::String query = juce::String::fromUTF8(search_.data()).trim();
    if (shownRevision_ == library_->revision() && shownQuery_ == query && shownFolder_ == folder_ && !sortChanged_)
        return;
    shownRevision_ = library_->revision();
    shownQuery_ = query;
    shownFolder_ = folder_;
    sortChanged_ = false;
    const auto folders = library_->folders();
    const auto words = juce::StringArray::fromTokens(query.toLowerCase(), " ", "\"");
    rows_.clear();
    for (auto& track : library_->tracks()) {
        if (folder_ >= 0 && folder_ < folders.size() && !track.file.isAChildOf(juce::File(folders[folder_])))
            continue;
        if (folder_ == -2 && std::find(history_.begin(), history_.end(), track.file) == history_.end())
            continue;
        auto keep = true;
        const auto haystack = (track.title + " " + track.artist + " " + track.file.getFileName()).toLowerCase();
        for (const auto& word : words)
            if (word.isNotEmpty() && !haystack.contains(word))
                keep = false;
        if (keep)
            rows_.push_back(std::move(track));
    }
    const auto ascending = sortAscending_;
    const auto column = sortColumn_;
    std::stable_sort(rows_.begin(), rows_.end(), [column, ascending](const DjTrack& a, const DjTrack& b) {
        auto less = false;
        auto greater = false;
        switch (column) {
        case 1: less = a.title.compareNatural(b.title) < 0; greater = b.title.compareNatural(a.title) < 0; break;
        case 2: less = a.artist.compareNatural(b.artist) < 0; greater = b.artist.compareNatural(a.artist) < 0; break;
        case 3: less = a.bpm < b.bpm; greater = b.bpm < a.bpm; break;
        case 4: less = a.key < b.key; greater = b.key < a.key; break;
        case 5: less = a.seconds < b.seconds; greater = b.seconds < a.seconds; break;
        case 6: less = a.plays < b.plays; greater = b.plays < a.plays; break;
        default: less = a.file.getFileName().compareNatural(b.file.getFileName()) < 0; greater = false; break;
        }
        return ascending ? less : greater;
    });
}

void DjView::trackMenu(const DjTrack& track)
{
    auto& dj = studio_.engine().dj();
    for (int deck = 0; deck < dj.shownDeckCount(); ++deck) {
        const auto label = ui::Ls("Charger sur la platine ", "Load on deck ") + deckLetter(deck);
        if (ImGui::MenuItem(label.toRawUTF8()))
            loadFile(deck, track.file);
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Analyser", "Analyse")))
        library_->analyse(track.file);
    if (ImGui::MenuItem(L("Ouvrir dans la Fonderie", "Open in the Foundry"))) {
        state_.foundryOpen = track.file;
        state_.mode = AppMode::foundry;
    }
    if (ImGui::MenuItem(L("Afficher dans l'explorateur", "Show in the explorer")))
        track.file.revealToUser();
}

void DjView::drawTrackTable(const ImVec2 size)
{
    const auto& p = theme::palette();
    if (!ImGui::BeginChild("##tracks", size)) {
        ImGui::EndChild();
        return;
    }
    // Search, analysis.
    ImGui::SetNextItemWidth(std::min(dp(320.0f), ImGui::GetContentRegionAvail().x * 0.45f));
    ImGui::InputTextWithHint("##search", L("Chercher un titre, un artiste…", "Search a title, an artist…"), search_.data(), search_.size());
    ImGui::SameLine();
    if (ImGui::Button(L("Tout analyser", "Analyse all")))
        library_->analyseAll();
    ui::tooltip(L("Tempo, grille, tonalité et volume de chaque morceau pas encore analysé (en tâche de fond)",
                  "Tempo, grid, key and loudness of every track not analysed yet (in the background)"));
    const auto progress = library_->progress();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (progress.scanning)
        ImGui::TextColored(theme::vec(p.accent), "%s", L("Lecture des dossiers…", "Reading the folders…"));
    else if (progress.waiting > 0)
        ImGui::TextColored(theme::vec(p.accent), "%s %d  %s", L("Analyse :", "Analysing:"), progress.waiting, progress.current.toRawUTF8());
    refreshRows();
    ImGui::SameLine();
    ImGui::TextColored(theme::vec(p.textFaint), "%d %s", static_cast<int>(rows_.size()), L("morceaux", "tracks"));

    constexpr auto flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable
                           | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Hideable;
    if (ImGui::BeginTable("##trackTable", 7, flags, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##state", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, dp(16.0f), 0);
        ImGui::TableSetupColumn(L("Titre", "Title"), ImGuiTableColumnFlags_DefaultSort, 3.0f, 1);
        ImGui::TableSetupColumn(L("Artiste", "Artist"), 0, 2.0f, 2);
        ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthFixed, dp(60.0f), 3);
        ImGui::TableSetupColumn(L("Tonalité", "Key"), ImGuiTableColumnFlags_WidthFixed, dp(70.0f), 4);
        ImGui::TableSetupColumn(L("Durée", "Time"), ImGuiTableColumnFlags_WidthFixed, dp(56.0f), 5);
        ImGui::TableSetupColumn(L("Joué", "Played"), ImGuiTableColumnFlags_WidthFixed, dp(44.0f), 6);
        ImGui::TableHeadersRow();
        if (auto* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                sortColumn_ = static_cast<int>(specs->Specs[0].ColumnUserID);
                sortAscending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
                sortChanged_ = true;
            }
            specs->SpecsDirty = false;
        }
        auto& dj = studio_.engine().dj();
        std::array<juce::File, 4> onDecks;
        for (int deck = 0; deck < 4; ++deck)
            onDecks[static_cast<std::size_t>(deck)] = dj.deck(deck).file;
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows_.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& track = rows_[static_cast<std::size_t>(row)];
                ImGui::PushID(row);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                // On a deck: its colour.
                for (int deck = 0; deck < 4; ++deck)
                    if (onDecks[static_cast<std::size_t>(deck)] == track.file) {
                        const auto at = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + dp(10.0f), at.y + ImGui::GetTextLineHeight()),
                                                                  deckColour(deck), dp(2.0f));
                        break;
                    }
                ImGui::TableSetColumnIndex(1);
                const auto chosen = selected_ == track.file;
                if (ImGui::Selectable(track.title.toRawUTF8(), chosen, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    selected_ = track.file;
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    if (const auto deck = freeDeck(); deck >= 0)
                        loadFile(deck, track.file);
                if (ImGui::BeginDragDropSource()) {
                    const auto path = track.file.getFullPathName().toStdString();
                    ImGui::SetDragDropPayload(kFilePayload, path.c_str(), path.size() + 1);
                    ImGui::TextUnformatted(track.title.toRawUTF8());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginPopupContextItem("##trackMenu")) {
                    trackMenu(track);
                    ImGui::EndPopup();
                }
                if (track.error.isNotEmpty())
                    ui::tooltip(track.error.toRawUTF8());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(theme::vec(p.textDim), "%s", track.artist.toRawUTF8());
                ImGui::TableSetColumnIndex(3);
                if (track.bpm > 0.0)
                    ImGui::Text("%.2f", track.bpm);
                else
                    ImGui::TextColored(theme::vec(p.textFaint), "%s", track.analysed ? "-" : "?");
                ImGui::TableSetColumnIndex(4);
                ImGui::TextColored(theme::vec(p.textDim), "%s", keyText(track.key).toRawUTF8());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextColored(theme::vec(p.textDim), "%s", track.seconds > 0.0 ? clock(track.seconds, false).toRawUTF8() : "");
                ImGui::TableSetColumnIndex(6);
                if (track.plays > 0)
                    ImGui::TextColored(theme::vec(p.textFaint), "%d", track.plays);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

// ---- The control port --------------------------------------------------------------------------

juce::var DjView::deckJson(const int index) const
{
    auto& dj = studio_.engine().dj();
    const auto deck = dj.deck(index);
    const auto channel = dj.channel(index);
    auto* item = new juce::DynamicObject();
    static constexpr const char* states[4] { "empty", "loading", "ready", "failed" };
    item->setProperty("deck", deckLetter(index));
    item->setProperty("state", states[static_cast<int>(deck.state)]);
    item->setProperty("file", deck.file.getFullPathName());
    item->setProperty("title", deck.title);
    item->setProperty("artist", deck.artist);
    if (deck.error.isNotEmpty())
        item->setProperty("error", deck.error);
    item->setProperty("analysed", deck.analysis != nullptr);
    item->setProperty("seconds", deck.seconds);
    item->setProperty("position", deck.position);
    item->setProperty("playing", deck.playing);
    item->setProperty("tempo", deck.tempo);
    item->setProperty("speed", deck.speed);
    item->setProperty("bpm", deck.bpm);
    item->setProperty("playedBpm", deck.playedBpm);
    item->setProperty("firstBeat", deck.firstBeat);
    if (deck.analysis != nullptr) {
        item->setProperty("key", engine::dj::keyName(deck.analysis->key));
        item->setProperty("camelot", engine::dj::camelotName(deck.analysis->key));
        item->setProperty("loudness", deck.analysis->loudness);
    }
    item->setProperty("keylock", deck.keylock);
    item->setProperty("sync", deck.sync);
    item->setProperty("master", deck.master);
    item->setProperty("phase", deck.phase);
    item->setProperty("looping", deck.looping);
    item->setProperty("loopStart", deck.loopStart);
    item->setProperty("loopEnd", deck.loopEnd);
    item->setProperty("cuePoint", deck.cuePoint);
    juce::Array<juce::var> cues;
    for (const auto cue : deck.hotCues)
        cues.add(cue);
    item->setProperty("hotCues", cues);
    item->setProperty("peakDb", deck.peak > 1.0e-6f ? 20.0 * std::log10(deck.peak) : -120.0);
    item->setProperty("trimDb", channel.trimDb);
    item->setProperty("fader", channel.fader);
    item->setProperty("effect", engine::dj::effectInfo(channel.effect.kind).key);
    item->setProperty("effectOn", channel.effect.on);
    return juce::var(item);
}

juce::var DjView::command(const juce::var& request)
{
    ensureLoaded();
    auto& dj = studio_.engine().dj();
    const auto action = request.getProperty("action", "status").toString().trim().toLowerCase();
    if(action=="processing") {
        const auto result=processing_->command(request);saveProcessing();
        return static_cast<bool>(result.getProperty("ok",false))?result.getProperty("result",{}):result.getProperty("error",{});
    }
    // A deck by its letter or its index.
    auto deck = -1;
    if (const auto value = request.getProperty("deck", juce::var()); !value.isVoid()) {
        if (value.isString()) {
            const auto letter = value.toString().trim().toUpperCase();
            deck = letter.isNotEmpty() ? static_cast<int>(letter[0] - 'A') : -1;
        } else {
            deck = static_cast<int>(value);
        }
    }
    const auto needDeck = [&]() -> juce::String {
        if (deck < 0 || deck >= DjEngine::deckCount)
            return "deck: A, B, C or D (or 0..3)";
        return {};
    };
    juce::String error;
    if (action == "status" || action.isEmpty()) {
        // Below.
    } else if (action == "load") {
        if ((error = needDeck()).isEmpty()) {
            const juce::File file(request.getProperty("file", juce::var()).toString());
            if (!file.existsAsFile())
                error = "no file \"" + file.getFullPathName() + "\"";
            else
                loadFile(deck, file);
        }
    } else if (action == "eject") {
        if ((error = needDeck()).isEmpty())
            dj.eject(deck);
    } else if (action == "play") {
        if ((error = needDeck()).isEmpty())
            dj.setPlaying(deck, flag(request, "on", true));
    } else if (action == "cue") {
        if ((error = needDeck()).isEmpty())
            dj.cue(deck, flag(request, "down", true));
    } else if (action == "seek") {
        if ((error = needDeck()).isEmpty())
            dj.seek(deck, number(request, "seconds", 0.0));
    } else if (action == "tempo") {
        if ((error = needDeck()).isEmpty()) {
            const auto state = dj.deck(deck);
            if (request.hasProperty("bpm") && state.bpm > 0.0)
                dj.setTempo(deck, number(request, "bpm", state.bpm) / state.bpm);
            else if (request.hasProperty("percent"))
                dj.setTempo(deck, 1.0 + number(request, "percent", 0.0) / 100.0);
            else
                dj.setTempo(deck, number(request, "ratio", 1.0));
        }
    } else if (action == "bend") {
        if ((error = needDeck()).isEmpty())
            dj.setBend(deck, number(request, "amount", 0.0));
    } else if (action == "sync") {
        if ((error = needDeck()).isEmpty())
            dj.setSync(deck, flag(request, "on", true));
    } else if (action == "keylock") {
        if ((error = needDeck()).isEmpty())
            dj.setKeylock(deck, flag(request, "on", true));
    } else if (action == "master") {
        dj.setMaster(deck);
    } else if (action == "hotcue") {
        if ((error = needDeck()).isEmpty()) {
            const auto what = request.getProperty("do", "jump").toString().toLowerCase();
            const auto type = what == "set" ? DjEngine::HotCueAction::set
                              : what == "remove" ? DjEngine::HotCueAction::remove
                                                 : DjEngine::HotCueAction::jumpOrSet;
            dj.hotCue(deck, static_cast<int>(number(request, "index", 0.0)), type);
        }
    } else if (action == "loop") {
        if ((error = needDeck()).isEmpty())
            dj.loopBeats(deck, number(request, "beats", 0.0));
    } else if (action == "loop_move") {
        if ((error = needDeck()).isEmpty())
            dj.loopMove(deck, number(request, "beats", 1.0));
    } else if (action == "loop_resize") {
        if ((error = needDeck()).isEmpty())
            dj.loopResize(deck, number(request, "factor", 2.0));
    } else if (action == "beatjump") {
        if ((error = needDeck()).isEmpty())
            dj.beatJump(deck, number(request, "beats", 4.0));
    } else if (action == "grid") {
        if ((error = needDeck()).isEmpty()) {
            const auto state = dj.deck(deck);
            dj.setGrid(deck, number(request, "bpm", state.bpm), number(request, "firstBeat", state.firstBeat));
            if (state.file != juce::File())
                if (auto track = library_->find(state.file)) {
                    track->bpm = number(request, "bpm", state.bpm);
                    track->firstBeat = number(request, "firstBeat", state.firstBeat);
                    track->gridSet = true;
                    library_->update(*track);
                }
        }
    } else if (action == "channel") {
        if ((error = needDeck()).isEmpty()) {
            auto channel = dj.channel(deck);
            channel.trimDb = static_cast<float>(number(request, "trimDb", channel.trimDb));
            channel.high = static_cast<float>(number(request, "high", channel.high));
            channel.mid = static_cast<float>(number(request, "mid", channel.mid));
            channel.low = static_cast<float>(number(request, "low", channel.low));
            channel.killHigh = flag(request, "killHigh", channel.killHigh);
            channel.killMid = flag(request, "killMid", channel.killMid);
            channel.killLow = flag(request, "killLow", channel.killLow);
            channel.filter = static_cast<float>(number(request, "filter", channel.filter));
            channel.resonance = static_cast<float>(number(request, "resonance", channel.resonance));
            channel.fader = static_cast<float>(number(request, "fader", channel.fader));
            channel.cue = flag(request, "cue", channel.cue);
            if (const auto side = request.getProperty("side", juce::var()); !side.isVoid())
                channel.side = side.isString() ? (side.toString().equalsIgnoreCase("left") ? 0 : side.toString().equalsIgnoreCase("right") ? 2 : 1)
                                               : static_cast<int>(side);
            if (const auto effect = request.getProperty("effect", juce::var()); effect.isObject()) {
                const auto kind = effect.getProperty("kind", juce::var()).toString();
                for (int k = 0; k < engine::dj::effectKindCount; ++k)
                    if (kind.equalsIgnoreCase(engine::dj::effectInfo(static_cast<engine::dj::EffectKind>(k)).key))
                        channel.effect.kind = static_cast<engine::dj::EffectKind>(k);
                channel.effect.on = flag(effect, "on", channel.effect.on);
                channel.effect.mix = static_cast<float>(number(effect, "mix", channel.effect.mix));
                channel.effect.first = static_cast<float>(number(effect, "first", channel.effect.first));
                channel.effect.second = static_cast<float>(number(effect, "second", channel.effect.second));
            }
            dj.setChannel(deck, channel);
        }
    } else if (action == "mixer") {
        dj.setCrossfader(static_cast<float>(number(request, "crossfader", dj.crossfader())));
        dj.setCrossfaderCurve(static_cast<float>(number(request, "curve", dj.crossfaderCurve())));
        dj.setMasterGainDb(static_cast<float>(number(request, "masterDb", dj.masterGainDb())));
        dj.setCueMix(static_cast<float>(number(request, "cueMix", dj.cueMix())));
        if (request.hasProperty("split"))
            dj.setCueMode(flag(request, "split", false) ? DjEngine::CueMode::split : DjEngine::CueMode::off);
        if (request.hasProperty("decks"))
            dj.setDeckCount(static_cast<int>(number(request, "decks", 2.0)));
        if (request.hasProperty("quantize"))
            dj.setQuantize(flag(request, "quantize", true));
        if (request.hasProperty("autoGain"))
            autoGain_ = flag(request, "autoGain", true);
        if (request.hasProperty("limiter"))
            dj.setLimiter(flag(request, "limiter", true));
    } else if (action == "record") {
        if (flag(request, "on", true)) {
            const auto path = request.getProperty("path", juce::var()).toString();
            const auto result = path.isNotEmpty() ? dj.startRecording(juce::File(path)) : juce::Result::ok();
            if (path.isEmpty())
                startRecording();
            if (result.failed())
                error = result.getErrorMessage();
        } else {
            dj.stopRecording();
        }
    } else if (action == "folders") {
        if (const auto add = request.getProperty("add", juce::var()).toString(); add.isNotEmpty())
            library_->addFolder(juce::File(add));
        if (const auto remove = request.getProperty("remove", juce::var()).toString(); remove.isNotEmpty())
            library_->removeFolder(juce::File(remove));
        if (flag(request, "scan", false))
            library_->rescan();
    } else if (action == "analyse") {
        if (const auto file = request.getProperty("file", juce::var()).toString(); file.isNotEmpty())
            library_->analyse(juce::File(file));
        else
            library_->analyseAll();
    } else if (action == "tracks") {
        // Below, with the list.
    } else {
        error = "action: status, load, eject, play, cue, seek, tempo, bend, sync, keylock, master, hotcue, loop, loop_move, "
                "loop_resize, beatjump, grid, channel, mixer, record, folders, analyse, tracks";
    }
    if (error.isNotEmpty())
        return error;
    auto* result = new juce::DynamicObject();
    juce::Array<juce::var> decks;
    for (int index = 0; index < DjEngine::deckCount; ++index)
        decks.add(deckJson(index));
    result->setProperty("decks", decks);
    result->setProperty("deckCount", dj.shownDeckCount());
    result->setProperty("masterBpm", dj.masterBpm());
    result->setProperty("masterDeck", dj.masterDeck());
    result->setProperty("crossfader", dj.crossfader());
    result->setProperty("masterPeakDb", dj.masterPeak() > 1.0e-6f ? 20.0 * std::log10(dj.masterPeak()) : -120.0);
    result->setProperty("split", dj.cueMode() == DjEngine::CueMode::split);
    result->setProperty("quantize", dj.quantize());
    result->setProperty("limiter", dj.limiter());
    result->setProperty("limiterReductionDb", dj.limiterReductionDb());
    result->setProperty("recording", dj.recording());
    if (dj.recording()) {
        result->setProperty("recordedSeconds", dj.recordedSeconds());
        result->setProperty("recordingFile", dj.recordingFile().getFullPathName());
    }
    const auto progress = library_->progress();
    result->setProperty("scanning", progress.scanning);
    result->setProperty("analysing", progress.waiting);
    juce::Array<juce::var> folders;
    for (const auto& folder : library_->folders())
        folders.add(folder);
    result->setProperty("folders", folders);
    if (action == "tracks") {
        juce::Array<juce::var> list;
        const auto query = request.getProperty("query", juce::var()).toString().toLowerCase();
        for (const auto& track : library_->tracks()) {
            if (query.isNotEmpty() && !(track.title + " " + track.artist + " " + track.file.getFileName()).toLowerCase().contains(query))
                continue;
            auto* item = new juce::DynamicObject();
            item->setProperty("file", track.file.getFullPathName());
            item->setProperty("title", track.title);
            item->setProperty("artist", track.artist);
            item->setProperty("bpm", track.bpm);
            item->setProperty("firstBeat", track.firstBeat);
            item->setProperty("key", engine::dj::keyName(track.key));
            item->setProperty("camelot", engine::dj::camelotName(track.key));
            item->setProperty("seconds", track.seconds);
            item->setProperty("analysed", track.analysed);
            item->setProperty("plays", track.plays);
            list.add(juce::var(item));
        }
        result->setProperty("tracks", list);
    }
    result->setProperty("trackCount", static_cast<int>(library_->tracks().size()));
    return juce::var(result);
}

} // namespace cydaw::v2
