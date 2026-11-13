#include <cydaw/base/Utilities.h>
#include <cydaw/base/Thread.h>
#include <cydaw/integration/AudioFiles.h>
#include <cydaw/base/File.h>
#include <cydaw/integration/CyAudioCoreAudioFormats.h>
#include "DjLibrary.h"

#include "../app/DjEngine.h"

#include <cydaw/engine/DjMix.h>

#include <algorithm>
#include <set>

namespace cydaw::v2 {

namespace {

constexpr int maximumScannedFiles = 50'000;

bool isAudioFile(const cydaw::base::File& file)
{
    return file.hasFileExtension("wav;wave;aif;aiff;flac;ogg;mp3;wma;m4a");
}

double number(const cydaw::base::Value& object, const char* key, const double fallback = 0.0)
{
    const auto value = object.getProperty(key, cydaw::base::Value());
    return value.isVoid() ? fallback : static_cast<double>(value);
}

} // namespace

DjLibrary::DjLibrary() : cydaw::base::Thread("CyDAW DJ collection")
{
    startThread(cydaw::base::Thread::Priority::low);
}

DjLibrary::~DjLibrary()
{
    signalThreadShouldExit();
    wake_.signal();
    stopThread(10'000);
    flush();
}

cydaw::base::File DjLibrary::collectionFile()
{
    if (const auto other = cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_DJ_COLLECTION_FILE", {}); other.isNotEmpty())
        return cydaw::base::File(other);
    return cydaw::base::File::getSpecialLocation(cydaw::base::File::userApplicationDataDirectory).getChildFile("CyDAW").getChildFile("DjCollection.json");
}

void DjLibrary::load()
{
    const auto own = cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_DJ_COLLECTION_FILE", {}).isNotEmpty();
    const auto automated = cydaw::base::SystemStats::getEnvironmentVariable("CYDAW_NO_RECOVERY", {}).isNotEmpty();
    saving_ = own || !automated;
    if (!saving_)
        return;
    const auto file = collectionFile();
    if (file.existsAsFile()) {
        fromJson(cydaw::base::JSON::parse(file));
    } else if (!automated) {
        // The first time: the person's music.
        const auto music = cydaw::base::File::getSpecialLocation(cydaw::base::File::userMusicDirectory);
        if (music.isDirectory()) {
            const std::scoped_lock lock(lock_);
            folders_.add(music.getFullPathName());
        }
    }
    rescan();
}

void DjLibrary::flush()
{
    if (!saving_ || !dirty_.exchange(false))
        return;
    const auto file = collectionFile();
    (void) file.getParentDirectory().createDirectory();
    (void) file.replaceWithText(cydaw::base::JSON::toString(toJson()));
}

void DjLibrary::tick()
{
    if (dirty_.load() && cydaw::base::Time::getMillisecondCounterHiRes() * 0.001 - dirtySince_ > 2.0)
        flush();
}

void DjLibrary::changed()
{
    revision_.fetch_add(1, std::memory_order_acq_rel);
    if (!dirty_.exchange(true))
        dirtySince_ = cydaw::base::Time::getMillisecondCounterHiRes() * 0.001;
}

cydaw::base::TextList DjLibrary::folders() const
{
    const std::scoped_lock lock(lock_);
    return folders_;
}

void DjLibrary::addFolder(const cydaw::base::File& folder)
{
    if (!folder.isDirectory())
        return;
    {
        const std::scoped_lock lock(lock_);
        folders_.addIfNotAlreadyThere(folder.getFullPathName());
    }
    changed();
    rescan();
}

void DjLibrary::removeFolder(const cydaw::base::File& folder)
{
    {
        const std::scoped_lock lock(lock_);
        folders_.removeString(folder.getFullPathName());
        // Its tracks go, but those played or set by hand.
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                                     [&](const DjTrack& track) {
                                         return track.file.isAChildOf(folder) && track.plays == 0 && !track.gridSet
                                                && std::all_of(track.hotCues.begin(), track.hotCues.end(), [](double cue) { return cue < 0.0; });
                                     }),
                      tracks_.end());
    }
    changed();
}

void DjLibrary::rescan()
{
    {
        const std::scoped_lock lock(lock_);
        scanWanted_ = true;
    }
    wake_.signal();
}

std::vector<DjTrack> DjLibrary::tracks() const
{
    const std::scoped_lock lock(lock_);
    return tracks_;
}

std::size_t DjLibrary::indexOf(const cydaw::base::File& file) const
{
    for (std::size_t i = 0; i < tracks_.size(); ++i)
        if (tracks_[i].file == file)
            return i;
    return tracks_.size();
}

std::optional<DjTrack> DjLibrary::find(const cydaw::base::File& file) const
{
    const std::scoped_lock lock(lock_);
    const auto index = indexOf(file);
    if (index >= tracks_.size())
        return std::nullopt;
    return tracks_[index];
}

void DjLibrary::remember(const cydaw::base::File& file)
{
    if (!file.existsAsFile())
        return;
    {
        const std::scoped_lock lock(lock_);
        if (indexOf(file) < tracks_.size())
            return;
        DjTrack track;
        track.file = file;
        app::djTrackTags(file, {}, track.title, track.artist);
        track.size = file.getSize();
        track.modified = file.getLastModificationTime().toMilliseconds();
        tracks_.push_back(std::move(track));
    }
    changed();
}

void DjLibrary::update(const DjTrack& track)
{
    {
        const std::scoped_lock lock(lock_);
        const auto index = indexOf(track.file);
        if (index < tracks_.size())
            tracks_[index] = track;
        else
            tracks_.push_back(track);
    }
    changed();
}

void DjLibrary::analyse(const cydaw::base::File& file)
{
    {
        const std::scoped_lock lock(lock_);
        if (std::find(queue_.begin(), queue_.end(), file) == queue_.end())
            queue_.push_back(file);
    }
    wake_.signal();
}

void DjLibrary::analyseAll()
{
    {
        const std::scoped_lock lock(lock_);
        for (const auto& track : tracks_)
            if (!track.analysed && track.error.isEmpty() && std::find(queue_.begin(), queue_.end(), track.file) == queue_.end())
                queue_.push_back(track.file);
    }
    wake_.signal();
}

DjLibrary::Progress DjLibrary::progress() const
{
    const std::scoped_lock lock(lock_);
    return { scanning_, static_cast<int>(queue_.size()) + (current_.isNotEmpty() ? 1 : 0), current_ };
}

void DjLibrary::run()
{
    const app::DjEngine::DecodingThreadScope decoding;
    cydaw::audio::AudioFormatManager formats;
    cydaw::integration::registerCyAudioCoreReaders(formats);
    while (!threadShouldExit()) {
        wake_.wait(1'000);
        bool scanNow = false;
        {
            const std::scoped_lock lock(lock_);
            scanNow = scanWanted_;
            scanWanted_ = false;
            scanning_ = scanNow;
        }
        if (scanNow) {
            scan();
            const std::scoped_lock lock(lock_);
            scanning_ = false;
        }
        while (!threadShouldExit()) {
            cydaw::base::File next;
            {
                const std::scoped_lock lock(lock_);
                if (queue_.empty() || scanWanted_)
                    break;
                next = queue_.front();
                queue_.pop_front();
                current_ = next.getFileName();
            }
            analyseNow(formats, next);
            const std::scoped_lock lock(lock_);
            current_ = {};
        }
    }
}

void DjLibrary::scan()
{
    const auto roots = folders();
    std::vector<cydaw::base::File> found;
    for (const auto& root : roots) {
        const cydaw::base::File folder(root);
        if (!folder.isDirectory())
            continue;
        for (const auto& entry : cydaw::base::RangedDirectoryIterator(folder, true, "*", cydaw::base::File::findFiles)) {
            if (threadShouldExit() || static_cast<int>(found.size()) >= maximumScannedFiles)
                break;
            if (isAudioFile(entry.getFile()))
                found.push_back(entry.getFile());
        }
    }
    // The disk is read without the lock (the interface takes it every
    // frame): what is new and what is gone, then both applied at once.
    const auto keyOf = [](const cydaw::base::File& file) {
        return cydaw::base::File::areFileNamesCaseSensitive() ? file.getFullPathName() : file.getFullPathName().toLowerCase();
    };
    std::set<cydaw::base::Text> known;
    std::vector<cydaw::base::File> listed;
    {
        const std::scoped_lock lock(lock_);
        for (const auto& track : tracks_) {
            known.insert(keyOf(track.file));
            listed.push_back(track.file);
        }
    }
    std::vector<DjTrack> fresh;
    for (const auto& file : found) {
        if (threadShouldExit())
            return;
        if (!known.insert(keyOf(file)).second)
            continue;
        DjTrack track;
        track.file = file;
        app::djTrackTags(file, {}, track.title, track.artist);
        track.size = file.getSize();
        track.modified = file.getLastModificationTime().toMilliseconds();
        fresh.push_back(std::move(track));
    }
    // Gone from the disk: gone from the collection.
    std::set<cydaw::base::Text> gone;
    for (const auto& file : listed)
        if (!file.existsAsFile())
            gone.insert(keyOf(file));
    auto added = false;
    {
        const std::scoped_lock lock(lock_);
        std::set<cydaw::base::Text> present;
        for (const auto& track : tracks_)
            present.insert(keyOf(track.file));
        for (auto& track : fresh)
            if (present.insert(keyOf(track.file)).second) {
                tracks_.push_back(std::move(track));
                added = true;
            }
        const auto before = tracks_.size();
        std::erase_if(tracks_, [&gone, &keyOf](const DjTrack& track) { return gone.count(keyOf(track.file)) > 0; });
        added = added || tracks_.size() != before;
    }
    if (added)
        changed();
}

void DjLibrary::analyseNow(cydaw::audio::AudioFormatManager& formats, const cydaw::base::File& file)
{
    cydaw::base::Text error;
    cydaw::base::Text title;
    cydaw::base::Text artist;
    const auto audio = app::DjEngine::decode(formats, file, error, title, artist, [this] { return threadShouldExit(); });
    std::optional<engine::dj::TrackAnalysis> found;
    if (audio != nullptr)
        found = engine::dj::analyse(*audio);
    if (threadShouldExit())
        return;
    {
        const std::scoped_lock lock(lock_);
        auto index = indexOf(file);
        if (index >= tracks_.size()) {
            DjTrack track;
            track.file = file;
            tracks_.push_back(std::move(track));
            index = tracks_.size() - 1;
        }
        auto& track = tracks_[index];
        if (audio == nullptr) {
            track.error = error;
        } else {
            track.error = {};
            track.title = title;
            track.artist = artist;
            track.seconds = audio->seconds();
            track.key = found->key;
            track.loudness = found->loudness;
            if (!track.gridSet) {
                track.bpm = found->bpm;
                track.firstBeat = found->firstBeat;
            }
            track.analysed = true;
        }
        track.size = file.getSize();
        track.modified = file.getLastModificationTime().toMilliseconds();
    }
    changed();
}

cydaw::base::Value DjLibrary::toJson() const
{
    const std::scoped_lock lock(lock_);
    auto* root = new cydaw::base::Object();
    root->setProperty("format", "CyDAW DJ collection");
    root->setProperty("version", 1);
    cydaw::base::Array<cydaw::base::Value> folders;
    for (const auto& folder : folders_)
        folders.add(folder);
    root->setProperty("folders", folders);
    cydaw::base::Array<cydaw::base::Value> list;
    for (const auto& track : tracks_) {
        auto* item = new cydaw::base::Object();
        item->setProperty("path", track.file.getFullPathName());
        item->setProperty("title", track.title);
        item->setProperty("artist", track.artist);
        item->setProperty("seconds", track.seconds);
        item->setProperty("bpm", track.bpm);
        item->setProperty("firstBeat", track.firstBeat);
        item->setProperty("gridSet", track.gridSet);
        item->setProperty("key", track.key);
        item->setProperty("loudness", track.loudness);
        item->setProperty("analysed", track.analysed);
        if (track.error.isNotEmpty())
            item->setProperty("error", track.error);
        item->setProperty("cue", track.cuePoint);
        cydaw::base::Array<cydaw::base::Value> cues;
        for (const auto cue : track.hotCues)
            cues.add(cue);
        item->setProperty("hotCues", cues);
        item->setProperty("size", track.size);
        item->setProperty("modified", track.modified);
        item->setProperty("plays", track.plays);
        item->setProperty("lastPlayed", track.lastPlayed);
        list.add(cydaw::base::Value(item));
    }
    root->setProperty("tracks", list);
    return cydaw::base::Value(root);
}

void DjLibrary::fromJson(const cydaw::base::Value& document)
{
    if (document.getProperty("format", cydaw::base::Value()).toString() != "CyDAW DJ collection")
        return;
    const std::scoped_lock lock(lock_);
    folders_.clear();
    if (const auto* folders = document.getProperty("folders", cydaw::base::Value()).getArray())
        for (const auto& folder : *folders)
            folders_.add(folder.toString());
    tracks_.clear();
    if (const auto* list = document.getProperty("tracks", cydaw::base::Value()).getArray())
        for (const auto& item : *list) {
            DjTrack track;
            track.file = cydaw::base::File(item.getProperty("path", cydaw::base::Value()).toString());
            track.title = item.getProperty("title", cydaw::base::Value()).toString();
            track.artist = item.getProperty("artist", cydaw::base::Value()).toString();
            track.seconds = number(item, "seconds");
            track.bpm = number(item, "bpm");
            track.firstBeat = number(item, "firstBeat");
            track.gridSet = static_cast<bool>(item.getProperty("gridSet", false));
            track.key = static_cast<int>(number(item, "key", -1.0));
            track.loudness = number(item, "loudness", -70.0);
            track.analysed = static_cast<bool>(item.getProperty("analysed", false));
            track.error = item.getProperty("error", cydaw::base::Value()).toString();
            track.cuePoint = number(item, "cue");
            if (const auto* cues = item.getProperty("hotCues", cydaw::base::Value()).getArray())
                for (int i = 0; i < std::min(8, cues->size()); ++i)
                    track.hotCues[static_cast<std::size_t>(i)] = static_cast<double>((*cues)[i]);
            track.size = static_cast<std::int64_t>(number(item, "size"));
            track.modified = static_cast<std::int64_t>(number(item, "modified"));
            track.plays = static_cast<int>(number(item, "plays"));
            track.lastPlayed = static_cast<std::int64_t>(number(item, "lastPlayed"));
            if (track.file != cydaw::base::File())
                tracks_.push_back(std::move(track));
        }
    revision_.fetch_add(1, std::memory_order_acq_rel);
}

} // namespace cydaw::v2
