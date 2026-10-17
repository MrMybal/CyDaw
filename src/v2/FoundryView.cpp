#include "FoundryView.h"
#include "FoundryJson.h"
#include "TransportTimecode.h"
#include "ModeProcessing.h"

#include "Icons.h"
#include "ImGuiHost.h"
#include "Locations.h"
#include "NativeDspGraphState.h"
#include "NativeDspRuntime.h"
#include "SoundModelFile.h"
#include "Studio.h"
#include "Theme.h"
#include "Widgets.h"
#include "BottomPanelControls.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace cydaw::v2 {

using engine::foundry::Clip;
using engine::foundry::Track;
using engine::foundry::TrackSet;
using ui::dp;
using ui::L;

namespace {

enum Process : int {
    processGain,
    processNormalise,
    processFadeIn,
    processFadeOut,
    processInvert,
    processReverse,
    processDc,
    processSilence,
    processInsertSilence,
    processStretch,
    processRate,
    processChannels,
    processEffects,
    processCount
};

const char* processName(const int process)
{
    switch (process) {
    case processGain: return L("Volume", "Gain");
    case processNormalise: return L("Normaliser", "Normalise");
    case processFadeIn: return L("Fondu entrant", "Fade in");
    case processFadeOut: return L("Fondu sortant", "Fade out");
    case processInvert: return L("Inverser la polarité", "Invert the polarity");
    case processReverse: return L("Renverser", "Reverse");
    case processDc: return L("Retirer le décalage (DC)", "Remove the DC offset");
    case processSilence: return L("Rendre silencieux", "Make silent");
    case processInsertSilence: return L("Insérer du silence", "Insert silence");
    case processStretch: return L("Étirer et transposer", "Stretch and transpose");
    case processRate: return L("Fréquence d'échantillonnage", "Sample rate");
    case processChannels: return L("Canaux", "Channels");
    case processEffects: return L("Effets d'une piste", "A track's effects");
    default: return "";
    }
}

juce::String timeText(const std::int64_t frames, const double rate)
{
    const auto seconds = rate > 0.0 ? static_cast<double>(frames) / rate : 0.0;
    const auto minutes = static_cast<int>(seconds / 60.0);
    char text[32];
    std::snprintf(text, sizeof(text), "%d:%06.3f", minutes, seconds - minutes * 60.0);
    return text;
}

double number(const juce::var& object, const char* key, const double fallback)
{
    const auto value = object.getProperty(key, juce::var());
    return value.isVoid() ? fallback : static_cast<double>(value);
}

juce::String fromUtf8(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

constexpr float headerWidthDp = 184.0f;
constexpr float clipHeaderDp = 15.0f;
constexpr float rowHeightDp = 112.0f;
constexpr float bandWidthDp = 12.0f;
constexpr float addRowDp = 34.0f;

} // namespace

FoundryView::FoundryView(Studio& studio, UiState& state, ImGuiHost& host, const bool cinema) : cinema_(cinema),studio_(studio), state_(state), host_(host) {}
app::FoundryPlayer& FoundryView::player()const {return cinema_?studio_.engine().cinemaPlayer():studio_.engine().foundryPlayer();}

FoundryView::~FoundryView()
{
    cinemaAudioWorker_.removeAllJobs(true,10000);
    for(const auto& task:cinemaAudioTasks_)task->target.deleteFile();
    if(cinema_)for(auto& t:tabs_){host_.releaseTexture(t.videoTextureKey);releaseCinemaThumbnails(t);}
    stop();
}

FoundryView::Tab* FoundryView::tab()
{
    if (current_ < 0 || current_ >= static_cast<int>(tabs_.size()))
        return nullptr;
    return &tabs_[static_cast<std::size_t>(current_)];
}

const engine::foundry::Session& FoundryView::shown(const Tab& tab) const
{
    return preview_ ? *preview_ : tab.document->session();
}

void FoundryView::report(const juce::String& message, const bool error)
{
    state_.status = message;
    state_.statusIsError = error;
    state_.statusTime = ImGui::GetTime();
}

void FoundryView::showToolsPanel(int panel)
{
    if(auto* t=tab()){syncProcessing(*t);t->processing->showPanel(panel);state_.setBottomPanelVisible(true);}
}
juce::StringArray FoundryView::unsaved() const
{
    juce::StringArray names;
    for (const auto& tab : tabs_)
        if (tab.document->dirty() || (tab.processing && tab.processingRevision != tab.processing->revision()))
            names.add(tab.document->name());
    return names;
}

// ---- Files -------------------------------------------------------------------------------------

bool FoundryView::open(const juce::File& file)
{
    if(cinema_&&file.hasFileExtension("mp4;mov;m4v;avi;wmv;mkv;webm;mpeg;mpg;mxf;ts;mts;m2ts;m2v;vob;ogv;flv;3gp;3g2;dv;nut;rm;rmvb"))return openCinemaVideo(file);
    for (std::size_t i = 0; i < tabs_.size(); ++i)
        if ((tabs_[i].document->file() == file || tabs_[i].document->sessionFile() == file) && file != juce::File()) {
            current_ = static_cast<int>(i);barSelected_=-1;
            return true;
        }
    juce::String error;
    auto document = FoundryDocument::open(file, error);
    if (document == nullptr) {
        report(error, true);
        return false;
    }
    Tab tab;
    tab.document = std::move(document);
    if(cinema_)ensureCinema(tab);
    saveBits_ = tab.document->bitsPerSample();
    tabs_.push_back(std::move(tab));
    current_ = static_cast<int>(tabs_.size()) - 1;barSelected_=-1;
    report(error.isNotEmpty() ? error : ui::Ls("Ouvert : ", "Open: ") + file.getFileName(), error.isNotEmpty());
    return true;
}

bool FoundryView::importFile(Tab& tab, const juce::File& file, const int track, const std::int64_t at)
{
    juce::String error;
    const auto sound = FoundryDocument::readSound(file, error);
    if (sound == nullptr) {
        report(error, true);
        return false;
    }
    const auto& session = tab.document->session();
    const auto index = track >= 0 && track < static_cast<int>(session.tracks.size()) ? track : static_cast<int>(session.tracks.size());
    if(index<static_cast<int>(session.tracks.size()) && session.tracks[index].bus)return importFile(tab,file,-1,at);
    if(index<static_cast<int>(session.tracks.size()) && session.tracks[index].midi) {
        syncProcessing(tab);
        if(const auto result=tab.processing->loadSample(session.tracks[index].id,file);result.failed()){report(result.getErrorMessage(),true);return false;}
        auto next=session;Clip clip;clip.id=next.newId();clip.start=std::max<std::int64_t>(0,at);clip.length=std::max<std::int64_t>(1,static_cast<std::int64_t>(sound->frames()*session.rate/sound->rate()));clip.name=file.getFileNameWithoutExtension().toStdString();
        clip.notes.push_back({next.newId(),0,clip.length,60,1});tab.clip=clip.id;
        next.tracks[index].clips.push_back(std::move(clip));
        tab.document->setProcessing(tab.processing->serialise());
        edit(tab,std::move(next),ui::Ls("Son dans le sampler","Sound into sampler"));pickTrack(tab,index,false);tab.detailsFocus=true;return true;
    }
    auto next = engine::foundry::withClip(session, index, std::max<std::int64_t>(0, at), *sound,
                                          file.getFileNameWithoutExtension().toStdString(), file.getFullPathName().toStdString());
    edit(tab, std::move(next), ui::Ls("Importer ", "Import ") + file.getFileName());
    pickTrack(tab, index, false);
    return true;
}

bool FoundryView::dropFiles(const juce::StringArray& files, const float x, const float y)
{
    if(cinema_)return dropCinemaFiles(files,x,y);
    auto any = false;
    for (int i = 0; i < files.size(); ++i) {
        const juce::File file(files[i]);
        if (!file.existsAsFile())
            continue;
        auto* current = tab();
        if (current == nullptr || file.hasFileExtension("cyfoundry")) {
            any = open(file) || any;
            continue;
        }
        // Onto the track under the pointer, at its place; else a new track
        // at the cursor.
        auto track = -1;
        auto at = current->cursor;
        if (x >= lanesMin_.x && x < lanesMax_.x && y >= lanesMin_.y && y < lanesMax_.y) {
            const auto rowHeight = dp(rowHeightDp) * current->laneZoom;
            const auto row = static_cast<int>((y - lanesMin_.y + current->laneScroll) / rowHeight);
            if (row < static_cast<int>(current->document->session().tracks.size()))
                track = row;
            at = std::max<std::int64_t>(0, static_cast<std::int64_t>(current->viewStart + (x - lanesMin_.x) * current->framesPerPixel));
        }
        any = importFile(*current, file, i == 0 ? track : -1, at) || any;
    }
    return any;
}

void FoundryView::chooseOpen()
{
    chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Un fichier son ou une session", "A sound file or a session"), juce::File {},
                                                   cinema_?"*.cycinema;*.mp4;*.mov;*.m4v;*.avi;*.wmv;*.mkv;*.webm;*.mpeg;*.mpg;*.mxf;*.ts;*.mts;*.m2ts;*.m2v;*.vob;*.ogv;*.flv;*.3gp;*.3g2;*.dv;*.nut;*.rm;*.rmvb;*.wav;*.flac":"*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.wma;*.cyfoundry");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this](const juce::FileChooser& chooser) {
                              for (const auto& file : chooser.getResults())
                                  (void) open(file);
                          });
}

void FoundryView::chooseImport()
{
    if (tab() == nullptr) {
        chooseOpen();
        return;
    }
    chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Des sons à ajouter, chacun sur une piste", "Sounds to add, each on a track"),
                                                   juce::File {}, "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.wma");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this](const juce::FileChooser& chooser) {
                              if (auto* current = tab())
                                  for (const auto& file : chooser.getResults())
                                      if(cinema_){juce::StringArray files;files.add(file.getFullPathName());dropCinemaFiles(files,-1,-1);}
                                      else (void) importFile(*current, file, -1, current->cursor);
                          });
}

void FoundryView::newDocument()
{
    Tab tab;
    auto* device = studio_.engine().deviceManager().getCurrentAudioDevice();
    const auto rate = device != nullptr ? device->getCurrentSampleRate() : 48'000.0;
    tab.document = FoundryDocument::create(rate > 0.0 ? rate : 48'000.0, ui::Ls("Sans titre ", "Untitled ") + juce::String(++newDocuments_));
    if(cinema_)ensureCinema(tab);
    tabs_.push_back(std::move(tab));
    current_ = static_cast<int>(tabs_.size()) - 1;barSelected_=-1;
}

void FoundryView::save(const bool as)
{
    auto* tab = this->tab();
    if (tab == nullptr)
        return;
    syncProcessing(*tab);
    commitProcessing(*tab, true);
    auto& document = *tab->document;
    // A sound file stays a sound file; anything more is a session.
    if (document.plainFile()) {
        if (!as && document.file() != juce::File()) {
            const auto result = document.save(document.file(), saveBits_);
            report(result.wasOk() ? ui::Ls("Enregistré : ", "Saved: ") + document.name() : result.getErrorMessage(), result.failed());
            return;
        }
        const auto folder = document.file() != juce::File() ? document.file().getParentDirectory() : locations::folder(Location::foundry);
        (void) folder.createDirectory();
        chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Enregistrer le son sous", "Save the sound as"),
                                                       folder.getChildFile(document.name()).withFileExtension("wav"), "*.wav;*.flac;*.aiff;*.ogg");
        const auto index = current_;
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                              [this, index](const juce::FileChooser& chooser) {
                                  auto file = chooser.getResult();
                                  if (file == juce::File() || index < 0 || index >= static_cast<int>(tabs_.size()))
                                      return;
                                  if (!file.hasFileExtension("wav;flac;aif;aiff;ogg"))
                                      file = file.withFileExtension("wav");
                                  commitProcessing(tabs_[static_cast<std::size_t>(index)], true);
                                  const auto result = tabs_[static_cast<std::size_t>(index)].document->save(file, saveBits_);
                                  report(result.wasOk() ? ui::Ls("Enregistré : ", "Saved: ") + file.getFileName() : result.getErrorMessage(),
                                         result.failed());
                              });
        return;
    }
    if (!as && document.sessionFile() != juce::File()) {
        const auto result = document.saveSession(document.sessionFile());
        report(result.wasOk() ? ui::Ls("Session enregistrée : ", "Session saved: ") + document.name() : result.getErrorMessage(), result.failed());
        return;
    }
    const auto folder = locations::folder(Location::foundry);
    (void) folder.createDirectory();
    chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Enregistrer la session", "Save the session"),
                                                   folder.getChildFile(juce::File::createLegalFileName(document.name())).withFileExtension(cinema_?"cycinema":"cyfoundry"),
                                                   cinema_?"*.cycinema":"*.cyfoundry");
    const auto index = current_;
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, index](const juce::FileChooser& chooser) {
                              auto file = chooser.getResult();
                              if (file == juce::File() || index < 0 || index >= static_cast<int>(tabs_.size()))
                                  return;
                              file = file.withFileExtension(cinema_?"cycinema":"cyfoundry");
                              commitProcessing(tabs_[static_cast<std::size_t>(index)], true);
                              const auto result = tabs_[static_cast<std::size_t>(index)].document->saveSession(file);
                              report(result.wasOk() ? ui::Ls("Session enregistrée : ", "Session saved: ") + file.getFileName()
                                                    : result.getErrorMessage(),
                                     result.failed());
                          });
}

void FoundryView::chooseExport()
{
    auto* tab = this->tab();
    if (tab == nullptr)
        return;
    const auto folder = locations::folder(Location::foundry);
    (void) folder.createDirectory();
    chooser_ = std::make_unique<juce::FileChooser>(ui::Ls("Exporter le mixage", "Export the mix"),
                                                   folder.getChildFile(juce::File::createLegalFileName(tab->document->name()))
                                                       .withFileExtension("wav"),
                                                   "*.wav;*.flac;*.aiff;*.ogg");
    const auto index = current_;
    const auto start = hasSelection(*tab) ? tab->selectionStart : std::int64_t {};
    const auto end = hasSelection(*tab) ? tab->selectionEnd : std::int64_t {};
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, index, start, end](const juce::FileChooser& chooser) {
                              auto file = chooser.getResult();
                              if (file == juce::File() || index < 0 || index >= static_cast<int>(tabs_.size()))
                                  return;
                              if (!file.hasFileExtension("wav;flac;aif;aiff;ogg"))
                                  file = file.withFileExtension("wav");
                              commitProcessing(tabs_[static_cast<std::size_t>(index)], true);
                              const auto result = tabs_[static_cast<std::size_t>(index)].document->exportMix(file, saveBits_, start, end);
                              report(result.wasOk() ? ui::Ls("Mixage exporté : ", "Mix exported: ") + file.getFileName() : result.getErrorMessage(),
                                     result.failed());
                          });
}

void FoundryView::closeTab(const int index)
{
    if (index < 0 || index >= static_cast<int>(tabs_.size()))
        return;
    if (playingTab_ == index)
        stop();
    if(cinema_){host_.releaseTexture(tabs_[index].videoTextureKey);releaseCinemaThumbnails(tabs_[index]);}
    tabs_.erase(tabs_.begin() + index);
    barSelected_=-1;
    if (playingTab_ > index)
        --playingTab_;
    current_ = std::min(current_, static_cast<int>(tabs_.size()) - 1);
}

// ---- What is picked ----------------------------------------------------------------------------

bool FoundryView::hasSelection(const Tab& tab) const
{
    return tab.selectionEnd > tab.selectionStart;
}

std::pair<std::int64_t, std::int64_t> FoundryView::target(const Tab& tab) const
{
    if (hasSelection(tab))
        return { tab.selectionStart, tab.selectionEnd };
    return { 0, tab.document->session().frames() };
}

TrackSet FoundryView::picked(const Tab& tab) const
{
    const auto count = tab.document->session().tracks.size();
    TrackSet set(count, false);
    auto any = false;
    for (std::size_t t = 0; t < count; ++t)
        if (t < tab.tracks.size() && tab.tracks[t]) {
            set[t] = true;
            any = true;
        }
    if (!any)
        std::fill(set.begin(), set.end(), true);
    return set;
}

bool FoundryView::allTracks(const Tab& tab) const
{
    const auto set = picked(tab);
    return std::all_of(set.begin(), set.end(), [](const bool on) { return on; });
}

void FoundryView::select(Tab& tab, std::int64_t start, std::int64_t end)
{
    start = std::max<std::int64_t>(0, start);
    end = std::max<std::int64_t>(0, end);
    if (end < start)
        std::swap(start, end);
    tab.selectionStart = start;
    tab.selectionEnd = end;
    tab.cursor = start;
    tab.selectionChanged = ImGui::GetTime();
}

void FoundryView::pickTrack(Tab& tab, const int track, const bool add)
{
    if(tab.processing && track>=0 && track<static_cast<int>(tab.document->session().tracks.size()))
        tab.processing->selectInput(tab.document->session().tracks[track].id);

    tab.masterSelected=false;
    const auto count = tab.document->session().tracks.size();
    tab.tracks.resize(count, false);
    if (!add)
        std::fill(tab.tracks.begin(), tab.tracks.end(), false);
    if (track >= 0 && static_cast<std::size_t>(track) < count)
        tab.tracks[static_cast<std::size_t>(track)] = add ? !tab.tracks[static_cast<std::size_t>(track)] : true;
    tab.selectionChanged = ImGui::GetTime();
}

// ---- Editing -----------------------------------------------------------------------------------

void FoundryView::edit(Tab& tab, Session next, const juce::String& label)
{
    if(cinema_)fitCinemaContainers(next);
    tab.document->apply(std::move(next), label);
    if(cinema_)syncProcessing(tab);
    if (playingTab_ == current_)
        this->player().setSession(tab.document->shared());
    tab.tracks.resize(tab.document->session().tracks.size(), false);
    tab.selectionChanged = ImGui::GetTime();
}

void FoundryView::edit(Tab& tab, Session next, const juce::String& label, const std::int64_t at, const std::int64_t removed,
                       const std::int64_t inserted)
{
    // The markers move when every track does.
    auto markers = allTracks(tab) ? tab.document->shiftedMarkers(at, removed, inserted) : tab.document->markers();
    if(cinema_)fitCinemaContainers(next);
    tab.document->apply(std::move(next), std::move(markers), label);
    if(cinema_)syncProcessing(tab);
    if (playingTab_ == current_)
        this->player().setSession(tab.document->shared());
    tab.tracks.resize(tab.document->session().tracks.size(), false);
    tab.selectionChanged = ImGui::GetTime();
}

void FoundryView::copy(Tab& tab)
{
    if (!hasSelection(tab))
        return;
    clipboard_ = engine::foundry::excerpt(tab.document->session(), tab.selectionStart, tab.selectionEnd, picked(tab));
    report(ui::Ls("Copié : ", "Copied: ") + timeText(tab.selectionEnd - tab.selectionStart, tab.document->session().rate), false);
}

void FoundryView::cut(Tab& tab)
{
    if (!hasSelection(tab))
        return;
    copy(tab);
    remove(tab);
}

void FoundryView::remove(Tab& tab)
{
    const auto& session = tab.document->session();
    // No stretch selected: the clip picked goes.
    if (!hasSelection(tab)) {
        int track = -1;
        int clip = -1;
        if (tab.clip != 0 && session.findClip(tab.clip, track, clip)) {
            auto next = session;
            auto& clips = next.tracks[static_cast<std::size_t>(track)].clips;
            clips.erase(clips.begin() + clip);
            edit(tab, std::move(next), ui::Ls("Supprimer le clip", "Delete the clip"));
            tab.clip = 0;
        }
        return;
    }
    const auto [start, end] = target(tab);
    // One channel of the clips: silence there, the time kept.
    if ((tab.mask & 0b11u) != 0b11u) {
        const auto mask = tab.mask;
        edit(tab,
             engine::foundry::processed(session, start, end, picked(tab),
                                        [mask](const Sound& sound, std::int64_t from, std::int64_t to, std::int64_t) {
                                            return engine::foundry::silenced(sound, from, to, mask);
                                        }),
             ui::Ls("Effacer", "Delete"));
        return;
    }
    edit(tab, engine::foundry::removed(session, start, end, picked(tab)), ui::Ls("Supprimer", "Delete"), start, end - start, 0);
    tab.selectionEnd = tab.selectionStart = tab.cursor = start;
}

void FoundryView::crop(Tab& tab)
{
    if (!hasSelection(tab))
        return;
    const auto [start, end] = target(tab);
    std::vector<FoundryMarker> kept;
    for (auto marker : tab.document->markers())
        if (marker.start >= start && marker.start < end) {
            marker.end = marker.region() ? std::min(marker.end, end) - start : marker.start - start;
            marker.start -= start;
            kept.push_back(std::move(marker));
        }
    tab.document->apply(engine::foundry::cropped(tab.document->session(), start, end), std::move(kept), ui::Ls("Rogner", "Crop"));
    if (playingTab_ == current_)
        this->player().setSession(tab.document->shared());
    select(tab, 0, end - start);
}

void FoundryView::paste(Tab& tab, const bool mix)
{
    if (!clipboard_)
        return;
    auto piece = *clipboard_;
    const auto& session = tab.document->session();
    if (std::abs(piece.rate - session.rate) > 0.5)
        piece = engine::foundry::withRate(piece, session.rate);
    const auto set = picked(tab);
    auto first = 0;
    while (first < static_cast<int>(set.size()) && !set[static_cast<std::size_t>(first)])
        ++first;
    auto next = session;
    auto at = tab.cursor;
    auto removedFrames = std::int64_t {};
    // Over a selection: it is replaced.
    if (hasSelection(tab) && !mix) {
        at = tab.selectionStart;
        removedFrames = tab.selectionEnd - tab.selectionStart;
        next = engine::foundry::removed(next, tab.selectionStart, tab.selectionEnd, set);
    }
    next = engine::foundry::pasted(next, at, piece, first, mix);
    if (mix)
        edit(tab, std::move(next), ui::Ls("Coller en mixant", "Paste mixed"));
    else
        edit(tab, std::move(next), ui::Ls("Coller", "Paste"), at, removedFrames, piece.frames());
    select(tab, at, at + piece.frames());
}

void FoundryView::splitAtCursor(Tab& tab)
{
    edit(tab, engine::foundry::split(tab.document->session(), tab.cursor, picked(tab)), ui::Ls("Scinder", "Split"));
}

void FoundryView::duplicateClip(Tab& tab)
{
    int track = -1;
    int clip = -1;
    const auto& session = tab.document->session();
    if (tab.clip == 0 || !session.findClip(tab.clip, track, clip))
        return;
    auto next = session;
    auto copy = next.tracks[static_cast<std::size_t>(track)].clips[static_cast<std::size_t>(clip)];
    copy.id = next.newId();
    copy.start = copy.end();
    tab.clip = copy.id;
    auto& clips = next.tracks[static_cast<std::size_t>(track)].clips;
    clips.push_back(std::move(copy));
    std::stable_sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
    edit(tab, std::move(next), ui::Ls("Dupliquer le clip", "Duplicate the clip"));
}

void FoundryView::addTrack(Tab& tab, const bool midi)
{
    const auto count=tab.document->session().tracks.size();
    auto next=engine::foundry::withTrack(tab.document->session(),(ui::Ls(midi?"MIDI ":"Audio ",midi?"MIDI ":"Audio ")+juce::String(static_cast<int>(count)+1)).toStdString());
    next.tracks.back().midi=midi;
    edit(tab,std::move(next),ui::Ls("Nouvelle piste","New track"));
    syncProcessing(tab);pickTrack(tab,static_cast<int>(count),false);
}

void FoundryView::addMarker(Tab& tab, const bool region)
{
    auto markers = tab.document->markers();
    FoundryMarker marker;
    if (region && hasSelection(tab)) {
        marker.start = tab.selectionStart;
        marker.end = tab.selectionEnd;
    } else {
        marker.start = marker.end = tab.cursor;
    }
    auto count = 0;
    for (const auto& other : markers)
        if (other.region() == marker.region())
            ++count;
    marker.name = (marker.region() ? ui::Ls("Région ", "Region ") : ui::Ls("Marqueur ", "Marker ")) + juce::String(count + 1);
    markers.push_back(marker);
    std::sort(markers.begin(), markers.end(), [](const FoundryMarker& a, const FoundryMarker& b) { return a.start < b.start; });
    tab.document->setMarkers(std::move(markers), marker.region() ? ui::Ls("Région", "Region") : ui::Ls("Marqueur", "Marker"));
}

// ---- Processes ---------------------------------------------------------------------------------

void FoundryView::applyProcess(Tab& tab)
{
    const auto& session = tab.document->session();
    const auto [start, end] = target(tab);
    const auto set = picked(tab);
    const auto mask = tab.mask;
    const auto name = juce::String::fromUTF8(processName(process_));
    namespace fs = engine::foundry;
    const auto each = [&](const fs::ClipProcess& process) { return fs::processed(session, start, end, set, process); };
    switch (process_) {
    case processGain: {
        const auto db = gainDb_;
        edit(tab, each([mask, db](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t) { return fs::gained(s, a, b, mask, db); }), name);
        break;
    }
    case processNormalise: {
        // Measured over every clip under the selection, then one gain for all.
        double peak = 0.0;
        double squares = 0.0;
        double frames = 0.0;
        for (std::size_t t = 0; t < session.tracks.size(); ++t) {
            if (!fs::inSet(set, t))
                continue;
            for (const auto& clip : session.tracks[t].clips) {
                const auto a = std::max(start, clip.start);
                const auto b = std::min(end, clip.end());
                if (b <= a || clip.sound == nullptr)
                    continue;
                const auto from = clip.offset + (a - clip.start);
                const auto to = clip.offset + (b - clip.start);
                const auto stats = fs::statistics(*clip.sound, from, to);
                for (int c = 0; c < clip.sound->channels(); ++c) {
                    if (!fs::inMask(mask, c))
                        continue;
                    peak = std::max(peak, static_cast<double>(stats.peak[static_cast<std::size_t>(c)]));
                    squares += stats.rms[static_cast<std::size_t>(c)] * stats.rms[static_cast<std::size_t>(c)] * static_cast<double>(to - from);
                    frames += static_cast<double>(to - from);
                }
            }
        }
        const auto level = normaliseRms_ ? (frames > 0.0 ? std::sqrt(squares / frames) : 0.0) : peak;
        if (level <= 1.0e-9) {
            report(ui::Ls("Rien à normaliser : la sélection est silencieuse", "Nothing to normalise: the selection is silent"), true);
            return;
        }
        const auto db = static_cast<float>(normaliseDb_ - 20.0 * std::log10(level));
        edit(tab, each([mask, db](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t) { return fs::gained(s, a, b, mask, db); }), name);
        break;
    }
    case processFadeIn:
    case processFadeOut: {
        // One fade over the whole selection, whichever clips it crosses.
        const auto in = process_ == processFadeIn;
        const auto curve = static_cast<fs::FadeCurve>(fadeCurve_);
        const auto span = static_cast<double>(std::max<std::int64_t>(1, end - start - 1));
        const auto from = start;
        edit(tab,
             each([=](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t timeline) {
                 return fs::processed(s, a, b, mask, [&](std::vector<float>& data, int) {
                     for (std::size_t i = 0; i < data.size(); ++i) {
                         const auto t = static_cast<float>(static_cast<double>(timeline + static_cast<std::int64_t>(i) - from) / span);
                         data[i] *= in ? fs::fadeShape(curve, t) : fs::fadeShape(curve, 1.0f - t);
                     }
                 });
             }),
             name);
        break;
    }
    case processInvert:
        edit(tab, each([mask](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t) { return fs::inverted(s, a, b, mask); }), name);
        break;
    case processReverse:
        edit(tab, each([mask](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t) { return fs::reversed(s, a, b, mask); }), name);
        break;
    case processDc:
        edit(tab, each([mask](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t) { return fs::centred(s, a, b, mask); }), name);
        break;
    case processSilence:
        edit(tab, each([mask](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t) { return fs::silenced(s, a, b, mask); }), name);
        break;
    case processInsertSilence: {
        const auto frames = static_cast<std::int64_t>(std::llround(silenceSeconds_ * session.rate));
        const auto at = tab.cursor;
        edit(tab, fs::withSilence(session, at, frames, set), name, at, 0, frames);
        select(tab, at, at + frames);
        break;
    }
    case processStretch: {
        static constexpr engine::StretchAlgorithm algorithms[4] { engine::StretchAlgorithm::repitch, engine::StretchAlgorithm::tonal,
                                                                  engine::StretchAlgorithm::beats, engine::StretchAlgorithm::texture };
        const auto speed = std::clamp(static_cast<double>(speedPercent_) / 100.0, 0.05, 8.0);
        const auto pitch = std::pow(2.0, static_cast<double>(pitchSemitones_) / 12.0);
        const auto algorithm = algorithms[std::clamp(stretchAlgorithm_, 0, 3)];
        auto next = each([=](const Sound& s, std::int64_t a, std::int64_t b, std::int64_t) { return fs::stretched(s, a, b, speed, pitch, algorithm); });
        const auto length = static_cast<std::int64_t>(std::llround(static_cast<double>(end - start) / speed));
        edit(tab, std::move(next), name, start, end - start, length);
        select(tab, start, start + length);
        break;
    }
    case processRate: {
        if (std::abs(session.rate - newRate_) < 0.5)
            break;
        const auto ratio = newRate_ / session.rate;
        auto markers = tab.document->markers();
        for (auto& marker : markers) {
            marker.start = static_cast<std::int64_t>(std::llround(static_cast<double>(marker.start) * ratio));
            marker.end = static_cast<std::int64_t>(std::llround(static_cast<double>(marker.end) * ratio));
        }
        tab.document->apply(fs::withRate(session, newRate_), std::move(markers), name);
        if (playingTab_ == current_)
            this->player().setSession(tab.document->shared());
        select(tab, 0, 0);
        tab.framesPerPixel = 0.0;
        break;
    }
    case processChannels: {
        const auto action = channelsAction_;
        auto next = fs::processed(session, 0, session.frames() + 1, set, [action](const Sound& s, std::int64_t, std::int64_t, std::int64_t) {
            const auto channels = s.channels();
            std::vector<std::vector<float>> matrix;
            auto count = channels;
            switch (action) {
            case 0:
                count = 1;
                matrix.assign(1, std::vector<float>(static_cast<std::size_t>(channels), 1.0f / static_cast<float>(channels)));
                break;
            case 1:
                count = 2;
                matrix.assign(2, std::vector<float>(static_cast<std::size_t>(channels), 0.0f));
                matrix[0][0] = 1.0f;
                matrix[1][static_cast<std::size_t>(std::min(1, channels - 1))] = 1.0f;
                break;
            case 2:
                if (channels < 2)
                    return s;
                matrix.assign(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(channels), 0.0f));
                for (int c = 0; c < channels; ++c)
                    matrix[static_cast<std::size_t>(c)][static_cast<std::size_t>(c < 2 ? 1 - c : c)] = 1.0f;
                break;
            default:
                count = 1;
                matrix.assign(1, std::vector<float>(static_cast<std::size_t>(channels), 0.0f));
                matrix[0][static_cast<std::size_t>(std::min(action - 3, channels - 1))] = 1.0f;
                break;
            }
            return s.remixed(count, matrix);
        });
        edit(tab, std::move(next), name);
        tab.mask = 0xffu;
        break;
    }
    case processEffects: {
        std::vector<TrackRef> audio;
        for (const auto& track : studio_.tracks())
            if (!track.midi)
                audio.push_back(track);
        audio.push_back(Studio::masterTrack());
        if (effectsTrack_ < 0 || effectsTrack_ >= static_cast<int>(audio.size())) {
            report(ui::Ls("Choisissez une piste", "Pick a track"), true);
            return;
        }
        if (const auto result = applyTrackEffects(tab, audio[static_cast<std::size_t>(effectsTrack_)], tailSeconds_); result.failed()) {
            report(result.getErrorMessage(), true);
            return;
        }
        break;
    }
    default:
        return;
    }
    report(name + ui::Ls(" : fait", ": done"), false);
}

juce::Result FoundryView::applyTrackEffects(Tab& tab, const TrackRef& track, const double tailSeconds)
{
    const auto* slot = studio_.graphFor(track);
    if (slot == nullptr)
        return juce::Result::fail(ui::Ls("Cette piste n'a pas d'effets", "That track has no effects"));
    const auto state = slot->graph().createVst3RuntimeState();
    juce::String error;
    auto native = app::restoreNativeDspGraphState(state.getProperty("native", {}), error);
    if (!native)
        return juce::Result::fail(ui::Ls("Ses effets ne se relisent pas : ", "Its effects do not restore: ") + error);
    app::attachImpulses(*native, state.getProperty("editor", {}));
    const auto graph = *native;
    const auto& session = tab.document->session();
    const auto rate = session.rate;
    {
        app::NativeDspRuntime probe;
        probe.prepare(rate, 1'024, 2);
        if (const auto compiled = probe.setGraph(graph); compiled.failed())
            return juce::Result::fail(ui::Ls("Ses effets ne se compilent pas : ", "Its effects do not compile: ") + compiled.getErrorMessage());
    }
    const auto tail = static_cast<std::int64_t>(std::llround(std::max(0.0, tailSeconds) * rate));
    const auto [start, end] = target(tab);
    // Each clip's part through the chain from its start (its delay taken
    // off); its tail laid over what follows in the clip's own sound.
    auto next = engine::foundry::processed(session, start, end, picked(tab), [&](const Sound& sound, std::int64_t from, std::int64_t to, std::int64_t) {
        constexpr int block = 1'024;
        app::NativeDspRuntime chain;
        chain.prepare(rate, block, 2);
        (void) chain.setGraph(graph);
        const auto latency = std::max(0, chain.latencySamples());
        const auto length = to - from;
        const auto room = std::min<std::int64_t>(tail, std::max<std::int64_t>(0, sound.frames() - to));
        const auto total = length + room + latency;
        std::vector<std::vector<float>> rendered(2, std::vector<float>(static_cast<std::size_t>(length + room)));
        juce::AudioBuffer<float> buffer(2, block);
        for (std::int64_t at = 0; at < total; at += block) {
            const auto n = static_cast<int>(std::min<std::int64_t>(block, total - at));
            for (int c = 0; c < 2; ++c) {
                auto* data = buffer.getWritePointer(c);
                std::fill_n(data, block, 0.0f);
                const auto available = std::clamp<std::int64_t>(length - at, 0, n);
                if (available > 0)
                    sound.read(std::min(c, sound.channels() - 1), from + at, available, data);
            }
            (void) chain.process(buffer, 0, n);
            for (int i = 0; i < n; ++i) {
                const auto out = at + i - latency;
                if (out < 0 || out >= length + room)
                    continue;
                rendered[0][static_cast<std::size_t>(out)] = buffer.getSample(0, i);
                rendered[1][static_cast<std::size_t>(out)] = buffer.getSample(1, i);
            }
        }
        std::vector<std::vector<float>> main(2);
        std::vector<std::vector<float>> after(2);
        for (int c = 0; c < 2; ++c) {
            auto& source = rendered[static_cast<std::size_t>(c)];
            main[static_cast<std::size_t>(c)].assign(source.begin(), source.begin() + length);
            after[static_cast<std::size_t>(c)].assign(source.begin() + length, source.end());
        }
        auto result = sound.replaced(from, to, Sound(std::move(main), rate));
        if (room > 0)
            result = result.mixedIn(to, Sound(std::move(after), rate), 1.0f);
        return result;
    });
    edit(tab, std::move(next), ui::Ls("Effets de ", "Effects of ") + studio_.trackName(track));
    return juce::Result::ok();
}

void FoundryView::sendToStudio(Tab& tab, const bool eachTrack)
{
    syncProcessing(tab);commitProcessing(tab, true);
    const auto& session = tab.document->session();
    const auto folder = locations::folder(Location::foundry);
    (void) folder.createDirectory();
    const auto base = juce::File::createLegalFileName(tab.document->name()).upToLastOccurrenceOf(".", false, false);
    std::vector<std::pair<juce::File, engine::foundry::Sound>> files;
    if (eachTrack) {
        // Each track as it is, whatever is muted or soloed.
        auto heard = session;
        for (auto& track : heard.tracks)
            track.mute = track.solo = false;
        for (std::size_t t = 0; t < session.tracks.size(); ++t) {
            TrackSet one(session.tracks.size(), false);
            one[t] = true;
            const auto name = (base.isNotEmpty() ? base : juce::String("Foundry")) + " - " + fromUtf8(session.tracks[t].name);
            files.emplace_back(folder.getNonexistentChildFile(juce::File::createLegalFileName(name), ".wav", false),
                               [&]{
                                   if(!tab.document->processing().getDynamicObject())return engine::foundry::render(heard,0,heard.frames(),&one);
                                   auto stem=heard;for(std::size_t k=0;k<stem.tracks.size();++k)if(k!=t)stem.tracks[k].clips.clear();
                                   return ModeProcessing::render(stem,tab.document->processing(),0,heard.frames(),tab.document->renderTail());
                               }());
        }
    } else if (tab.document->plainFile() && tab.document->file() != juce::File() && !tab.document->dirty()) {
        if (const auto track = studio_.addAudioTrack(tab.document->file())) {
            state_.selectTrack(*track);
            state_.mode = AppMode::studio;
            report(ui::Ls("Dans le studio : ", "In the studio: ") + tab.document->name(), false);
        }
        return;
    } else {
        files.emplace_back(folder.getNonexistentChildFile(base.isNotEmpty() ? base : juce::String("Foundry"), ".wav", false),
                           tab.document->processing().getDynamicObject()?ModeProcessing::render(session,tab.document->processing(),0,session.frames(),tab.document->renderTail()):engine::foundry::render(session, 0, session.frames()));
    }
    auto added = 0;
    for (const auto& [file, sound] : files) {
        if (const auto result = FoundryDocument::writeSound(sound, file, 24); result.failed()) {
            report(result.getErrorMessage(), true);
            return;
        }
        if (const auto track = studio_.addAudioTrack(file)) {
            state_.selectTrack(*track);
            ++added;
        }
    }
    state_.mode = AppMode::studio;
    report(ui::Ls("Dans le studio : ", "In the studio: ") + juce::String(added) + ui::Ls(" piste(s)", " track(s)"), added == 0);
}

// ---- Playing -----------------------------------------------------------------------------------

void FoundryView::playFrom(Tab& tab)
{
    auto& player = this->player();
    syncProcessing(tab);commitProcessing(tab);
    player.setProcessor(tab.processing);
    player.setSession(tab.document->shared());
    if (hasSelection(tab))
        player.play(tab.selectionStart, tab.selectionEnd, loop_);
    else
        player.play(tab.cursor, 0, false);
    playingTab_ = current_;
}

void FoundryView::stop()
{
    this->player().stop();
    playingTab_ = -1;
}

TransportSync::State FoundryView::transportState() const
{
    TransportSync::State s;
    if(current_<0||current_>=static_cast<int>(tabs_.size())){s.available=false;return s;}
    const auto& t=tabs_[static_cast<std::size_t>(current_)];
    const auto& session=t.document->session();
    s.playing=playingTab_==current_&&this->player().playing();
    s.seconds=(s.playing?this->player().position():static_cast<double>(t.cursor))/session.rate;
    s.bpm=session.tempo;
    return s;
}
void FoundryView::applyTransport(const TransportSync::Update& u)
{
    auto* t=tab();if(!t)return;
    auto& player=this->player();
    auto before=transportState();
    if(u.tempo&&std::abs(t->document->session().tempo-u.state.bpm)>0.01){
        // Incoming clock updates are live mixer changes, not hundreds of undo steps.
        auto next=t->document->session();next.tempo=u.state.bpm;t->document->replace(std::move(next));
        syncProcessing(*t);
        if(before.playing)player.setSession(t->document->shared());
    }
    if(u.position)t->cursor=static_cast<std::int64_t>(std::max(0.0,u.state.seconds)*t->document->session().rate);
    else if(before.playing)t->cursor=static_cast<std::int64_t>(player.position());
    const bool playing=u.play?u.state.playing:before.playing;
    if(!playing){if(before.playing)stop();return;}
    if(u.play||u.position){
        syncProcessing(*t);player.setProcessor(t->processing);player.setSession(t->document->shared());
        player.play(t->cursor,0,false);playingTab_=current_;
    }
}

void FoundryView::togglePlay()
{
    auto* tab = this->tab();
    if (tab == nullptr)
        return;
    if (this->player().playing() && playingTab_ == current_)
        stop();
    else
        playFrom(*tab);
}

bool FoundryView::undoProcessing(const bool redo)
{
    auto* current=tab();return current&&current->processing&&current->processing->undo(redo);
}

bool FoundryView::undo()
{
    auto* tab = this->tab();
    if(tab==nullptr)return false;
    const auto before=tab->document->shared();
    if(!tab->document->undo())return false;
    if(cinema_){syncProcessing(*tab);for(const auto& track:tab->document->session().tracks)if(!track.bus){
        const auto previous=before->trackIndex(track.id);
        if(previous<0||before->tracks[previous].parentId!=track.parentId)tab->processing->routeInput(track.id,track.parentId);
    }}
    if (playingTab_ == current_)
        this->player().setSession(tab->document->shared());
    tab->tracks.resize(tab->document->session().tracks.size(), false);
    tab->selectionChanged = ImGui::GetTime();
    return true;
}

bool FoundryView::redo()
{
    auto* tab = this->tab();
    if(tab==nullptr)return false;
    const auto before=tab->document->shared();
    if(!tab->document->redo())return false;
    if(cinema_){syncProcessing(*tab);for(const auto& track:tab->document->session().tracks)if(!track.bus){
        const auto previous=before->trackIndex(track.id);
        if(previous<0||before->tracks[previous].parentId!=track.parentId)tab->processing->routeInput(track.id,track.parentId);
    }}
    if (playingTab_ == current_)
        this->player().setSession(tab->document->shared());
    tab->tracks.resize(tab->document->session().tracks.size(), false);
    tab->selectionChanged = ImGui::GetTime();
    return true;
}

void FoundryView::measure(Tab& tab, const bool force)
{
    const auto [start, end] = target(tab);
    const auto set = picked(tab);
    if (!force && tab.measuredSession == tab.document->shared() && tab.measuredStart == start && tab.measuredEnd == end
        && tab.measuredTracks == set)
        return;
    const auto rate = tab.document->session().rate;
    if (!force && (ImGui::GetTime() - tab.selectionChanged < 0.3 || static_cast<double>(end - start) / rate > 600.0 || drag_ != Drag::none))
        return;
    // What the tracks picked give, mixed.
    const auto mixed = engine::foundry::render(tab.document->session(), start, end, &set);
    tab.statistics = engine::foundry::statistics(mixed, 0, mixed.frames());
    tab.spectrum = engine::foundry::spectrum(mixed, 0, mixed.frames(), 160);
    tab.measuredSession = tab.document->shared();
    tab.measuredStart = start;
    tab.measuredEnd = end;
    tab.measuredTracks = set;
}

// ---- The window --------------------------------------------------------------------------------

void FoundryView::fit(Tab& tab, const float width)
{
    const auto frames = std::max<std::int64_t>(static_cast<std::int64_t>(tab.document->session().rate * 10.0), tab.document->session().frames());
    tab.framesPerPixel = std::max(1.0e-3, static_cast<double>(frames) / std::max(1.0f, width));
    tab.viewStart = 0.0;
}

void FoundryView::follow(Tab& tab, const float width)
{
    // While it plays, the view keeps up, a page at a time.
    auto& player = this->player();
    if (!player.playing() || playingTab_ != current_ || drag_ != Drag::none)
        return;
    const auto at = player.position();
    const auto span = tab.framesPerPixel * width;
    if (at < tab.viewStart || at > tab.viewStart + span * 0.95)
        tab.viewStart = std::max(0.0, at - span * 0.05);
}

void FoundryView::syncProcessing(Tab& tab)
{
    const auto initial = !tab.processing;
    if(!tab.processing) {
        tab.processing=std::make_shared<ModeProcessing>(&host_);
        tab.processing->restore(tab.document->processing());
        auto* device=studio_.engine().deviceManager().getCurrentAudioDevice();
        tab.processing->prepare(device?device->getCurrentSampleRate():48000,device?device->getCurrentBufferSizeSamples():512);
    }
    if(tab.processingSession!=tab.document->shared().get()) {
        tab.processing->syncFoundry(tab.document->session());
        tab.processingSession=tab.document->shared().get();
        if (initial) tab.processingRevision=tab.processing->revision();
    }
}
void FoundryView::commitProcessing(Tab& tab, const bool force)
{
    if (!tab.processing)
        return;
    auto next = tab.document->session();
    bool mixChanged=tab.processing->copyMixTo(next);
    if(cinema_)for(auto& track:next.tracks)if(track.bus&&!track.virtualGroup){track.virtualGroup=true;mixChanged=true;}
    if (mixChanged) {
        tab.document->replace(std::move(next));
        tab.processingSession = tab.document->shared().get();
        tab.selectionChanged = ImGui::GetTime();
        if (playingTab_ == current_)
            this->player().setSession(tab.document->shared());
    }
    const auto revision = tab.processing->revision();
    if (tab.processingRevision == revision)
        return;
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (tab.observedProcessingRevision != revision) {
        tab.observedProcessingRevision = revision;
        tab.processingChangedAt = now;
    }
    if (!force && (ImGui::IsAnyMouseDown() || now - tab.processingChangedAt < 0.4)) {
        host_.wake(3);
        return;
    }
    tab.document->setProcessing(tab.processing->serialise());
    tab.processingRevision = revision;
}

void FoundryView::drawClipInspector(Tab& tab)
{
    const auto snapshot=tab.document->shared();int track{},index{};
    if(!snapshot->findClip(tab.clip,track,index)) {
        ImGui::TextWrapped("%s",L("Sélectionnez un clip pour régler son début, sa durée, sa source et ses fondus.","Select a clip to edit its start, length, source and fades."));return;
    }
    auto clip=snapshot->tracks[track].clips[index];const bool midi=snapshot->tracks[track].midi;const auto rate=snapshot->rate;
    char name[256]{};fromUtf8(clip.name).copyToUTF8(name,sizeof(name));ImGui::SetNextItemWidth(-1);
    bool changed=ImGui::InputText(L("##clipName","##clipName"),name,sizeof(name));if(changed)clip.name=name;
    ImGui::TextDisabled("%s",midi?"MIDI":"AUDIO");
    if(!midi) {ImGui::TextWrapped("%s",clip.file.c_str());changed=ImGui::SliderFloat(L("Gain","Gain"),&clip.gainDb,-60,24,"%.1f dB")||changed;}
    double start=clip.start/rate,offset=clip.offset/rate,length=clip.length/rate,fadeIn=clip.fadeIn/rate,fadeOut=clip.fadeOut/rate;
    const auto field=[&](const char* label,double& v){ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(-1);return ImGui::InputDouble((std::string("##")+label).c_str(),&v,.01,1,"%.4f s");};
    changed=field(L("Début","Start"),start)||changed;changed=field(L("Décalage source","Source offset"),offset)||changed;changed=field(L("Durée","Length"),length)||changed;
    if(!midi){changed=field(L("Fondu entrant","Fade in"),fadeIn)||changed;changed=field(L("Fondu sortant","Fade out"),fadeOut)||changed;}
    if(changed){clip.start=std::max<std::int64_t>(0,static_cast<std::int64_t>(start*rate));clip.offset=std::max<std::int64_t>(0,static_cast<std::int64_t>(offset*rate));
        if(clip.sound)clip.offset=std::min(clip.offset,std::max<std::int64_t>(0,clip.sound->frames()-1));
        clip.length=std::max<std::int64_t>(1,static_cast<std::int64_t>(length*rate));if(clip.sound)clip.length=std::min(clip.length,clip.sound->frames()-clip.offset);
        clip.fadeIn=std::clamp<std::int64_t>(static_cast<std::int64_t>(fadeIn*rate),0,clip.length);clip.fadeOut=std::clamp<std::int64_t>(static_cast<std::int64_t>(fadeOut*rate),0,clip.length-clip.fadeIn);
        auto next=*snapshot;next.tracks[track].clips[index]=clip;edit(tab,std::move(next),ui::Ls("Réglages du clip","Clip settings"));}
    if(ImGui::Button(L("Inverser le clip (Reverse)","Reverse clip")))reverseClip(tab);
    if(midi){
        if(ImGui::Button(L("Éditer les notes MIDI","Edit MIDI notes")))showMidiEditor(tab);
        if(ImGui::Button(L("Choisir l'instrument…","Choose instrument…")))tab.processing->chooseInstrument(snapshot->tracks[track].id);
        for(const auto& note:clip.notes)if(note.id==tab.note){
            auto n=note;ImGui::SeparatorText(L("Note sélectionnée","Selected note"));
            bool change=ImGui::SliderInt(L("Hauteur","Pitch"),&n.key,0,127);change=ImGui::SliderFloat(L("Vélocité","Velocity"),&n.velocity,.001f,1.f)||change;
            double duration=n.length/rate;change=field(L("Durée de note","Note length"),duration)||change;
            if(change){n.length=std::max<std::int64_t>(1,static_cast<std::int64_t>(duration*rate));auto next=tab.document->session();for(auto& item:next.tracks[track].clips[index].notes)if(item.id==n.id)item=n;edit(tab,std::move(next),ui::Ls("Note MIDI","MIDI note"));}break;
        }
    }
}

void FoundryView::draw()
{
    if(cinema_){drawCinema();return;}
    if (state_.foundryOpen) {
        const auto file = *state_.foundryOpen;
        state_.foundryOpen.reset();
        (void) open(file);
    }
    auto& player = this->player();
    if (playingTab_ >= 0 && !player.playing())
        playingTab_ = -1;
    if (tabs_.empty()) {
        const auto avail = ImGui::GetContentRegionAvail();
        const auto& p = theme::palette();
        ImGui::Dummy(ImVec2(avail.x, avail.y * 0.32f));
        const auto centred = [&](const char* text, const ImU32 colour) {
            ImGui::SetCursorPosX(std::max(0.0f, (avail.x - ImGui::CalcTextSize(text).x) * 0.5f));
            ImGui::TextColored(theme::vec(colour), "%s", text);
        };
        ImGui::PushFont(theme::boldFont(), ImGui::GetStyle().FontSizeBase * 1.6f);
        centred(L("Fonderie", "Foundry"), p.text);
        ImGui::PopFont();
        centred(L("Ouvrez des sons (Ctrl+O) ou glissez-les ici : chacun sur sa piste, pour les couper, les traiter, les mixer.",
                  "Open sounds (Ctrl+O) or drop them here: each on its track, to cut them, process them, mix them."),
                p.textDim);
        ImGui::Spacing();
        const auto width = dp(300.0f);
        ImGui::SetCursorPosX((avail.x - width) * 0.5f);
        if (ui::primaryButton(L("Ouvrir…", "Open…"), ImVec2(width * 0.5f - dp(4.0f), 0.0f)))
            chooseOpen();
        ImGui::SameLine();
        if (ImGui::Button(L("Nouvelle session", "New session"), ImVec2(width * 0.5f - dp(4.0f), 0.0f)))
            newDocument();
        return;
    }
    drawTabs();
    auto* current = tab();
    if (current == nullptr)
        return;
    auto& tab = *current;
    tab.tracks.resize(tab.document->session().tracks.size(), false);
    if (shownTab_ != current_) {
        shownTab_ = current_;
        tab.selectionChanged = ImGui::GetTime();
    }
    syncProcessing(tab);
    const auto bottomVisible = state_.bottomPanelVisible();
    const auto totalHeight=ImGui::GetContentRegionAvail().y;
    const auto toolsHeight=std::clamp(dp(processingHeight_),dp(180.0f),std::max(dp(180.0f),totalHeight*.65f));
    ImGui::BeginChild("##foundryTimeline",ImVec2(0,std::max(dp(170.0f),totalHeight-(bottomVisible ? toolsHeight+dp(7) : ui::bottomPanelRestoreHeight()+ImGui::GetStyle().ItemSpacing.y))),0,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)&&!ImGui::GetIO().WantTextInput)handleKeys(tab);
    drawToolbar(tab);
    const auto avail = ImGui::GetContentRegionAvail();
    const auto sideWidth = std::min(dp(310.0f), avail.x * 0.3f);
    const auto mainWidth = avail.x - sideWidth - dp(6.0f);
    const auto statusHeight = ImGui::GetFrameHeight();
    const auto laneWidth = mainWidth - dp(headerWidthDp) - dp(bandWidthDp);
    ImGui::BeginGroup();
    if (tab.framesPerPixel <= 0.0)
        fit(tab, laneWidth);
    follow(tab, laneWidth);
    drawOverview(tab, ImVec2(mainWidth, dp(36.0f)));
    drawRuler(tab, ImVec2(mainWidth, dp(32.0f)));
    drawTracks(tab, ImVec2(mainWidth, std::max(dp(80.0f), ImGui::GetContentRegionAvail().y - statusHeight - dp(6.0f))));
    drawStatus(tab);
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, dp(6.0f));
    drawSide(tab, ImVec2(sideWidth, avail.y));
    ImGui::EndChild();
    if (bottomVisible) {
        ImGui::InvisibleButton("##foundryToolsSplit", ImVec2(-1, dp(6)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive())
            processingHeight_ = std::clamp(processingHeight_ - ImGui::GetIO().MouseDelta.y, 180.0f, 700.0f);
        const auto bottom = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##foundryProcessing", ImVec2(bottom.x - sideWidth - dp(6), bottom.y), ImGuiChildFlags_Borders);
        tab.processing->draw([&] { drawMidiEditor(tab); }, true, [&] {
            state_.setBottomPanelVisible(false);
            host_.wake(3);
        });
        ImGui::EndChild();
        ImGui::SameLine(0.0f, dp(6));
        drawActions(tab, ImVec2(sideWidth, bottom.y));
    } else {
        if (ui::bottomPanelRestoreBar()) {
            state_.setBottomPanelVisible(true);
            host_.wake(3);
        }
        tab.processing->draw({}, false);
    }
    commitProcessing(tab);
    if (player.playing() || drag_ != Drag::none)
        host_.wake(2);
}

void FoundryView::drawTabs()
{
    if (!ImGui::BeginTabBar("##foundryTabs", ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll
                                                 | ImGuiTabBarFlags_Reorderable))
        return;
    auto close = -1;
    const auto forcing = current_ != barSelected_;
    auto shownIndex = barSelected_;
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const auto& document = *tabs_[static_cast<std::size_t>(i)].document;
        auto open = true;
        const auto label = document.name() + "###foundryTab" + juce::String(static_cast<juce::int64>(reinterpret_cast<juce::pointer_sized_int>(&document)));
        const auto flags = forcing && i == current_ ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem(label.toRawUTF8(), &open, flags | (document.dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0))) {
            shownIndex = i;
            if (!forcing && current_ != i) {
                current_ = i;
                if (playingTab_ >= 0 && playingTab_ != i)
                    stop();
            }
            ImGui::EndTabItem();
        }
        if (!open)
            close = i;
    }
    barSelected_ = shownIndex;
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing))
        newDocument();
    ui::tooltip(L("Nouvelle session", "New session"));
    ImGui::EndTabBar();
    if (close >= 0)
        closeTab(close);
}

void FoundryView::drawToolbar(Tab& tab)
{
    const auto& p = theme::palette();
    auto& document = *tab.document;
    if (ui::iconButton(icon::openFile, L("Ouvrir un son ou une session (Ctrl+O)", "Open a sound or a session (Ctrl+O)")))
        chooseOpen();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::importFile, L("Ajouter des sons, chacun sur une nouvelle piste (au curseur)",
                                           "Add sounds, each on a new track (at the cursor)")))
        chooseImport();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::save, document.plainFile() ? L("Enregistrer le fichier (Ctrl+S) ; Maj : sous un autre nom",
                                                            "Save the file (Ctrl+S); Shift: under another name")
                                                        : L("Enregistrer la session (Ctrl+S) ; Maj : sous un autre nom",
                                                            "Save the session (Ctrl+S); Shift: under another name")))
        save(ImGui::GetIO().KeyShift);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::exportFile, L("Exporter le mixage (la sélection, ou tout) en WAV, FLAC, AIFF ou Ogg",
                                           "Export the mix (the selection, or all of it) as WAV, FLAC, AIFF or Ogg")))
        chooseExport();
    ImGui::SameLine(0.0f, dp(2.0f));
    {
        ImGui::SetNextItemWidth(dp(78.0f));
        const char* names[3] { "16 bits", "24 bits", "32 float" };
        auto index = saveBits_ == 16 ? 0 : saveBits_ == 32 ? 2 : 1;
        if (ImGui::Combo("##bits", &index, names, 3))
            saveBits_ = index == 0 ? 16 : index == 2 ? 32 : 24;
        ui::tooltip(L("La résolution à l'enregistrement et à l'export (WAV ; FLAC et AIFF : 24 bits au plus)",
                      "The resolution when saved and exported (WAV; FLAC and AIFF: 24 bits at most)"));
    }
    ui::toolbarSeparator();
    if (ui::iconButton(icon::undo, (ui::Ls("Annuler (Ctrl+Z) ", "Undo (Ctrl+Z) ") + document.undoLabel()).toRawUTF8()))
        (void) undo();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::redo, (ui::Ls("Rétablir (Ctrl+Y) ", "Redo (Ctrl+Y) ") + document.redoLabel()).toRawUTF8()))
        (void) redo();
    ui::toolbarSeparator();
    if (ui::iconButton(icon::cut, L("Couper (Ctrl+X)", "Cut (Ctrl+X)")))
        cut(tab);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::copy, L("Copier (Ctrl+C)", "Copy (Ctrl+C)")))
        copy(tab);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::paste, L("Coller (Ctrl+V) ; Maj : en mixant (Ctrl+Maj+V)", "Paste (Ctrl+V); Shift: mixed (Ctrl+Shift+V)")))
        paste(tab, ImGui::GetIO().KeyShift);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::trash, L("Supprimer la sélection, ou le clip choisi (Suppr)", "Delete the selection, or the clip picked (Delete)")))
        remove(tab);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::crop, L("Rogner : garder la sélection seule (Ctrl+T)", "Crop: keep the selection only (Ctrl+T)")))
        crop(tab);
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::trim, L("Scinder les clips au curseur (S)", "Split the clips at the cursor (S)")))
        splitAtCursor(tab);
    ui::toolbarSeparator();
    if (ui::iconButton(icon::add, L("Nouvelle piste audio ou MIDI", "New audio or MIDI track")))
        ImGui::OpenPopup("##addFoundryTrack");
    if(ImGui::BeginPopup("##addFoundryTrack")){trackMenu(tab);ImGui::EndPopup();}
    ImGui::SameLine();auto tempo=static_cast<float>(document.session().tempo);ImGui::SetNextItemWidth(dp(90));if(ImGui::DragFloat("BPM",&tempo,.1f,20,400,"%.1f")){auto next=document.session();next.tempo=tempo;edit(tab,std::move(next),"Tempo");}
    ui::toolbarSeparator();
    if (ui::iconButton(icon::zoomIn, L("Zoom sur la sélection", "Zoom to the selection")) && hasSelection(tab)) {
        const auto width = std::max(1.0f, ImGui::GetContentRegionAvail().x - dp(headerWidthDp + 340.0f));
        tab.framesPerPixel = std::max(1.0e-3, static_cast<double>(tab.selectionEnd - tab.selectionStart) / width);
        tab.viewStart = static_cast<double>(tab.selectionStart);
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::fullScreen, L("Tout voir", "See it all")))
        tab.framesPerPixel = 0.0;
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::edit, L("Crayon : redessiner les échantillons d'un clip, de près (zoomez jusqu'aux échantillons)",
                                     "Pencil: redraw a clip's samples, close up (zoom in down to the samples)"),
                       pencil_, p.accent))
        pencil_ = !pencil_;
    ui::toolbarSeparator();
    // The channels the selection takes.
    const auto choose = [&](const char* label, const unsigned mask, const char* tip) {
        auto on = (tab.mask & 0b11u) == mask;
        if (ui::toggle(label, &on, p.accent, ImVec2(dp(34.0f), 0.0f), tip))
            tab.mask = mask == 0b11u ? 0xffu : mask;
        ImGui::SameLine(0.0f, dp(2.0f));
    };
    choose(L("G", "L"), 0b01u, L("La sélection sur la gauche seule", "The selection on the left only"));
    choose(L("D", "R"), 0b10u, L("La sélection sur la droite seule", "The selection on the right only"));
    choose(L("G+D", "L+R"), 0b11u, L("La sélection sur tous les canaux", "The selection on every channel"));
    ImGui::NewLine();
}

void FoundryView::drawOverview(Tab& tab, const ImVec2 size)
{
    const auto& p = theme::palette();
    const auto& session = shown(tab);
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto end = ImVec2(origin.x + size.x, origin.y + size.y);
    ImGui::InvisibleButton("##overview", size);
    list->AddRectFilled(origin, end, p.bg0, dp(2.0f));
    const auto frames = std::max<std::int64_t>(1, session.frames());
    const auto width = static_cast<int>(size.x);
    // Every clip's loudest, per pixel, kept until it changes.
    if (tab.overviewSession.get() != &session || tab.overviewWidth != width || preview_) {
        tab.overviewSession = preview_ ? nullptr : tab.document->shared();
        tab.overviewWidth = width;
        tab.overview.assign(static_cast<std::size_t>(std::max(0, width)), { 0.0f, 0.0f });
        const auto perPixel = static_cast<double>(frames) / std::max(1, width);
        for (const auto& track : session.tracks)
            for (const auto& clip : track.clips) {
                if (clip.sound == nullptr)
                    continue;
                const auto x0 = static_cast<int>(static_cast<double>(clip.start) / perPixel);
                const auto x1 = static_cast<int>(std::ceil(static_cast<double>(clip.end()) / perPixel));
                for (int x = std::max(0, x0); x < std::min(width, x1); ++x) {
                    const auto a = std::max<std::int64_t>(clip.start, static_cast<std::int64_t>(x * perPixel));
                    const auto b = std::min<std::int64_t>(clip.end(), std::max<std::int64_t>(a + 1, static_cast<std::int64_t>((x + 1) * perPixel)));
                    if (b <= a)
                        continue;
                    auto& column = tab.overview[static_cast<std::size_t>(x)];
                    for (int c = 0; c < std::min(2, clip.sound->channels()); ++c) {
                        const auto [low, high] = clip.sound->range(c, clip.offset + (a - clip.start), clip.offset + (b - clip.start));
                        column.first = std::min(column.first, low);
                        column.second = std::max(column.second, high);
                    }
                }
            }
    }
    const auto centre = origin.y + size.y * 0.5f;
    for (int x = 0; x < width && x < static_cast<int>(tab.overview.size()); ++x) {
        const auto [low, high] = tab.overview[static_cast<std::size_t>(x)];
        if (high - low < 1.0e-6f)
            continue;
        list->AddRectFilled(ImVec2(origin.x + x, centre - std::min(1.0f, high) * size.y * 0.46f),
                            ImVec2(origin.x + x + 1.0f, centre - std::max(-1.0f, low) * size.y * 0.46f + 1.0f), theme::alpha(p.audio, 0.75f));
    }
    const auto laneWidth = size.x - dp(headerWidthDp) - dp(bandWidthDp);
    const auto x0 = origin.x + static_cast<float>(tab.viewStart / frames) * size.x;
    const auto x1 = origin.x + static_cast<float>((tab.viewStart + tab.framesPerPixel * laneWidth) / frames) * size.x;
    list->AddRectFilled(ImVec2(x0, origin.y), ImVec2(std::max(x0 + 2.0f, x1), end.y), theme::alpha(p.accent, 0.12f));
    list->AddRect(ImVec2(x0, origin.y), ImVec2(std::max(x0 + 2.0f, x1), end.y), theme::alpha(p.accent, 0.7f));
    if (hasSelection(tab))
        list->AddRectFilled(ImVec2(origin.x + static_cast<float>(tab.selectionStart) / frames * size.x, end.y - dp(4.0f)),
                            ImVec2(origin.x + static_cast<float>(tab.selectionEnd) / frames * size.x, end.y), p.accent);
    if (playingTab_ == current_) {
        const auto x = origin.x + static_cast<float>(this->player().position() / frames) * size.x;
        list->AddLine(ImVec2(x, origin.y), ImVec2(x, end.y), p.play, dp(1.5f));
    }
    if (ImGui::IsItemActive()) {
        const auto span = tab.framesPerPixel * laneWidth;
        const auto at = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f) * static_cast<double>(frames);
        tab.viewStart = std::clamp(at - span * 0.5, 0.0, std::max(0.0, static_cast<double>(frames) - span * 0.5));
    }
    list->AddRect(origin, end, p.line, dp(2.0f));
}

void FoundryView::drawRuler(Tab& tab, const ImVec2 size)
{
    const auto& p = theme::palette();
    const auto rate = tab.document->session().rate;
    const auto origin = ImGui::GetCursorScreenPos();
    const auto left = origin.x + dp(headerWidthDp);
    const auto right = origin.x + size.x - dp(bandWidthDp);
    const auto width = right - left;
    auto position = tab.viewStart / rate;
    auto pixelsPerSecond = rate / tab.framesPerPixel;
    const auto seconds = std::max(1.0, static_cast<double>(tab.document->session().frames()) / rate);
    const auto bandHeight = dp(13.0f);
    if (ui::timelineBand("##foundryBand", ImVec2(left, origin.y), ImVec2(right, origin.y + bandHeight), position, pixelsPerSecond,
                         width / seconds * 0.25, rate * 64.0, 0.01, 1.0, 0.0))
        tab.framesPerPixel = 0.0;
    else {
        tab.viewStart = position * rate;
        tab.framesPerPixel = rate / pixelsPerSecond;
    }
    auto* list = ImGui::GetWindowDrawList();
    const auto top = origin.y + bandHeight;
    const auto bottom = origin.y + size.y;
    list->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), p.bg1);
    static constexpr double steps[] { 0.0001, 0.0002, 0.0005, 0.001, 0.002, 0.005, 0.01, 0.02, 0.05, 0.1, 0.2, 0.5,
                                      1.0,    2.0,    5.0,    10.0,  15.0,  30.0,  60.0, 120.0, 300.0, 600.0 };
    auto step = steps[std::size(steps) - 1];
    for (const auto candidate : steps)
        if (candidate * pixelsPerSecond >= dp(90.0f)) {
            step = candidate;
            break;
        }
    const auto from = std::floor(position / step) * step;
    list->PushClipRect(ImVec2(left, top), ImVec2(right, bottom), true);
    ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 0.78f);
    for (auto t = from; t < position + width / pixelsPerSecond + step; t += step) {
        const auto x = left + static_cast<float>((t - position) * pixelsPerSecond);
        list->AddLine(ImVec2(x, top), ImVec2(x, bottom), theme::alpha(p.textFaint, 0.5f));
        for (int k = 1; k < 5; ++k) {
            const auto minor = x + static_cast<float>(step * pixelsPerSecond * k / 5.0);
            list->AddLine(ImVec2(minor, bottom - dp(4.0f)), ImVec2(minor, bottom), theme::alpha(p.textFaint, 0.35f));
        }
        const auto minutes = static_cast<int>(t / 60.0);
        char text[32];
        if (step < 0.01)
            std::snprintf(text, sizeof(text), "%d:%07.4f", minutes, t - minutes * 60.0);
        else if (step < 1.0)
            std::snprintf(text, sizeof(text), "%d:%05.2f", minutes, t - minutes * 60.0);
        else
            std::snprintf(text, sizeof(text), "%d:%02d", minutes, static_cast<int>(std::round(t - minutes * 60.0)));
        list->AddText(ImVec2(x + dp(3.0f), top + dp(1.0f)), p.textDim, text);
    }
    ImGui::PopFont();
    for (const auto& marker : tab.document->markers()) {
        const auto x = left + static_cast<float>((marker.start / rate - position) * pixelsPerSecond);
        if (marker.region()) {
            const auto x1 = left + static_cast<float>((marker.end / rate - position) * pixelsPerSecond);
            list->AddRectFilled(ImVec2(x, bottom - dp(5.0f)), ImVec2(x1, bottom), theme::alpha(p.modulation, 0.7f));
        }
        list->AddTriangleFilled(ImVec2(x - dp(5.0f), top), ImVec2(x + dp(5.0f), top), ImVec2(x, top + dp(7.0f)), p.warn);
    }
    list->PopClipRect();
    // A click in the times: the cursor there.
    ImGui::SetCursorScreenPos(ImVec2(left, top));
    ImGui::InvisibleButton("##times", ImVec2(std::max(1.0f, width), bottom - top));
    if (ImGui::IsItemActive()) {
        tab.cursor = std::max<std::int64_t>(0, static_cast<std::int64_t>(tab.viewStart + (ImGui::GetIO().MousePos.x - left) * tab.framesPerPixel));
        tab.selectionStart = tab.selectionEnd = tab.cursor;
    }
    if (ImGui::IsItemDeactivated() && playingTab_ == current_)
        playFrom(tab);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom));
    ImGui::Dummy(ImVec2(size.x, 0.0f));
}

FoundryView::Hit FoundryView::hitAt(const Tab& tab, const ImVec2 mouse) const
{
    Hit hit;
    if (mouse.x < lanesMin_.x || mouse.x >= lanesMax_.x || mouse.y < lanesMin_.y || mouse.y >= lanesMax_.y)
        return hit;
    const auto& session = shown(tab);
    const auto rowHeight = dp(rowHeightDp) * tab.laneZoom;
    const auto row = static_cast<int>((mouse.y - lanesMin_.y + tab.laneScroll) / rowHeight);
    hit.track = row;
    hit.part = Drag::select;
    if (row < 0 || row >= static_cast<int>(session.tracks.size()))
        return hit;
    const auto rowTop = lanesMin_.y + row * rowHeight - tab.laneScroll;
    if(tab.automationRows.contains(session.tracks[row].id) && mouse.y>=rowTop+rowHeight*.42f)return hit;
    const auto xOf = [&](const double frame) { return lanesMin_.x + static_cast<float>((frame - tab.viewStart) / tab.framesPerPixel); };
    const auto& clips = session.tracks[static_cast<std::size_t>(row)].clips;
    const auto header = rowTop + dp(2.0f) + dp(clipHeaderDp);
    for (int c = static_cast<int>(clips.size()) - 1; c >= 0; --c) {
        const auto& clip = clips[static_cast<std::size_t>(c)];
        const auto x0 = xOf(static_cast<double>(clip.start));
        const auto x1 = xOf(static_cast<double>(clip.end()));
        if (mouse.x < x0 - dp(5.0f) || mouse.x > x1 + dp(5.0f))
            continue;
        hit.clip = c;
        const auto fadeInX = xOf(static_cast<double>(clip.start + clip.fadeIn));
        const auto fadeOutX = xOf(static_cast<double>(clip.end() - clip.fadeOut));
        if (mouse.y < header && std::abs(mouse.x - fadeInX) <= dp(6.0f) && x1 - x0 > dp(24.0f)) {
            hit.part = Drag::fadeIn;
        } else if (mouse.y < header && std::abs(mouse.x - fadeOutX) <= dp(6.0f) && x1 - x0 > dp(24.0f)) {
            hit.part = Drag::fadeOut;
        } else if (std::abs(mouse.x - x0) <= dp(5.0f) && mouse.y >= header) {
            hit.part = Drag::trimLeft;
        } else if (std::abs(mouse.x - x1) <= dp(5.0f) && mouse.y >= header) {
            hit.part = Drag::trimRight;
        } else if (mouse.x >= x0 && mouse.x <= x1) {
            hit.part = mouse.y < header ? Drag::move : Drag::select;
        } else {
            hit.clip = -1;
            continue;
        }
        return hit;
    }
    return hit;
}

void FoundryView::drawTrackHeader(Tab& tab, const int index, const ImVec2 origin, const ImVec2 size)
{
    const auto& p = theme::palette();
    const auto retained=tab.document->shared();
    const auto& session = *retained;
    const auto& track = session.tracks[static_cast<std::size_t>(index)];
    auto* list = ImGui::GetWindowDrawList();
    const auto colour = theme::trackColour(index);
    const auto chosen = index < static_cast<int>(tab.tracks.size()) && tab.tracks[static_cast<std::size_t>(index)];
    const auto end = ImVec2(origin.x + size.x, origin.y + size.y);
    list->AddRectFilled(origin, end, chosen ? theme::mix(p.bg2, colour, 0.18f) : p.bg1);
    list->AddRectFilled(origin, ImVec2(origin.x + dp(4.0f), end.y), colour);
    ImGui::PushID(static_cast<int>(track.id));
    // A click on it picks the track (Ctrl: one more); a double click renames it.
    ImGui::SetCursorScreenPos(origin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##header", size);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        pickTrack(tab, index, ImGui::GetIO().KeyCtrl);
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        ImGui::OpenPopup("##rename");
    if (ImGui::BeginPopup("##rename")) {
        std::array<char, 96> name {};
        fromUtf8(track.name).copyToUTF8(name.data(), name.size());
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##name", name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            auto next = session;
            next.tracks[static_cast<std::size_t>(index)].name = name.data();
            edit(tab, std::move(next), ui::Ls("Renommer la piste", "Rename the track"));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    // The name has the whole width when the row under it holds the
    // automation and remove buttons; a short track keeps them beside it.
    const auto x = origin.x + dp(10.0f);
    const auto row = origin.y + dp(4.0f) + ImGui::GetFontSize() + dp(6.0f);
    const auto roomy = row + ImGui::GetFrameHeight() <= end.y;
    const auto buttonsY = roomy ? row + (ImGui::GetFrameHeight() - dp(18.0f)) * 0.5f : origin.y + dp(2.0f);
    ImGui::SetCursorScreenPos(ImVec2(end.x - dp(48.0f), buttonsY));
    bool automation=tab.automationRows.contains(track.id);
    if(ui::toggle("A",&automation,p.accent,ImVec2(dp(20),dp(18)),L("Afficher l'automation","Show automation"))) {
        if(automation)tab.automationRows.insert(track.id);else tab.automationRows.erase(track.id);
    }
    const auto nameWidth = size.x - (roomy ? dp(16.0f) : dp(60.0f));
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ui::clippedText(list, ImVec2(x, origin.y + dp(4.0f)), nameWidth, p.text, track.name.c_str());
    const auto nameCut = ImGui::CalcTextSize(track.name.c_str()).x > nameWidth;
    ImGui::PopFont();
    // A name cut short is read whole under the pointer.
    if (nameCut && ImGui::IsMouseHoveringRect(ImVec2(x, origin.y), ImVec2(x + nameWidth, row)) && !ImGui::IsAnyItemActive())
        ImGui::SetTooltip("%s", track.name.c_str());
    ImGui::SetCursorScreenPos(ImVec2(end.x - dp(24.0f), buttonsY));
    if (ui::iconButton(icon::close, L("Retirer la piste", "Remove the track"), false, 0, ImVec2(dp(20.0f), dp(18.0f)))) {
        edit(tab, engine::foundry::withoutTrack(session, index), ui::Ls("Retirer la piste", "Remove the track"));
        ImGui::PopID();
        return;
    }
    if (!roomy) {
        ImGui::PopID();
        return;
    }
    // Mute, solo, gain, pan: as on a mixer, no step to undo.
    const auto change = [&](auto&& apply) {
        auto next = tab.document->session();
        apply(next.tracks[static_cast<std::size_t>(index)]);
        tab.document->replace(std::move(next));
        if (playingTab_ == current_)
            this->player().setSession(tab.document->shared());
    };
    auto mute = track.mute;
    auto solo = track.solo;
    ImGui::SetCursorScreenPos(ImVec2(x, row));
    if (ui::toggle("M", &mute, p.mute, ImVec2(dp(24.0f), 0.0f), L("Muet", "Mute")))
        change([mute](Track& t) { t.mute = mute; });
    ImGui::SameLine(0.0f, dp(3.0f));
    if (ui::toggle("S", &solo, p.solo, ImVec2(dp(24.0f), 0.0f), L("Solo", "Solo")))
        change([solo](Track& t) { t.solo = solo; });
    ImGui::SameLine(0.0f, dp(6.0f));
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textFaint), "%s",track.bus?"BUS":track.midi?"MIDI":"AUDIO");
    // Gain and pan: two knobs, each value beside its knob, never under a
    // handle.
    constexpr auto knobDiameter = 24.0f;
    const auto knobs = row + ImGui::GetFrameHeight() + dp(6.0f);
    if (knobs + dp(knobDiameter) > end.y) {
        ImGui::PopID();
        return;
    }
    auto gain = tab.document->session().tracks[static_cast<std::size_t>(index)].gainDb;
    auto pan = tab.document->session().tracks[static_cast<std::size_t>(index)].pan;
    ui::KnobStyle style;
    style.diameter = knobDiameter;
    style.colour = colour;
    const auto knobWidth = dp(knobDiameter + 12.0f);
    const auto half = (size.x - dp(14.0f)) * 0.5f;
    const auto valueBeside = [&](const float left, const juce::String& text) {
        const auto height = ImGui::GetFontSize();
        list->AddText(ImVec2(left + knobWidth + dp(1.0f), knobs + (dp(knobDiameter) - height) * 0.5f), p.textDim, text.toRawUTF8());
    };
    style.flags = ui::knobNoLabel;
    ImGui::SetCursorScreenPos(ImVec2(x - dp(6.0f), knobs));
    auto changed = ui::knob("##gain", &gain, -48.0f, 12.0f, 0.0f, style);
    ui::tooltip(L("Volume de la piste (double-clic : 0 dB)", "The track's gain (double click: 0 dB)"));
    valueBeside(x - dp(6.0f), juce::String(gain, 1) + " dB");
    style.flags = ui::knobNoLabel | ui::knobBipolar;
    ImGui::SetCursorScreenPos(ImVec2(x - dp(6.0f) + half, knobs));
    changed = ui::knob("##pan", &pan, -1.0f, 1.0f, 0.0f, style) || changed;
    ui::tooltip(L("Panoramique (double-clic : au centre)", "Pan (double click: centred)"));
    const auto percent = juce::roundToInt(std::abs(pan) * 100.0f);
    valueBeside(x - dp(6.0f) + half, percent == 0 ? juce::String("C")
                                     : juce::String(pan < 0.0f ? L("G ", "L ") : L("D ", "R ")) + juce::String(percent));
    if (changed)
        change([gain, pan](Track& t) {
            t.gainDb = gain;
            t.pan = pan;
        });
    ImGui::PopID();
}

void FoundryView::drawClip(const Tab& tab, const Clip& clip, const int track, const ImVec2 rowMin, const ImVec2 rowMax, const float left,
                           const float right, const ImU32 colour)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto xOf = [&](const double frame) { return left + static_cast<float>((frame - tab.viewStart) / tab.framesPerPixel); };
    const auto x0 = xOf(static_cast<double>(clip.start));
    const auto x1 = xOf(static_cast<double>(clip.end()));
    if (x1 < left || x0 > right)
        return;
    const auto top = rowMin.y + dp(2.0f);
    const auto bottom = rowMax.y - dp(2.0f);
    const auto header = top + dp(clipHeaderDp);
    const auto picked = clip.id == tab.clip;
    const auto boxMin = ImVec2(std::max(x0, left - dp(2.0f)), top);
    const auto boxMax = ImVec2(std::min(x1, right + dp(2.0f)), bottom);
    list->AddRectFilled(boxMin, boxMax, theme::alpha(colour, 0.13f), dp(3.0f));
    list->AddRectFilled(boxMin, ImVec2(boxMax.x, header), theme::alpha(colour, picked ? 0.95f : 0.6f), dp(3.0f), ImDrawFlags_RoundCornersTop);
    list->AddRect(boxMin, boxMax, picked ? p.text : theme::alpha(colour, 0.8f), dp(3.0f), picked ? dp(1.6f) : dp(1.0f));
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 0.82f);
    ui::clippedText(list, ImVec2(std::max(x0, left) + dp(4.0f), top + dp(1.0f)), boxMax.x - std::max(x0, left) - dp(8.0f),
                    IM_COL32(20, 20, 20, 255), clip.name.c_str());
    ImGui::PopFont();
    if(!clip.sound) {
        const auto height=std::max(dp(10),bottom-header-dp(5));
        for(const auto& note:clip.notes) {
            const auto a=std::max(clip.offset,note.start),b=std::min(clip.offset+clip.length,note.start+note.length);
            if(b<=a)continue;
            const auto y=header+dp(2)+(1.f-note.key/127.f)*height;
            list->AddRectFilled(ImVec2(std::max(left,xOf(clip.start+a-clip.offset)),y),ImVec2(std::min(right,xOf(clip.start+b-clip.offset)),y+dp(3)),p.text);
        }
        return;
    }
    if(bottom<=header+dp(4))return;
    // Its sound, its gain and fades shown on it.
    const auto& sound = *clip.sound;
    const auto channels = std::min(2, std::max(1, sound.channels()));
    const auto laneHeight = (bottom - header) / static_cast<float>(channels);
    const auto wave = theme::brighter(colour, 0.25f);
    const auto from = std::max(left, x0);
    const auto to = std::min(right, x1);
    const auto frameAt = [&](const float x) { return tab.viewStart + static_cast<double>(x - left) * tab.framesPerPixel; };
    const auto anyPicked = std::any_of(tab.tracks.begin(), tab.tracks.end(), [](const bool on) { return on; });
    for (int c = 0; c < channels; ++c) {
        const auto centre = header + laneHeight * (static_cast<float>(c) + 0.5f);
        const auto half = laneHeight * 0.46f * tab.amplitude;
        const auto laneTop = header + laneHeight * static_cast<float>(c);
        const auto laneBottom = laneTop + laneHeight;
        list->AddLine(ImVec2(from, centre), ImVec2(to, centre), theme::alpha(p.textFaint, 0.25f));
        const auto selected = tab.selectionEnd > tab.selectionStart && engine::foundry::inMask(tab.mask, c)
                              && (!anyPicked || (track < static_cast<int>(tab.tracks.size()) && tab.tracks[static_cast<std::size_t>(track)]));
        if (selected) {
            const auto s0 = std::max(from, xOf(static_cast<double>(tab.selectionStart)));
            const auto s1 = std::min(to, xOf(static_cast<double>(tab.selectionEnd)));
            if (s1 > s0)
                list->AddRectFilled(ImVec2(s0, laneTop), ImVec2(s1, laneBottom), theme::alpha(p.selection, 0.6f));
        }
        if (tab.framesPerPixel >= 1.0) {
            for (auto x = from; x < to; x += 1.0f) {
                const auto f0 = static_cast<std::int64_t>(std::floor(frameAt(x))) - clip.start;
                const auto f1 = std::max(f0 + 1, static_cast<std::int64_t>(std::floor(frameAt(x + 1.0f))) - clip.start);
                const auto a = std::clamp<std::int64_t>(f0, 0, clip.length);
                const auto b = std::clamp<std::int64_t>(f1, 0, clip.length);
                if (b <= a)
                    continue;
                const auto [low, high] = sound.range(c, clip.offset + a, clip.offset + b);
                const auto gain = engine::foundry::clipGain(clip, (a + b) / 2);
                const auto y0 = std::max(laneTop, centre - high * gain * half);
                const auto y1 = std::min(laneBottom, centre - low * gain * half + 1.0f);
                list->AddRectFilled(ImVec2(x, y0), ImVec2(x + 1.0f, std::max(y0 + 1.0f, y1)), wave);
            }
        } else {
            const auto first = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor(frameAt(from))) - clip.start - 1);
            const auto last = std::min<std::int64_t>(clip.length, static_cast<std::int64_t>(std::ceil(frameAt(to))) - clip.start + 2);
            if (last > first) {
                std::vector<float> values(static_cast<std::size_t>(last - first));
                sound.read(c, clip.offset + first, last - first, values.data());
                std::vector<ImVec2> points;
                points.reserve(values.size());
                for (std::size_t i = 0; i < values.size(); ++i) {
                    const auto frame = first + static_cast<std::int64_t>(i);
                    points.emplace_back(xOf(static_cast<double>(clip.start + frame)),
                                        std::clamp(centre - values[i] * engine::foundry::clipGain(clip, frame) * half, laneTop, laneBottom));
                }
                list->AddPolyline(points.data(), static_cast<int>(points.size()), wave, dp(1.2f));
                if (1.0 / tab.framesPerPixel > 6.0)
                    for (const auto& point : points)
                        list->AddCircleFilled(point, dp(2.2f), wave);
            }
        }
    }
    // The fades: their lines and their handles.
    if (x1 - x0 > dp(24.0f)) {
        const auto fadeInX = xOf(static_cast<double>(clip.start + clip.fadeIn));
        const auto fadeOutX = xOf(static_cast<double>(clip.end() - clip.fadeOut));
        if (clip.fadeIn > 0)
            list->AddLine(ImVec2(x0, bottom), ImVec2(fadeInX, header), theme::alpha(p.text, 0.7f), dp(1.2f));
        if (clip.fadeOut > 0)
            list->AddLine(ImVec2(fadeOutX, header), ImVec2(x1, bottom), theme::alpha(p.text, 0.7f), dp(1.2f));
        for (const auto handle : { fadeInX, fadeOutX })
            list->AddRectFilled(ImVec2(handle - dp(3.5f), top + dp(4.0f)), ImVec2(handle + dp(3.5f), top + dp(11.0f)), p.text);
    }
}

void FoundryView::clipMenu(Tab& tab)
{
    int track = -1;
    int index = -1;
    const auto retained=tab.document->shared();
    const auto& session = *retained;
    if (!session.findClip(tab.clip, track, index)) {
        ImGui::CloseCurrentPopup();
        return;
    }
    const auto clip = session.tracks[static_cast<std::size_t>(track)].clips[static_cast<std::size_t>(index)];
    ImGui::TextDisabled("%s", clip.name.c_str());
    if (ImGui::MenuItem(L("Sélectionner le clip", "Select the clip"))) {
        pickTrack(tab, track, false);
        select(tab, clip.start, clip.end());
    }
    if(ImGui::MenuItem(L("Détails du clip", "Clip details")))tab.detailsFocus=true;
    if(ImGui::MenuItem(L("Inverser le clip (Reverse)","Reverse clip"))){reverseClip(tab);return;}
    if(session.tracks[track].midi && ImGui::MenuItem(L("Éditer les notes MIDI","Edit MIDI notes")))showMidiEditor(tab);
    if (ImGui::MenuItem(L("Scinder au curseur", "Split at the cursor"), "S", false, tab.cursor > clip.start && tab.cursor < clip.end())) {
        TrackSet one(session.tracks.size(), false);
        one[static_cast<std::size_t>(track)] = true;
        edit(tab, engine::foundry::split(session, tab.cursor, one), ui::Ls("Scinder", "Split"));
        return;
    }
    if (ImGui::MenuItem(L("Dupliquer", "Duplicate"), "Ctrl+D")) {
        duplicateClip(tab);
        return;
    }
    {
        auto gain = clip.gainDb;
        ImGui::SetNextItemWidth(dp(160.0f));
        ImGui::SliderFloat(L("Gain du clip", "Clip gain"), &gain, -48.0f, 12.0f, "%.1f dB");
        if (gain != clip.gainDb) {
            auto next = session;
            next.tracks[static_cast<std::size_t>(track)].clips[static_cast<std::size_t>(index)].gainDb = gain;
            tab.document->replace(std::move(next));
            if (playingTab_ == current_)
                this->player().setSession(tab.document->shared());
        }
    }
    if (ImGui::MenuItem(L("Retirer les fondus", "Remove the fades"), nullptr, false, clip.fadeIn > 0 || clip.fadeOut > 0)) {
        auto next = session;
        auto& changed = next.tracks[static_cast<std::size_t>(track)].clips[static_cast<std::size_t>(index)];
        changed.fadeIn = changed.fadeOut = 0;
        edit(tab, std::move(next), ui::Ls("Retirer les fondus", "Remove the fades"));
        return;
    }
    ImGui::Separator();
    if (ImGui::MenuItem(L("Supprimer le clip", "Delete the clip"), "Suppr")) {
        auto next = session;
        auto& clips = next.tracks[static_cast<std::size_t>(track)].clips;
        clips.erase(clips.begin() + index);
        edit(tab, std::move(next), ui::Ls("Supprimer le clip", "Delete the clip"));
        tab.clip = 0;
    }
}

void FoundryView::drawTracks(Tab& tab, const ImVec2 size)
{
    const auto& p = theme::palette();
    auto* list = ImGui::GetWindowDrawList();
    const auto origin = ImGui::GetCursorScreenPos();
    const auto headerWidth = dp(headerWidthDp);
    const auto left = origin.x + headerWidth;
    const auto right = origin.x + size.x - dp(bandWidthDp);
    const auto bottom = origin.y + size.y;
    lanesMin_ = ImVec2(left, origin.y);
    const auto masterHeight=std::min(dp(100),size.y*.3f);
    const auto masterTop=bottom-masterHeight;
    const auto trackBottom=masterTop-dp(3);
    lanesMax_ = ImVec2(right, trackBottom);
    // The rows' band: their height (toward them: taller), up and down.
    {
        auto zoom = tab.laneZoom;
        auto scroll = tab.laneScroll;
        if (ui::rowsBand("##rows", ImVec2(right, origin.y), ImVec2(origin.x + size.x, bottom), scroll, zoom, 0.4f, 4.0f))
            zoom = 1.0f;
        tab.laneZoom = zoom;
        tab.laneScroll = scroll;
    }
    const auto retained=tab.document->shared();
    const auto& session = preview_?*preview_:*retained;
    const auto count = static_cast<int>(session.tracks.size());
    const auto rowHeight = dp(rowHeightDp) * tab.laneZoom;
    const auto total = rowHeight * count + dp(addRowDp);
    tab.laneScroll = std::clamp(tab.laneScroll, 0.0f, std::max(0.0f, total - (trackBottom-origin.y)));
    const auto xOf = [&](const double frame) { return left + static_cast<float>((frame - tab.viewStart) / tab.framesPerPixel); };
    const auto frameAt = [&](const float x) { return tab.viewStart + static_cast<double>(x - left) * tab.framesPerPixel; };
    const auto anyPicked = std::any_of(tab.tracks.begin(), tab.tracks.end(), [](const bool on) { return on; });

    ImGui::PushClipRect(origin, ImVec2(right, trackBottom), true);
    list->AddRectFilled(ImVec2(left, origin.y), ImVec2(right, bottom), p.bg0);
    for (int t = 0; t < count; ++t) {
        const auto rowTop = origin.y + rowHeight * t - tab.laneScroll;
        const auto rowBottom = rowTop + rowHeight;
        if (rowBottom < origin.y || rowTop > bottom)
            continue;
        const auto colour = theme::trackColour(t);
        list->AddLine(ImVec2(left, rowBottom - 0.5f), ImVec2(right, rowBottom - 0.5f), p.line);
        const auto chosen = t < static_cast<int>(tab.tracks.size()) && tab.tracks[static_cast<std::size_t>(t)];
        if (chosen)
            list->AddRectFilled(ImVec2(left, rowTop), ImVec2(right, rowBottom), theme::alpha(colour, 0.05f));
        // The selection over the tracks picked (where there is no clip, too).
        if (hasSelection(tab) && (chosen || !anyPicked)) {
            const auto s0 = std::max(left, xOf(static_cast<double>(tab.selectionStart)));
            const auto s1 = std::min(right, xOf(static_cast<double>(tab.selectionEnd)));
            if (s1 > s0)
                list->AddRectFilled(ImVec2(s0, rowTop), ImVec2(s1, rowBottom), theme::alpha(p.selection, 0.2f));
        }
        for (const auto& clip : session.tracks[static_cast<std::size_t>(t)].clips)
            drawClip(tab, clip, t, ImVec2(left, rowTop), ImVec2(right, rowBottom-(tab.automationRows.contains(session.tracks[t].id)?rowHeight*.58f:0)), left, right, colour);
    }
    // A row to add a track, or to drop a sound on.
    {
        const auto rowTop = origin.y + rowHeight * count - tab.laneScroll;
        if (rowTop < bottom) {
            list->AddRectFilled(ImVec2(origin.x, rowTop), ImVec2(right, rowTop + dp(addRowDp)), p.bg1);
            ui::centredText(list, ImVec2(left, rowTop), ImVec2(right, rowTop + dp(addRowDp)), p.textFaint,
                            L("Glissez un son ici pour une nouvelle piste", "Drop a sound here for a new track"));
            ImGui::SetCursorScreenPos(ImVec2(origin.x + dp(8.0f), rowTop + (dp(addRowDp) - ImGui::GetFrameHeight()) * 0.5f));
            if (ImGui::Button(L("+ Piste", "+ Track"), ImVec2(headerWidth - dp(16.0f), 0.0f)))ImGui::OpenPopup("##addTrackRow");
            if(ImGui::BeginPopup("##addTrackRow")){trackMenu(tab);ImGui::EndPopup();}
        }
    }
    // Markers, the cursor, the playhead.
    for (const auto& marker : tab.document->markers()) {
        const auto x = xOf(static_cast<double>(marker.start));
        if (x < left || x > right)
            continue;
        list->AddLine(ImVec2(x, origin.y), ImVec2(x, bottom), theme::alpha(p.warn, 0.7f), dp(1.0f));
        list->AddText(ImVec2(x + dp(3.0f), origin.y + dp(1.0f)), p.warn, marker.name.toRawUTF8());
    }
    const auto cursorX = xOf(static_cast<double>(tab.cursor));
    if (cursorX >= left && cursorX <= right)
        list->AddLine(ImVec2(cursorX, origin.y), ImVec2(cursorX, bottom), p.text, dp(1.0f));
    if (playingTab_ == current_) {
        const auto x = xOf(this->player().position());
        if (x >= left && x <= right)
            list->AddLine(ImVec2(x, origin.y), ImVec2(x, bottom), p.play, dp(1.6f));
    }
    if (count == 0 || session.frames() == 0)
        ui::centredText(list, ImVec2(left, origin.y), ImVec2(right, std::min(bottom, origin.y + rowHeight)), p.textFaint,
                        L("Glissez des sons sur les pistes, ou ajoutez-en", "Drop sounds on the tracks, or add some"));
    // The headers, over their rows.
    for (int t = 0; t < count; ++t) {
        const auto rowTop = origin.y + rowHeight * t - tab.laneScroll;
        if (rowTop + rowHeight < origin.y || rowTop > bottom)
            continue;
        drawTrackHeader(tab, t, ImVec2(origin.x, rowTop), ImVec2(headerWidth - dp(2.0f), rowHeight - dp(1.0f)));
        if (static_cast<int>(tab.document->session().tracks.size()) != count)
            break;
    }
    ImGui::PopClipRect();
    if(tab.document->session().tracks.size()!=static_cast<std::size_t>(count)){ImGui::SetCursorScreenPos(ImVec2(origin.x,bottom+dp(4)));return;}
    // The master is a timeline lane, as well as a processing target.
    if(masterTop<bottom && bottom>origin.y) {
        list->AddRectFilled(ImVec2(origin.x,masterTop),ImVec2(left,bottom),tab.masterSelected?p.bg3:p.bg1);
        ImGui::SetCursorScreenPos(ImVec2(origin.x+dp(8),masterTop+dp(8)));
        if(ImGui::Button("MASTER",ImVec2(headerWidth-dp(16),0))) {
            tab.masterSelected=true;tab.clip=0;std::fill(tab.tracks.begin(),tab.tracks.end(),false);tab.processing->selectMaster();
        }
        ImGui::TextDisabled("%s",L("Effets et automation","Effects and automation"));
    }

    // The lanes' gestures: a click places the cursor and picks the track, a
    // drag selects over the tracks it crosses; a clip's header moves it (to
    // another track too), its edges trim it, its corners' handles fade it;
    // the pencil redraws close up; the wheel zooms (Shift: scrolls; Ctrl:
    // the waveform's height).
    ImGui::SetCursorScreenPos(ImVec2(left, origin.y));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##lanes", ImVec2(std::max(1.0f, right - left), size.y));
    const auto mouseRow=static_cast<int>((ImGui::GetIO().MousePos.y-origin.y+tab.laneScroll)/rowHeight);
    const auto withinRow=ImGui::GetIO().MousePos.y-origin.y+tab.laneScroll-mouseRow*rowHeight;
    const bool overAutomation=ImGui::GetIO().MousePos.y>=masterTop || (mouseRow>=0 && mouseRow<count && tab.automationRows.contains(session.tracks[mouseRow].id) && withinRow>=rowHeight*.42f);
    const auto hovered = ImGui::IsItemHovered() && !overAutomation;
    auto& io = ImGui::GetIO();
    const auto real = tab.document->shared();
    const auto mouseFrame = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::llround(frameAt(io.MousePos.x))));
    const auto rowAt = [&](const float y) { return static_cast<int>(std::floor((y - origin.y + tab.laneScroll) / rowHeight)); };
    const auto realCount = static_cast<int>(real->tracks.size());
    if (hovered && drag_ == Drag::none) {
        const auto hit = hitAt(tab, io.MousePos);
        if (hit.part == Drag::trimLeft || hit.part == Drag::trimRight || hit.part == Drag::fadeIn || hit.part == Drag::fadeOut)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        else if (hit.part == Drag::move)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (ImGui::IsItemActivated() && !overAutomation) {
        const auto hit = hitAt(tab, io.MousePos);
        preview_.reset();
        const auto onClip = hit.clip >= 0 && hit.track >= 0 && hit.track < realCount;
        if (onClip && hit.part != Drag::select) {
            const auto& clip = real->tracks[static_cast<std::size_t>(hit.track)].clips[static_cast<std::size_t>(hit.clip)];
            drag_ = hit.part;
            dragClip_ = clip.id;
            dragOriginal_ = clip;
            dragFrom_ = mouseFrame;
            dragTrack_ = hit.track;
            tab.clip = clip.id;
            pickTrack(tab, hit.track, false);
        } else if (onClip && pencil_ && tab.framesPerPixel < 2.0 && real->tracks[hit.track].clips[hit.clip].sound) {
            const auto& clip = real->tracks[static_cast<std::size_t>(hit.track)].clips[static_cast<std::size_t>(hit.clip)];
            drag_ = Drag::pencil;
            dragClip_ = clip.id;
            dragTrack_ = hit.track;
            const auto rowTop = origin.y + rowHeight * hit.track - tab.laneScroll + dp(2.0f) + dp(clipHeaderDp);
            const auto channels = std::min(2, std::max(1, clip.sound->channels()));
            const auto laneHeight = (rowHeight - dp(4.0f) - dp(clipHeaderDp)) / static_cast<float>(channels);
            dragLane_ = std::clamp(static_cast<int>((io.MousePos.y - rowTop) / laneHeight), 0, channels - 1);
            lastPencil_ = -1;
            preview_ = *real;
        } else {
            drag_ = Drag::select;
            dragTrack_ = std::clamp(rowAt(io.MousePos.y), 0, std::max(0, realCount - 1));
            // In the top of a track's row: its left; in the bottom: its right.
            const auto within = (io.MousePos.y - (origin.y + rowHeight * dragTrack_ - tab.laneScroll)) / rowHeight;
            tab.mask = within < 0.22f ? 0b01u : within > 0.82f ? 0b10u : 0xffu;
            if (io.KeyShift && hasSelection(tab))
                dragFrom_ = std::abs(mouseFrame - tab.selectionStart) < std::abs(mouseFrame - tab.selectionEnd) ? tab.selectionEnd : tab.selectionStart;
            else if (io.KeyShift)
                dragFrom_ = tab.cursor;
            else
                dragFrom_ = mouseFrame;
        }
    }
    if (ImGui::IsItemActive() && drag_ != Drag::none) {
        int track = -1;
        int index = -1;
        const auto delta = mouseFrame - dragFrom_;
        const auto withClip = [&](auto&& change) {
            auto next = *real;
            if (!next.findClip(dragClip_, track, index))
                return;
            change(next, track, index);
            preview_ = std::move(next);
        };
        switch (drag_) {
        case Drag::select: {
            const auto row = std::clamp(rowAt(io.MousePos.y), 0, std::max(0, realCount - 1));
            tab.tracks.assign(real->tracks.size(), false);
            for (auto t = std::min(row, dragTrack_); t <= std::max(row, dragTrack_) && t < static_cast<int>(tab.tracks.size()); ++t)
                tab.tracks[static_cast<std::size_t>(t)] = true;
            if (row != dragTrack_)
                tab.mask = 0xffu;
            select(tab, std::min(dragFrom_, mouseFrame), std::max(dragFrom_, mouseFrame));
            tab.cursor = dragFrom_ <= mouseFrame ? tab.selectionStart : tab.selectionEnd;
            const auto span = tab.framesPerPixel * (right - left);
            if (io.MousePos.x > right - dp(10.0f))
                tab.viewStart += span * 0.02;
            else if (io.MousePos.x < left + dp(10.0f))
                tab.viewStart = std::max(0.0, tab.viewStart - span * 0.02);
            break;
        }
        case Drag::move:
            withClip([&](Session& next, const int from, const int at) {
                auto clip = next.tracks[static_cast<std::size_t>(from)].clips[static_cast<std::size_t>(at)];
                clip.start = std::max<std::int64_t>(0, dragOriginal_.start + delta);
                auto& clips = next.tracks[static_cast<std::size_t>(from)].clips;
                clips.erase(clips.begin() + at);
                auto target = std::clamp(rowAt(io.MousePos.y), 0, static_cast<int>(next.tracks.size()) - 1);
                if(next.tracks[target].midi!=next.tracks[from].midi)target=from;
                auto& destination = next.tracks[static_cast<std::size_t>(target)].clips;
                destination.push_back(clip);
                std::stable_sort(destination.begin(), destination.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
            });
            break;
        case Drag::trimLeft:
            withClip([&](Session& next, const int t, const int c) {
                auto& clip = next.tracks[static_cast<std::size_t>(t)].clips[static_cast<std::size_t>(c)];
                const auto d = std::clamp<std::int64_t>(delta, -std::min(dragOriginal_.offset, dragOriginal_.start), dragOriginal_.length - 32);
                clip.start = dragOriginal_.start + d;
                clip.offset = dragOriginal_.offset + d;
                clip.length = dragOriginal_.length - d;
                clip.fadeIn = std::min(clip.fadeIn, clip.length);
                clip.fadeOut = std::min(clip.fadeOut, clip.length - clip.fadeIn);
            });
            break;
        case Drag::trimRight:
            withClip([&](Session& next, const int t, const int c) {
                auto& clip = next.tracks[static_cast<std::size_t>(t)].clips[static_cast<std::size_t>(c)];
                clip.length = std::clamp<std::int64_t>(dragOriginal_.length + delta, 32, clip.sound?std::max<std::int64_t>(32,clip.sound->frames() - clip.offset):static_cast<std::int64_t>(real->rate*3600));
                clip.fadeOut = std::min(clip.fadeOut, clip.length);
                clip.fadeIn = std::min(clip.fadeIn, clip.length - clip.fadeOut);
            });
            break;
        case Drag::fadeIn:
            withClip([&](Session& next, const int t, const int c) {
                auto& clip = next.tracks[static_cast<std::size_t>(t)].clips[static_cast<std::size_t>(c)];
                clip.fadeIn = std::clamp<std::int64_t>(mouseFrame - clip.start, 0, clip.length - clip.fadeOut);
            });
            break;
        case Drag::fadeOut:
            withClip([&](Session& next, const int t, const int c) {
                auto& clip = next.tracks[static_cast<std::size_t>(t)].clips[static_cast<std::size_t>(c)];
                clip.fadeOut = std::clamp<std::int64_t>(clip.end() - mouseFrame, 0, clip.length - clip.fadeIn);
            });
            break;
        case Drag::pencil:
            if (preview_ && preview_->findClip(dragClip_, track, index)) {
                auto& clip = preview_->tracks[static_cast<std::size_t>(track)].clips[static_cast<std::size_t>(index)];
                const auto channels = std::min(2, std::max(1, clip.sound->channels()));
                const auto rowTop = origin.y + rowHeight * track - tab.laneScroll + dp(2.0f) + dp(clipHeaderDp);
                const auto laneHeight = (rowHeight - dp(4.0f) - dp(clipHeaderDp)) / static_cast<float>(channels);
                const auto centre = rowTop + laneHeight * (static_cast<float>(dragLane_) + 0.5f);
                const auto half = laneHeight * 0.46f * tab.amplitude;
                const auto value = std::clamp((centre - io.MousePos.y) / half, -1.0f, 1.0f);
                const auto at = std::clamp<std::int64_t>(mouseFrame - clip.start, 0, clip.length - 1) + clip.offset;
                const auto from = lastPencil_ < 0 ? at : lastPencil_;
                const auto fromValue = lastPencil_ < 0 ? value : lastPencilValue_;
                const auto a = std::min(from, at);
                const auto b = std::max(from, at);
                auto data = clip.sound->render(a, b + 1);
                for (auto f = a; f <= b; ++f) {
                    const auto t = b > a ? static_cast<float>(f - a) / static_cast<float>(b - a) : 1.0f;
                    const auto v = from <= at ? fromValue + (value - fromValue) * t : value + (fromValue - value) * t;
                    if (dragLane_ < static_cast<int>(data.size()))
                        data[static_cast<std::size_t>(dragLane_)][static_cast<std::size_t>(f - a)] = v;
                }
                clip.sound = std::make_shared<const Sound>(clip.sound->replaced(a, b + 1, std::move(data)));
                clip.edited = true;
                lastPencil_ = at;
                lastPencilValue_ = value;
            }
            break;
        default:
            break;
        }
    }
    if (ImGui::IsItemDeactivated()) {
        const auto clicked = std::abs(io.MouseClickedPos[0].x - io.MousePos.x) < 3.0f && std::abs(io.MouseClickedPos[0].y - io.MousePos.y) < 3.0f;
        if (preview_ && (!clicked || drag_ == Drag::pencil)) {
            const char* label = nullptr;
            switch (drag_) {
            case Drag::move: label = L("Déplacer le clip", "Move the clip"); break;
            case Drag::trimLeft:
            case Drag::trimRight: label = L("Rogner le clip", "Trim the clip"); break;
            case Drag::fadeIn: label = L("Fondu entrant du clip", "The clip's fade in"); break;
            case Drag::fadeOut: label = L("Fondu sortant du clip", "The clip's fade out"); break;
            case Drag::pencil: label = L("Crayon", "Pencil"); break;
            default: label = L("Modifier", "Edit"); break;
            }
            edit(tab, *preview_, juce::String::fromUTF8(label));
        } else if (drag_ == Drag::select && clicked) {
            // A click: the cursor here, the track picked, nothing selected.
            tab.selectionStart = tab.selectionEnd = tab.cursor = mouseFrame;
            const auto row = rowAt(io.MousePos.y);
            if (row >= 0 && row < realCount)
                pickTrack(tab, row, io.KeyCtrl);
            const auto hit = hitAt(tab, io.MousePos);
            tab.clip = hit.clip >= 0 && hit.track >= 0 && hit.track < realCount
                           ? real->tracks[static_cast<std::size_t>(hit.track)].clips[static_cast<std::size_t>(hit.clip)].id
                           : 0;
            if(tab.clip)tab.detailsFocus=true;
            if (playingTab_ == current_)
                playFrom(tab);
        }
        preview_.reset();
        drag_ = Drag::none;
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        const auto hit = hitAt(tab, io.MousePos);
        if (hit.clip >= 0 && hit.track >= 0 && hit.track < realCount) {
            const auto& clip = real->tracks[static_cast<std::size_t>(hit.track)].clips[static_cast<std::size_t>(hit.clip)];
            pickTrack(tab, hit.track, false);
            select(tab, clip.start, clip.end());
            tab.clip=clip.id;tab.detailsFocus=true;
            if(real->tracks[hit.track].midi)showMidiEditor(tab);
        } else if(hit.track>=0 && hit.track<realCount && real->tracks[hit.track].midi) {
            createMidiClip(tab,hit.track,mouseFrame);
        } else {
            std::fill(tab.tracks.begin(), tab.tracks.end(), false);
            select(tab, 0, real->frames());
        }
    }
    if (hovered && io.MouseWheel != 0.0f) {
        if (io.KeyShift) {
            tab.viewStart = std::max(0.0, tab.viewStart - io.MouseWheel * tab.framesPerPixel * (right - left) * 0.1);
        } else if (io.KeyCtrl) {
            tab.amplitude = std::clamp(tab.amplitude * (io.MouseWheel > 0.0f ? 1.25f : 0.8f), 1.0f, 64.0f);
        } else {
            const auto anchor = frameAt(io.MousePos.x);
            const auto most = std::max(1.0, static_cast<double>(std::max<std::int64_t>(real->frames(), static_cast<std::int64_t>(real->rate * 10.0)))
                                                / std::max(1.0f, right - left) * 2.0);
            tab.framesPerPixel = std::clamp(tab.framesPerPixel * (io.MouseWheel > 0.0f ? 0.8 : 1.25), 1.0 / 64.0, most);
            tab.viewStart = std::max(0.0, anchor - static_cast<double>(io.MousePos.x - left) * tab.framesPerPixel);
        }
        ui::takeWheel();
    }
    // A sound dragged in (the studio's browser, the DJ collection).
    if (ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload(kFilePayload); payload != nullptr && payload->DataSize > 1) {
            const auto row = rowAt(io.MousePos.y);
            (void) importFile(tab, juce::File(juce::String::fromUTF8(static_cast<const char*>(payload->Data))),
                              row >= 0 && row < realCount ? row : -1, mouseFrame);
        }
        ImGui::EndDragDropTarget();
    }
    if (hovered && ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        const auto hit = hitAt(tab, io.MousePos);
        if (hit.clip >= 0 && hit.track >= 0 && hit.track < realCount) {
            tab.clip = real->tracks[static_cast<std::size_t>(hit.track)].clips[static_cast<std::size_t>(hit.clip)].id;
            pickTrack(tab,hit.track,false);tab.detailsFocus=true;
            ImGui::OpenPopup("##clipMenu");
        } else {
            ImGui::OpenPopup("##lanesMenu");
        }
    }
    if (ImGui::BeginPopup("##clipMenu")) {
        clipMenu(tab);
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##lanesMenu")) {
        const auto selected = hasSelection(tab);
        if (ImGui::MenuItem(L("Couper", "Cut"), "Ctrl+X", false, selected))
            cut(tab);
        if (ImGui::MenuItem(L("Copier", "Copy"), "Ctrl+C", false, selected))
            copy(tab);
        if (ImGui::MenuItem(L("Coller", "Paste"), "Ctrl+V", false, clipboard_.has_value()))
            paste(tab, false);
        if (ImGui::MenuItem(L("Coller en mixant", "Paste mixed"), "Ctrl+Maj+V", false, clipboard_.has_value()))
            paste(tab, true);
        if (ImGui::MenuItem(L("Supprimer", "Delete"), "Suppr", false, selected))
            remove(tab);
        if (ImGui::MenuItem(L("Rogner", "Crop"), "Ctrl+T", false, selected))
            crop(tab);
        if (ImGui::MenuItem(L("Scinder au curseur", "Split at the cursor"), "S"))
            splitAtCursor(tab);
        ImGui::Separator();
        if (ImGui::MenuItem(L("Tout sélectionner", "Select all"), "Ctrl+A")) {
            std::fill(tab.tracks.begin(), tab.tracks.end(), false);
            select(tab, 0, tab.document->session().frames());
        }
        if (ImGui::MenuItem(L("Marqueur au curseur", "Marker at the cursor"), "M"))
            addMarker(tab, false);
        if (ImGui::MenuItem(L("Région de la sélection", "Region of the selection"), "Maj+M", false, selected))
            addMarker(tab, true);
        if(ImGui::BeginMenu(L("Nouvelle piste","New track"))){trackMenu(tab);ImGui::EndMenu();}
        ImGui::Separator();
        if (ImGui::MenuItem(L("Envoyer le mixage au studio", "Send the mix to the studio")))
            sendToStudio(tab, false);
        if (ImGui::MenuItem(L("Envoyer chaque piste au studio", "Send each track to the studio")))
            sendToStudio(tab, true);
        ImGui::EndPopup();
    }
    ImGui::PushClipRect(origin,ImVec2(right,trackBottom),true);
    for(int t=0;t<count;++t)if(tab.automationRows.contains(real->tracks[t].id)) {
        const auto y=origin.y+rowHeight*t-tab.laneScroll;
        if(y+rowHeight>=origin.y && y<=bottom)drawAutomation(tab,t,ImVec2(left,y+rowHeight*.42f),ImVec2(right,y+rowHeight));
    }
    ImGui::PopClipRect();
    drawAutomation(tab,-1,ImVec2(left,masterTop),ImVec2(right,bottom));
    const auto masterHead=xOf(playingTab_==current_?this->player().position():static_cast<double>(tab.cursor));
    if(masterHead>=left && masterHead<=right)list->AddLine(ImVec2(masterHead,masterTop),ImVec2(masterHead,bottom),playingTab_==current_?p.play:p.text,dp(1));
    ImGui::SetCursorScreenPos(ImVec2(origin.x, bottom + dp(4.0f)));
}

void FoundryView::drawStatus(Tab& tab)
{
    const auto& p = theme::palette();
    const auto& session = tab.document->session();
    const auto rate = session.rate;
    juce::String text;
    if (hasSelection(tab)) {
        const auto set = picked(tab);
        const auto tracks = static_cast<int>(std::count(set.begin(), set.end(), true));
        text << ui::Ls("Sélection ", "Selection ") << timeText(tab.selectionStart, rate) << " - " << timeText(tab.selectionEnd, rate) << "  ("
             << timeText(tab.selectionEnd - tab.selectionStart, rate) << ", " << juce::String(tracks) << ui::Ls(" piste(s))", " track(s))");
    } else {
        text << ui::Ls("Curseur ", "Cursor ") << timeText(tab.cursor, rate);
    }
    text << "     " << juce::String(juce::roundToInt(rate)) << " Hz  ·  " << juce::String(static_cast<int>(session.tracks.size()))
         << ui::Ls(" pistes", " tracks") << "  ·  " << timeText(session.frames(), rate);
    if (playingTab_ == current_)
        text << "     " << ui::Ls("Lecture ", "Playing ") << timeText(static_cast<std::int64_t>(this->player().position()), rate);
    ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 0.86f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::vec(p.textDim), "%s", text.toRawUTF8());
    ImGui::PopFont();
}

void FoundryView::drawSide(Tab& tab, const ImVec2 size)
{
    const auto& p = theme::palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.bg1);
    if (ImGui::BeginChild("##foundrySide", size, ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTabBar("##sideTabs")) {
            const bool focus=std::exchange(tab.detailsFocus,false);
            if(ImGui::BeginTabItem(L("Détails","Details"),nullptr,focus?ImGuiTabItemFlags_SetSelected:0)){drawClipInspector(tab);ImGui::EndTabItem();}
            if (ImGui::BeginTabItem(L("Analyse", "Analysis"))) {
                measure(tab, false);
                drawAnalysis(tab);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Historique", "History"))) {
                const auto history = tab.document->history();
                if (history.isEmpty())
                    ImGui::TextDisabled("%s", L("Aucune modification", "No change"));
                for (int i = history.size(); --i >= 0;)
                    ImGui::BulletText("%s", history[i].toRawUTF8());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void FoundryView::drawActions(Tab& tab, const ImVec2 size)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::palette().bg1);
    if (ImGui::BeginChild("##foundryActions", size, ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTabBar("##actionTabs")) {
            if (ImGui::BeginTabItem(L("Traitements", "Processes"))) {
                drawProcesses(tab);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(L("Marqueurs", "Markers"))) {
                drawMarkers(tab);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void FoundryView::showMidiEditor(Tab& tab)
{
    state_.setBottomPanelVisible(true);
    tab.processing->showEditor();
}

void FoundryView::drawProcesses(Tab& tab)
{
    const auto& p = theme::palette();
    const auto& session = tab.document->session();
    if (ImGui::BeginListBox("##processes", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 8.4f))) {
        for (int i = 0; i < processCount; ++i)
            if (ImGui::Selectable(processName(i), process_ == i))
                process_ = i;
        ImGui::EndListBox();
    }
    ImGui::Spacing();
    ImGui::PushFont(theme::boldFont(), 0.0f);
    ImGui::TextUnformatted(processName(process_));
    ImGui::PopFont();
    const auto [start, end] = target(tab);
    const auto set = picked(tab);
    const auto tracks = static_cast<int>(std::count(set.begin(), set.end(), true));
    juce::String on;
    if (process_ == processRate)
        on = ui::Ls("Toute la session", "The whole session");
    else if (process_ == processInsertSilence)
        on = ui::Ls("Au curseur, ", "At the cursor, ") + timeText(tab.cursor, session.rate) + ", " + juce::String(tracks) + ui::Ls(" piste(s)", " track(s)");
    else if (process_ == processChannels || !hasSelection(tab))
        on = juce::String(tracks) + ui::Ls(" piste(s), en entier", " track(s), whole");
    else
        on = ui::Ls("La sélection, ", "The selection, ") + timeText(end - start, session.rate) + ", " + juce::String(tracks)
             + ui::Ls(" piste(s)", " track(s)");
    ImGui::TextColored(theme::vec(p.textFaint), "%s", on.toRawUTF8());
    ImGui::Spacing();
    ImGui::SetNextItemWidth(-1.0f);
    switch (process_) {
    case processGain:
        ImGui::SliderFloat("##gain", &gainDb_, -48.0f, 24.0f, "%+.1f dB");
        break;
    case processNormalise:
        ImGui::SliderFloat("##normalise", &normaliseDb_, -30.0f, 0.0f, normaliseRms_ ? "RMS %.1f dB" : L("crête %.1f dBFS", "peak %.1f dBFS"));
        ImGui::Checkbox(L("Au niveau RMS (sinon la crête)", "To the RMS level (else the peak)"), &normaliseRms_);
        break;
    case processFadeIn:
    case processFadeOut: {
        const char* curves[4] { L("Linéaire", "Linear"), L("Rapide", "Fast"), L("Lente", "Slow"), L("Douce (S)", "Smooth (S)") };
        ImGui::Combo("##curve", &fadeCurve_, curves, 4);
        break;
    }
    case processInsertSilence:
        ImGui::SliderFloat("##silence", &silenceSeconds_, 0.01f, 60.0f, "%.2f s", ImGuiSliderFlags_Logarithmic);
        break;
    case processStretch: {
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Vitesse", "Speed"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##speed", &speedPercent_, 25.0f, 400.0f, "%.1f %%", ImGuiSliderFlags_Logarithmic);
        ImGui::TextColored(theme::vec(p.textFaint), "%s %s", L("Nouvelle durée :", "New length:"),
                           timeText(static_cast<std::int64_t>((end - start) / std::max(0.05, speedPercent_ / 100.0)), session.rate).toRawUTF8());
        const char* algorithms[4] { L("Bande (la hauteur suit)", "Tape (the pitch follows)"), L("Tonal", "Tonal"),
                                    L("Rythmique", "Rhythmic"), L("Texture", "Texture") };
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::Combo("##algorithm", &stretchAlgorithm_, algorithms, 4);
        if (stretchAlgorithm_ != 0) {
            ImGui::TextColored(theme::vec(p.textDim), "%s", L("Transposition", "Transposition"));
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::SliderFloat("##pitch", &pitchSemitones_, -24.0f, 24.0f, "%+.2f st");
        }
        break;
    }
    case processRate: {
        static constexpr int rates[] { 8'000, 11'025, 16'000, 22'050, 32'000, 44'100, 48'000, 88'200, 96'000, 176'400, 192'000 };
        ImGui::TextColored(theme::vec(p.textFaint), "%s %d Hz", L("Maintenant :", "Now:"), juce::roundToInt(session.rate));
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##rate", (juce::String(newRate_) + " Hz").toRawUTF8())) {
            for (const auto rate : rates)
                if (ImGui::Selectable((juce::String(rate) + " Hz").toRawUTF8(), rate == newRate_))
                    newRate_ = rate;
            ImGui::EndCombo();
        }
        break;
    }
    case processChannels: {
        const char* actions[5] { L("Mono (mélangés)", "Mono (mixed)"), L("Stéréo (doublé)", "Stereo (doubled)"),
                                 L("Échanger gauche et droite", "Swap left and right"), L("Garder la gauche", "Keep the left"),
                                 L("Garder la droite", "Keep the right") };
        ImGui::Combo("##channels", &channelsAction_, actions, 5);
        break;
    }
    case processEffects: {
        std::vector<TrackRef> audio;
        for (const auto& track : studio_.tracks())
            if (!track.midi)
                audio.push_back(track);
        audio.push_back(Studio::masterTrack());
        const auto name = effectsTrack_ >= 0 && effectsTrack_ < static_cast<int>(audio.size())
                              ? studio_.trackName(audio[static_cast<std::size_t>(effectsTrack_)])
                              : ui::Ls("Choisir une piste du projet…", "Pick a track of the project…");
        if (ImGui::BeginCombo("##effectsTrack", name.toRawUTF8())) {
            for (int i = 0; i < static_cast<int>(audio.size()); ++i)
                if (ImGui::Selectable(studio_.trackName(audio[static_cast<std::size_t>(i)]).toRawUTF8(), i == effectsTrack_))
                    effectsTrack_ = i;
            ImGui::EndCombo();
        }
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Traîne (réverbe, écho) gardée dans le clip", "Tail (reverb, echo) kept in the clip"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat("##tail", &tailSeconds_, 0.0f, 20.0f, "%.1f s");
        ImGui::PushStyleColor(ImGuiCol_Text, theme::vec(p.textFaint));
        ImGui::TextWrapped("%s", L("Les effets de la piste du studio (son panneau Périphériques) rendus dans chaque clip sous la sélection.",
                                   "The studio track's effects (its Devices panel) rendered into every clip under the selection."));
        ImGui::PopStyleColor();
        break;
    }
    default:
        break;
    }
    ImGui::Spacing();
    if (ui::primaryButton(L("Appliquer", "Apply"), ImVec2(-1.0f, 0.0f)))
        applyProcess(tab);
    ImGui::Spacing();
    ImGui::Separator();
    auto tail=tab.document->renderTail();
    if(ImGui::SliderFloat(L("Traîne export (s)","Export tail (s)"),&tail,0,30,"%.2f"))tab.document->setRenderTail(tail);
    if (ImGui::Button(L("Exporter le mixage…", "Export the mix…"), ImVec2(-1.0f, 0.0f)))
        chooseExport();
    if (ImGui::Button(L("Envoyer le mixage au studio", "Send the mix to the studio"), ImVec2(-1.0f, 0.0f)))
        sendToStudio(tab, false);
    ui::tooltip(L("Le mixage sur une nouvelle piste audio du projet", "The mix on a new audio track of the project"));
    if (ImGui::Button(L("Envoyer chaque piste au studio", "Send each track to the studio"), ImVec2(-1.0f, 0.0f)))
        sendToStudio(tab, true);
}

void FoundryView::drawMarkers(Tab& tab)
{
    const auto& p = theme::palette();
    const auto rate = tab.document->session().rate;
    if (ImGui::Button(L("+ Marqueur (M)", "+ Marker (M)")))
        addMarker(tab, false);
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasSelection(tab));
    if (ImGui::Button(L("+ Région (Maj+M)", "+ Region (Shift+M)")))
        addMarker(tab, true);
    ImGui::EndDisabled();
    ImGui::Spacing();
    auto markers = tab.document->markers();
    auto changed = false;
    auto removeAt = -1;
    for (int i = 0; i < static_cast<int>(markers.size()); ++i) {
        auto& marker = markers[static_cast<std::size_t>(i)];
        ImGui::PushID(i);
        const auto when = marker.region() ? timeText(marker.start, rate) + " - " + timeText(marker.end, rate) : timeText(marker.start, rate);
        if (ImGui::Selectable(when.toRawUTF8(), false, ImGuiSelectableFlags_None, ImVec2(ImGui::GetContentRegionAvail().x - dp(30.0f), 0.0f))) {
            if (marker.region())
                select(tab, marker.start, marker.end);
            else
                tab.cursor = marker.start;
            const auto span = tab.framesPerPixel * dp(600.0f);
            if (marker.start < tab.viewStart || marker.start > tab.viewStart + span)
                tab.viewStart = std::max(0.0, marker.start - span * 0.2);
        }
        ImGui::SameLine();
        if (ui::iconButton(icon::trash, L("Retirer", "Remove")))
            removeAt = i;
        std::array<char, 96> name {};
        marker.name.copyToUTF8(name.data(), name.size());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##name", name.data(), name.size(), ImGuiInputTextFlags_EnterReturnsTrue)) {
            marker.name = juce::String::fromUTF8(name.data());
            changed = true;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (removeAt >= 0) {
        markers.erase(markers.begin() + removeAt);
        changed = true;
    }
    if (changed)
        tab.document->setMarkers(std::move(markers), ui::Ls("Marqueurs", "Markers"));
    if (tab.document->markers().empty())
        ImGui::TextColored(theme::vec(p.textFaint), "%s",
                           L("Aucun marqueur. Ils sont gardés dans la session, et dans un WAV avec le son.",
                             "No marker. They are kept in the session, and in a WAV with the sound."));
}

void FoundryView::drawAnalysis(Tab& tab)
{
    const auto& p = theme::palette();
    const auto [start, end] = target(tab);
    const auto rate = tab.document->session().rate;
    ImGui::TextColored(theme::vec(p.textFaint), "%s %s",
                       hasSelection(tab) ? L("Le mixage de la sélection :", "The selection's mix:") : L("Le mixage, en entier :", "The whole mix:"),
                       timeText(end - start, rate).toRawUTF8());
    if (ImGui::Button(L("Mesurer", "Measure")))
        measure(tab, true);
    if (!tab.statistics) {
        ImGui::TextDisabled("%s", L("Choisissez ce qu'il faut mesurer.", "Pick what to measure."));
        return;
    }
    const auto& stats = *tab.statistics;
    const auto db = [](const double value) { return value > 1.0e-9 ? 20.0 * std::log10(value) : -120.0; };
    if (ImGui::BeginTable("##stats", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn(L("Gauche", "Left"));
        ImGui::TableSetupColumn(L("Droite", "Right"));
        ImGui::TableHeadersRow();
        const auto row = [&](const char* name, auto&& value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(name);
            for (int c = 0; c < 2; ++c) {
                ImGui::TableSetColumnIndex(1 + c);
                ImGui::TextUnformatted(value(c).toRawUTF8());
            }
        };
        row(L("Crête", "Peak"), [&](int c) { return juce::String(db(stats.peak[static_cast<std::size_t>(c)]), 2) + " dB"; });
        row("RMS", [&](int c) { return juce::String(db(stats.rms[static_cast<std::size_t>(c)]), 2) + " dB"; });
        row(L("Décalage DC", "DC offset"), [&](int c) { return juce::String(stats.dc[static_cast<std::size_t>(c)] * 100.0, 3) + " %"; });
        ImGui::EndTable();
    }
    ImGui::Text("%s %.1f LUFS", L("Sonie intégrée :", "Integrated loudness:"), stats.loudness);
    ImGui::Text("%s %lld", L("Échantillons à pleine échelle :", "Samples at full scale:"), static_cast<long long>(stats.clipped));
    if (!tab.spectrum.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(theme::vec(p.textDim), "%s", L("Spectre moyen", "Average spectrum"));
        const auto width = ImGui::GetContentRegionAvail().x;
        const auto height = dp(130.0f);
        const auto origin = ImGui::GetCursorScreenPos();
        auto* list = ImGui::GetWindowDrawList();
        ImGui::Dummy(ImVec2(width, height));
        list->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), p.bg0, dp(2.0f));
        for (const auto level : { -20.0f, -40.0f, -60.0f, -80.0f }) {
            const auto y = origin.y + height * (-level / 100.0f);
            list->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y), p.grid);
        }
        const auto nyquist = rate * 0.5;
        for (const auto hz : { 100.0, 1'000.0, 10'000.0 }) {
            if (hz >= nyquist)
                continue;
            const auto x = origin.x + static_cast<float>(std::log(hz / 20.0) / std::log(nyquist / 20.0)) * width;
            list->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), p.grid);
            list->AddText(ImVec2(x + dp(2.0f), origin.y + height - ImGui::GetFontSize()), p.textFaint,
                          hz >= 1'000.0 ? (juce::String(static_cast<int>(hz / 1'000.0)) + "k").toRawUTF8() : "100");
        }
        std::vector<ImVec2> points;
        const auto bins = tab.spectrum.size();
        for (std::size_t b = 0; b < bins; ++b) {
            const auto x = origin.x + width * (static_cast<float>(b) + 0.5f) / static_cast<float>(bins);
            const auto y = origin.y + height * std::clamp(-tab.spectrum[b] / 100.0f, 0.0f, 1.0f);
            points.emplace_back(x, y);
        }
        list->AddPolyline(points.data(), static_cast<int>(points.size()), p.accent, dp(1.5f));
    }
}

void FoundryView::handleKeys(Tab& tab)
{
    auto& io = ImGui::GetIO();
    const auto frames = tab.document->session().frames();
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
        togglePlay();
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false))
        loop_ = !loop_;
    if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) {
        tab.cursor = 0;
        tab.selectionStart = tab.selectionEnd = 0;
        tab.viewStart = 0.0;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End, false)) {
        tab.cursor = frames;
        tab.selectionStart = tab.selectionEnd = frames;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        remove(tab);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false))
        cut(tab);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
        copy(tab);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
        paste(tab, io.KeyShift);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        std::fill(tab.tracks.begin(), tab.tracks.end(), false);
        select(tab, 0, frames);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_T, false))
        crop(tab);
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
        duplicateClip(tab);
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        splitAtCursor(tab);
    if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_M, false))
        addMarker(tab, io.KeyShift);
    if (!io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Equal, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, true)))
        { if(cinema_)zoomCinema(tab,1.4);else tab.framesPerPixel = std::max(1.0 / 64.0, tab.framesPerPixel * 0.7); }
    if (!io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Minus, true) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, true)))
        { if(cinema_)zoomCinema(tab,1/1.4);else tab.framesPerPixel *= 1.4; }
}

void FoundryView::drawTransport()
{
    const auto& p = theme::palette();
    auto* tab = this->tab();
    auto& player = this->player();
    const auto playing = player.playing() && playingTab_ == current_;
    if (ui::iconButton(icon::previous, L("Au début (Origine)", "To the start (Home)")) && tab != nullptr) {
        tab->cursor = 0;
        tab->viewStart = 0.0;
    }
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(playing ? icon::pause : icon::play,
                       L("Lire depuis le curseur, ou la sélection (Espace)", "Play from the cursor, or the selection (Space)"), playing, p.play))
        togglePlay();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::stop, L("Stop", "Stop")))
        stop();
    ImGui::SameLine(0.0f, dp(2.0f));
    if (ui::iconButton(icon::loop, L("Lire la sélection en boucle (L)", "Play the selection in a loop (L)"), loop_))
        loop_ = !loop_;
    ui::toolbarSeparator();
    if (tab != nullptr) {
        const auto rate = tab->document->session().rate;
        const auto at = playing ? static_cast<std::int64_t>(player.position()) : tab->cursor;
        ImGui::PushFont(theme::monoFont(), ImGui::GetStyle().FontSizeBase * 1.15f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(playing ? p.play : p.text), "%s", timeText(at, rate).toRawUTF8());
        ImGui::PopFont();
        ImGui::SameLine(0.0f, dp(8.0f));
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textFaint), "%s", tab->document->name().toRawUTF8());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::vec(p.textFaint), "%s", L("Aucune session ouverte", "No session open"));
    }
}

// ---- The control port --------------------------------------------------------------------------

juce::var FoundryView::tabJson(const Tab& tab) const
{
    const auto& session = tab.document->session();
    auto* item = new juce::DynamicObject();
    item->setProperty("name", tab.document->name());
    item->setProperty("file", tab.document->file().getFullPathName());
    item->setProperty("sessionFile", tab.document->sessionFile().getFullPathName());
    item->setProperty("plainFile", tab.document->plainFile());
    item->setProperty("frames", session.frames());
    item->setProperty("rate", session.rate);
    item->setProperty("tempo",session.tempo);
    item->setProperty("cinema",session.cinema);
    auto* picture=new juce::DynamicObject();picture->setProperty("file",fromUtf8(session.picture.file));
    picture->setProperty("start",session.picture.start);picture->setProperty("sourceIn",session.picture.sourceIn);
    picture->setProperty("duration",session.picture.duration);picture->setProperty("fps",session.picture.fps);
    item->setProperty("picture",juce::var(picture));
    item->setProperty("activeGroup",static_cast<juce::int64>(tab.activeGroup));
    if(cinema_){juce::Array<juce::var> panes;for(const auto& pane:cinemaDropTargets_){auto* p=new juce::DynamicObject();p->setProperty("track",pane.track);
        p->setProperty("x",pane.min.x-ImGui::GetMainViewport()->Pos.x);p->setProperty("y",pane.min.y-ImGui::GetMainViewport()->Pos.y);p->setProperty("width",pane.max.x-pane.min.x);p->setProperty("height",pane.max.y-pane.min.y);p->setProperty("timelineX",pane.timelineX-ImGui::GetMainViewport()->Pos.x);p->setProperty("timelineWidth",pane.timelineWidth);panes.add(juce::var(p));}
        item->setProperty("panes",panes);item->setProperty("viewStart",tab.viewStart/session.rate);item->setProperty("viewSeconds",tab.cinemaVisibleSeconds);
        item->setProperty("contentsVisible",tab.cinemaContentsVisible);
        juce::Array<juce::var> windows;
        for(const auto* id:{"###cinemaTimeline","###cinemaPreview","###cinemaTools","###cinemaDetails"}){
            const auto* w=ImGui::FindWindowByID(ImHashStr(id));if(!w||!w->WasActive)continue;
            auto* v=new juce::DynamicObject();v->setProperty("id",id);v->setProperty("x",w->Pos.x-ImGui::GetMainViewport()->Pos.x);v->setProperty("y",w->Pos.y-ImGui::GetMainViewport()->Pos.y);
            v->setProperty("width",w->Size.x);v->setProperty("height",w->Size.y);v->setProperty("dock",static_cast<juce::int64>(w->DockId));windows.add(juce::var(v));
        }item->setProperty("windows",windows);
        item->setProperty("imguiErrorCount",GImGui->ErrorCountCurrentFrame);
        item->setProperty("imguiErrorLog",juce::String(GImGui->DebugLogBuf.c_str()));
        juce::Array<juce::var> thumbnailFrames;for(const auto& [frame,thumbnail]:tab.thumbnails)thumbnailFrames.add(static_cast<juce::int64>(frame));
        item->setProperty("thumbnailFrames",thumbnailFrames);
        auto* navigator=new juce::DynamicObject();navigator->setProperty("x",cinemaNavigatorMin_.x-ImGui::GetMainViewport()->Pos.x);navigator->setProperty("y",cinemaNavigatorMin_.y-ImGui::GetMainViewport()->Pos.y);
        navigator->setProperty("width",cinemaNavigatorMax_.x-cinemaNavigatorMin_.x);navigator->setProperty("height",cinemaNavigatorMax_.y-cinemaNavigatorMin_.y);item->setProperty("navigatorBounds",juce::var(navigator));
        item->setProperty("followPaused",cinemaFollowPaused_);
        item->setProperty("filmstrip",tab.cinemaFilmstrip);item->setProperty("thumbnailCount",static_cast<int>(tab.thumbnails.size()));
        auto* strip=new juce::DynamicObject();strip->setProperty("x",cinemaStripMin_.x-ImGui::GetMainViewport()->Pos.x);strip->setProperty("y",cinemaStripMin_.y-ImGui::GetMainViewport()->Pos.y);strip->setProperty("width",cinemaStripMax_.x-cinemaStripMin_.x);strip->setProperty("height",cinemaStripMax_.y-cinemaStripMin_.y);item->setProperty("filmstripBounds",juce::var(strip));item->setProperty("toolsPanel",tab.processing?tab.processing->selectedPanel():0);
        juce::Array<juce::var> buses;for(const auto& bus:cinemaBusTargets_){auto* b=new juce::DynamicObject();b->setProperty("id",static_cast<juce::int64>(bus.id));b->setProperty("x",bus.min.x-ImGui::GetMainViewport()->Pos.x);b->setProperty("y",bus.min.y-ImGui::GetMainViewport()->Pos.y);b->setProperty("width",bus.max.x-bus.min.x);b->setProperty("height",bus.max.y-bus.min.y);buses.add(juce::var(b));}item->setProperty("buses",buses);
        juce::Array<juce::var> list;for(const auto& row:cinemaListTargets_){auto* r=new juce::DynamicObject();r->setProperty("id",static_cast<juce::int64>(row.id));r->setProperty("x",row.min.x-ImGui::GetMainViewport()->Pos.x);r->setProperty("y",row.min.y-ImGui::GetMainViewport()->Pos.y);r->setProperty("width",row.max.x-row.min.x);r->setProperty("height",row.max.y-row.min.y);list.add(juce::var(r));}item->setProperty("subtrackFocusButtons",list);}

    if(tab.video){const auto video=tab.video->snapshot();auto* decoder=new juce::DynamicObject();
        decoder->setProperty("backend",video.backend);decoder->setProperty("codec",video.codec);decoder->setProperty("error",video.error);decoder->setProperty("loading",video.loading);decoder->setProperty("width",video.width);
        decoder->setProperty("height",video.height);decoder->setProperty("duration",video.duration);decoder->setProperty("fps",video.fps);
        decoder->setProperty("time",video.time);decoder->setProperty("revision",static_cast<juce::int64>(video.revision));item->setProperty("decoder",juce::var(decoder));}

    item->setProperty("masterAutomation",foundry_json::automation(session.masterAutomation));
    item->setProperty("seconds", session.rate > 0.0 ? static_cast<double>(session.frames()) / session.rate : 0.0);
    item->setProperty("bits", tab.document->bitsPerSample());
    item->setProperty("dirty", tab.document->dirty());
    item->setProperty("selectionStart", tab.selectionStart);
    item->setProperty("selectionEnd", tab.selectionEnd);
    item->setProperty("cursor", tab.cursor);
    item->setProperty("mask", static_cast<int>(tab.mask & 0b11u));
    item->setProperty("clip", static_cast<juce::int64>(tab.clip));
    item->setProperty("undo", tab.document->undoLabel());
    item->setProperty("redo", tab.document->redoLabel());
    juce::Array<juce::var> pickedTracks;
    const auto set = picked(tab);
    for (std::size_t t = 0; t < set.size(); ++t)
        if (set[t])
            pickedTracks.add(static_cast<int>(t));
    item->setProperty("pickedTracks", pickedTracks);
    // The first clip's channels, as a single file would say them.
    if (!session.tracks.empty() && !session.tracks[0].clips.empty() && session.tracks[0].clips[0].sound != nullptr)
        item->setProperty("channels", session.tracks[0].clips[0].sound->channels());
    juce::Array<juce::var> tracks;
    for (const auto& track : session.tracks) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("id",static_cast<juce::int64>(track.id));
        entry->setProperty("midi",track.midi);
        entry->setProperty("bus",track.bus);
        entry->setProperty("virtualGroup",track.virtualGroup);entry->setProperty("parentId",static_cast<juce::int64>(track.parentId));
        entry->setProperty("containerStart",track.containerStart);entry->setProperty("containerLength",track.containerLength);
        entry->setProperty("automation",foundry_json::automation(track.automation));
        entry->setProperty("name", fromUtf8(track.name));
        entry->setProperty("gainDb", track.gainDb);
        entry->setProperty("pan", track.pan);
        entry->setProperty("mute", track.mute);
        entry->setProperty("solo", track.solo);
        juce::Array<juce::var> clips;
        for (const auto& clip : track.clips) {
            auto* c = new juce::DynamicObject();
            c->setProperty("id", static_cast<juce::int64>(clip.id));
            c->setProperty("notes",foundry_json::notes(clip.notes));
            c->setProperty("name", fromUtf8(clip.name));
            c->setProperty("start", clip.start);
            c->setProperty("offset", clip.offset);
            c->setProperty("length", clip.length);
            c->setProperty("fadeIn", clip.fadeIn);
            c->setProperty("fadeOut", clip.fadeOut);
            c->setProperty("gainDb", clip.gainDb);
            c->setProperty("channels", clip.sound != nullptr ? clip.sound->channels() : 0);
            c->setProperty("edited", clip.edited);
            clips.add(juce::var(c));
        }
        entry->setProperty("clips", clips);
        tracks.add(juce::var(entry));
    }
    item->setProperty("tracks", tracks);
    juce::Array<juce::var> markers;
    for (const auto& marker : tab.document->markers()) {
        auto* entry = new juce::DynamicObject();
        entry->setProperty("start", marker.start);
        entry->setProperty("end", marker.end);
        entry->setProperty("name", marker.name);
        markers.add(juce::var(entry));
    }
    item->setProperty("markers", markers);
    return juce::var(item);
}

juce::var FoundryView::command(const juce::var& request)
{
    const auto action = request.getProperty("action", "status").toString().trim().toLowerCase();
    if (request.hasProperty("tab")) {
        const auto index = static_cast<int>(request.getProperty("tab", 0));
        if (index < 0 || index >= static_cast<int>(tabs_.size()))
            return juce::String("no tab ") + juce::String(index);
        current_ = index;
    }
    if(cinema_&&(action=="video"||action=="video_audio"||action=="group"||action=="add_subtrack"||action=="assign"||action=="view"||action=="container"||action=="add_track"))return cinemaCommand(request);
    auto* tab = this->tab();
    if(action=="processing") {
        if(!tab)return juce::String("no session open");
        syncProcessing(*tab);const auto result=tab->processing->command(request);commitProcessing(*tab, true);
        return static_cast<bool>(result.getProperty("ok",false))?result.getProperty("result",{}):result.getProperty("error",{});
    }
    if(tab){
        syncProcessing(*tab);
        commitProcessing(*tab, action == "save" || action == "save_session" || action == "export" || action == "send_to_studio");
    }

    const auto needTab = [&]() -> juce::String { return tab == nullptr ? juce::String("no session open") : juce::String(); };
    const auto rate = tab != nullptr ? tab->document->session().rate : 48'000.0;
    const auto frameOf = [&](const char* key, const std::int64_t fallback) {
        if (!request.hasProperty(key))
            return fallback;
        const auto value = number(request, key, 0.0);
        return static_cast<bool>(request.getProperty("seconds", false)) ? static_cast<std::int64_t>(std::llround(value * rate))
                                                                        : static_cast<std::int64_t>(value);
    };
    const auto trackOf = [&](const char* key, const int fallback) {
        return request.hasProperty(key) ? static_cast<int>(request.getProperty(key, fallback)) : fallback;
    };
    juce::String error;
    if (action == "status" || action.isEmpty()) {
    } else if (action == "open") {
        if (!open(juce::File(request.getProperty("file", juce::var()).toString())))
            error = state_.status;
    } else if (action == "new") {
        Tab created;
        created.document = FoundryDocument::create(number(request, "rate", 48'000.0), request.getProperty("name", "Untitled").toString());
        if(cinema_)ensureCinema(created);
        tabs_.push_back(std::move(created));
        current_ = static_cast<int>(tabs_.size()) - 1;
    } else if (action == "close") {
        closeTab(current_);
    } else if (action == "import") {
        if ((error = needTab()).isEmpty())
            if (!importFile(*tab, juce::File(request.getProperty("file", juce::var()).toString()), trackOf("track", -1), frameOf("at", tab->cursor)))
                error = state_.status;
    } else if (action == "add_track") {
        if ((error = needTab()).isEmpty())
            addTrack(*tab,request.getProperty("type","audio").toString()=="midi");
    } else if(action=="midi_clip") {
        if((error=needTab()).isEmpty())createMidiClip(*tab,trackOf("track",0),frameOf("at",tab->cursor));
    } else if(action=="notes") {
        if((error=needTab()).isEmpty()) {
            auto next=tab->document->session();int t{},c{};
            const auto id=static_cast<std::uint64_t>(static_cast<juce::int64>(request.getProperty("id",static_cast<juce::int64>(tab->clip))));
            if(!next.findClip(id,t,c)||!next.tracks[t].midi)error="no MIDI clip";
            else {next.tracks[t].clips[c].notes=foundry_json::notes(request.getProperty("notes",{}),next);edit(*tab,std::move(next),"MIDI notes");}
        }
    } else if(action=="automation") {
        if((error=needTab()).isEmpty()) {
            auto next=tab->document->session();const auto index=trackOf("track",-1);
            if(index>=static_cast<int>(next.tracks.size()))error="no track";
            else {auto lanes=foundry_json::automation(request.getProperty("lanes",{}));if(index<0)next.masterAutomation=std::move(lanes);else next.tracks[index].automation=std::move(lanes);edit(*tab,std::move(next),"Automation");}
        }
    } else if(action=="reverse_clip") {
        if((error=needTab()).isEmpty()){tab->clip=static_cast<juce::int64>(request.getProperty("id",static_cast<juce::int64>(tab->clip)));reverseClip(*tab);}
    } else if (action == "remove_track") {
        if ((error = needTab()).isEmpty())
            edit(*tab, engine::foundry::withoutTrack(tab->document->session(), trackOf("track", 0)), "Remove the track");
    } else if (action == "track") {
        if ((error = needTab()).isEmpty()) {
            const auto index = trackOf("track", 0);
            auto next = tab->document->session();
            if (index < 0 || index >= static_cast<int>(next.tracks.size())) {
                error = "no track " + juce::String(index);
            } else {
                auto& track = next.tracks[static_cast<std::size_t>(index)];
                track.gainDb = static_cast<float>(number(request, "gainDb", track.gainDb));
                track.pan = static_cast<float>(number(request, "pan", track.pan));
                track.mute = static_cast<bool>(request.getProperty("mute", track.mute));
                track.solo = static_cast<bool>(request.getProperty("solo", track.solo));
                if (request.hasProperty("name"))
                    track.name = request.getProperty("name", "").toString().toStdString();
                tab->document->replace(std::move(next));
                if (playingTab_ == current_)
                    this->player().setSession(tab->document->shared());
            }
        }
    } else if (action == "clip") {
        if ((error = needTab()).isEmpty()) {
            auto next = tab->document->session();
            int track = -1;
            int index = -1;
            if (!next.findClip(static_cast<std::uint64_t>(static_cast<juce::int64>(request.getProperty("id", 0))), track, index)) {
                error = "no clip with that id";
            } else {
                auto clip = next.tracks[static_cast<std::size_t>(track)].clips[static_cast<std::size_t>(index)];
                clip.start = std::max<std::int64_t>(0, frameOf("start", clip.start));
                clip.offset = std::clamp<std::int64_t>(frameOf("offset", clip.offset), 0, (clip.sound?clip.sound->frames():static_cast<std::int64_t>(rate*3600)) - 1);
                clip.length = std::clamp<std::int64_t>(frameOf("length", clip.length), 1, (clip.sound?clip.sound->frames():static_cast<std::int64_t>(rate*3600)) - clip.offset);
                clip.fadeIn = std::clamp<std::int64_t>(frameOf("fadeIn", clip.fadeIn), 0, clip.length);
                clip.fadeOut = std::clamp<std::int64_t>(frameOf("fadeOut", clip.fadeOut), 0, clip.length - clip.fadeIn);
                clip.gainDb = static_cast<float>(number(request, "gainDb", clip.gainDb));
                auto& clips = next.tracks[static_cast<std::size_t>(track)].clips;
                clips.erase(clips.begin() + index);
                if (!static_cast<bool>(request.getProperty("delete", false))) {
                    const auto destination = std::clamp(trackOf("toTrack", track), 0, static_cast<int>(next.tracks.size()) - 1);
                    auto& target = next.tracks[static_cast<std::size_t>(destination)].clips;
                    target.push_back(clip);
                    std::stable_sort(target.begin(), target.end(), [](const Clip& a, const Clip& b) { return a.start < b.start; });
                }
                edit(*tab, std::move(next), "Clip");
            }
        }
    } else if (action == "select") {
        if ((error = needTab()).isEmpty()) {
            select(*tab, frameOf("start", 0), frameOf("end", tab->document->session().frames()));
            const auto channels = request.getProperty("channels", "both").toString().toLowerCase();
            tab->mask = channels == "left" ? 0b01u : channels == "right" ? 0b10u : 0xffu;
            tab->tracks.assign(tab->document->session().tracks.size(), false);
            if (const auto* tracks = request.getProperty("tracks", juce::var()).getArray())
                for (const auto& index : *tracks)
                    if (static_cast<int>(index) >= 0 && static_cast<std::size_t>(static_cast<int>(index)) < tab->tracks.size())
                        tab->tracks[static_cast<std::size_t>(static_cast<int>(index))] = true;
        }
    } else if (action == "cursor") {
        if ((error = needTab()).isEmpty()) {
            tab->cursor = std::max<std::int64_t>(0, frameOf("at", 0));
            tab->selectionStart = tab->selectionEnd = tab->cursor;
            if (request.hasProperty("track"))
                pickTrack(*tab, trackOf("track", 0), false);
        }
    } else if (action == "edit") {
        if ((error = needTab()).isEmpty()) {
            const auto op = request.getProperty("op", juce::var()).toString().toLowerCase();
            if (op == "cut")
                cut(*tab);
            else if (op == "copy")
                copy(*tab);
            else if (op == "paste")
                paste(*tab, false);
            else if (op == "paste_mix")
                paste(*tab, true);
            else if (op == "delete")
                remove(*tab);
            else if (op == "crop")
                crop(*tab);
            else if (op == "split")
                splitAtCursor(*tab);
            else if (op == "select_all") {
                std::fill(tab->tracks.begin(), tab->tracks.end(), false);
                select(*tab, 0, tab->document->session().frames());
            } else
                error = "op: cut, copy, paste, paste_mix, delete, crop, split, select_all";
        }
    } else if (action == "process") {
        if ((error = needTab()).isEmpty()) {
            static const std::pair<const char*, int> names[] { { "gain", processGain },         { "normalise", processNormalise },
                                                               { "fade_in", processFadeIn },     { "fade_out", processFadeOut },
                                                               { "invert", processInvert },      { "reverse", processReverse },
                                                               { "dc", processDc },              { "silence", processSilence },
                                                               { "insert_silence", processInsertSilence },
                                                               { "stretch", processStretch },    { "rate", processRate },
                                                               { "channels", processChannels },  { "effects", processEffects } };
            const auto name = request.getProperty("name", juce::var()).toString().toLowerCase();
            auto found = -1;
            for (const auto& [key, value] : names)
                if (name == key)
                    found = value;
            if (found < 0) {
                error = "name: gain, normalise, fade_in, fade_out, invert, reverse, dc, silence, insert_silence, stretch, rate, channels, effects";
            } else {
                process_ = found;
                gainDb_ = static_cast<float>(number(request, "gainDb", gainDb_));
                normaliseDb_ = static_cast<float>(number(request, "targetDb", normaliseDb_));
                normaliseRms_ = static_cast<bool>(request.getProperty("rms", normaliseRms_));
                fadeCurve_ = static_cast<int>(number(request, "curve", fadeCurve_));
                silenceSeconds_ = static_cast<float>(number(request, "duration", silenceSeconds_));
                speedPercent_ = static_cast<float>(number(request, "speed", speedPercent_ / 100.0) * 100.0);
                pitchSemitones_ = static_cast<float>(number(request, "pitch", pitchSemitones_));
                stretchAlgorithm_ = static_cast<int>(number(request, "algorithm", stretchAlgorithm_));
                newRate_ = static_cast<int>(number(request, "rate", newRate_));
                channelsAction_ = static_cast<int>(number(request, "channels", channelsAction_));
                tailSeconds_ = static_cast<float>(number(request, "tail", tailSeconds_));
                if (found == processEffects) {
                    const auto trackName = request.getProperty("track", juce::var()).toString();
                    std::vector<TrackRef> audio;
                    for (const auto& track : studio_.tracks())
                        if (!track.midi)
                            audio.push_back(track);
                    audio.push_back(Studio::masterTrack());
                    effectsTrack_ = -1;
                    for (int i = 0; i < static_cast<int>(audio.size()); ++i)
                        if (studio_.trackName(audio[static_cast<std::size_t>(i)]).equalsIgnoreCase(trackName)
                            || (trackName.equalsIgnoreCase("master") && audio[static_cast<std::size_t>(i)].isMaster()))
                            effectsTrack_ = i;
                    if (effectsTrack_ < 0)
                        error = "no audio track \"" + trackName + "\"";
                }
                if (error.isEmpty()) {
                    const auto before = tab->document->revision();
                    applyProcess(*tab);
                    if (tab->document->revision() == before && state_.statusIsError)
                        error = state_.status;
                }
            }
        }
    } else if (action == "undo") {
        if (!undo())
            error = "nothing to undo";
    } else if (action == "redo") {
        if (!redo())
            error = "nothing to redo";
    } else if (action == "save") {
        if ((error = needTab()).isEmpty()) {
            const auto path = request.getProperty("path", juce::var()).toString();
            const auto bits = static_cast<int>(number(request, "bits", tab->document->bitsPerSample()));
            const auto file = path.isNotEmpty() ? juce::File(path) : tab->document->file();
            if (file == juce::File())
                error = "path: where to save it";
            else if (const auto result = tab->document->save(file, bits); result.failed())
                error = result.getErrorMessage();
        }
    } else if (action == "save_session") {
        if ((error = needTab()).isEmpty()) {
            const auto path = request.getProperty("path", juce::var()).toString();
            const auto file = path.isNotEmpty() ? juce::File(path) : tab->document->sessionFile();
            if (file == juce::File())
                error = "path: where to save it";
            else if (const auto result = tab->document->saveSession(file.withFileExtension(cinema_?"cycinema":"cyfoundry")); result.failed())
                error = result.getErrorMessage();
        }
    } else if (action == "export") {
        if ((error = needTab()).isEmpty()) {
            if(request.hasProperty("tail"))tab->document->setRenderTail(static_cast<float>(number(request,"tail",0)));
            const auto path = request.getProperty("path", juce::var()).toString();
            if (path.isEmpty())
                error = "path: where to write the mix";
            else if (const auto result = tab->document->exportMix(juce::File(path), static_cast<int>(number(request, "bits", 24.0)),
                                                                  frameOf("start", 0), frameOf("end", 0));
                     result.failed())
                error = result.getErrorMessage();
        }
    } else if (action == "play") {
        if ((error = needTab()).isEmpty()) {
            loop_ = static_cast<bool>(request.getProperty("loop", loop_));
            playFrom(*tab);
        }
    } else if (action == "stop") {
        stop();
    } else if (action == "marker") {
        if ((error = needTab()).isEmpty()) {
            const auto what = request.getProperty("do", "add").toString().toLowerCase();
            if (what == "add" || what == "region") {
                addMarker(*tab, what == "region");
            } else if (what == "remove") {
                auto markers = tab->document->markers();
                const auto index = static_cast<std::size_t>(number(request, "index", 0.0));
                if (index < markers.size()) {
                    markers.erase(markers.begin() + static_cast<std::ptrdiff_t>(index));
                    tab->document->setMarkers(std::move(markers), "Markers");
                } else {
                    error = "no marker there";
                }
            }
        }
    } else if (action == "stats") {
        if ((error = needTab()).isEmpty())
            measure(*tab, true);
    } else if (action == "send_to_studio") {
        if ((error = needTab()).isEmpty())
            sendToStudio(*tab, static_cast<bool>(request.getProperty("eachTrack", false)));
    } else {
        error = "action: status, open, new, close, import, add_track, remove_track, track, clip, select, cursor, edit, process, undo, "
                "redo, save, save_session, export, play, stop, marker, stats, send_to_studio";
    }
    if (error.isNotEmpty())
        return error;
    auto* result = new juce::DynamicObject();
    if(auto* current=this->tab()){syncProcessing(*current);commitProcessing(*current);}
    juce::Array<juce::var> list;
    for (const auto& each : tabs_)
        list.add(tabJson(each));
    result->setProperty("tabs", list);
    result->setProperty("current", current_);
    result->setProperty("playing", this->player().playing());
    result->setProperty("position", this->player().position());
    const auto peak=std::max(this->player().takePeak(0),this->player().takePeak(1));
    result->setProperty("outputPeakDb",peak>1.0e-6f?20.0*std::log10(peak):-120.0);
    result->setProperty("loop", loop_);
    result->setProperty("clipboardFrames", clipboard_ ? clipboard_->frames() : 0);
    if (tab = this->tab(); tab != nullptr && tab->statistics && action == "stats") {
        const auto& stats = *tab->statistics;
        auto* measured = new juce::DynamicObject();
        juce::Array<juce::var> peaks;
        juce::Array<juce::var> rms;
        juce::Array<juce::var> dc;
        for (int c = 0; c < 2; ++c) {
            peaks.add(stats.peak[static_cast<std::size_t>(c)] > 1.0e-9f ? 20.0 * std::log10(stats.peak[static_cast<std::size_t>(c)]) : -120.0);
            rms.add(stats.rms[static_cast<std::size_t>(c)] > 1.0e-9 ? 20.0 * std::log10(stats.rms[static_cast<std::size_t>(c)]) : -120.0);
            dc.add(stats.dc[static_cast<std::size_t>(c)]);
        }
        measured->setProperty("peakDb", peaks);
        measured->setProperty("rmsDb", rms);
        measured->setProperty("dc", dc);
        measured->setProperty("loudness", stats.loudness);
        measured->setProperty("clipped", stats.clipped);
        measured->setProperty("frames", stats.frames);
        juce::Array<juce::var> spectrum;
        for (const auto value : tab->spectrum)
            spectrum.add(value);
        measured->setProperty("spectrumDb", spectrum);
        result->setProperty("stats", juce::var(measured));
    }
    return juce::var(result);
}

#include "FoundryEditing.inc"

#include "CinemaEditing.inc"

} // namespace cydaw::v2
