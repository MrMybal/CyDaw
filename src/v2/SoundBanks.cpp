#include "SoundBanks.h"

#include "Locations.h"
#include "SamplerMapFile.h"
#include "Widgets.h"

#include <cydaw/engine/DrumSynth.h>

#include <algorithm>
#include <cmath>
#include <set>

namespace cydaw::v2 {

namespace {

constexpr int kWorkers = 4;
constexpr auto kLfsPointer = "version https://git-lfs";

// Every byte but the unreserved ones and the slashes, as %XX of its UTF-8.
juce::String escapedPath(const juce::String& path)
{
    juce::String escaped;
    for (const auto* byte = path.toRawUTF8(); *byte != 0; ++byte) {
        const auto c = static_cast<unsigned char>(*byte);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'
            || c == '_' || c == '~' || c == '/')
            escaped << static_cast<char>(c);
        else
            escaped << "%" << juce::String::toHexString(static_cast<int>(c)).paddedLeft('0', 2).toUpperCase();
    }
    return escaped;
}

// A folder there, made if need be: another download thread may make the
// same one, or one of its parents, at the same instant. JUCE makes the
// parents first and gives up when one of them appears under it: tried again,
// what is already there is skipped.
bool makeFolder(const juce::File& folder)
{
    for (auto attempt = 0; attempt < 8 && !folder.isDirectory(); ++attempt)
        (void) folder.createDirectory();
    return folder.isDirectory();
}

bool isLicenceOrReadme(const juce::String& path)
{
    if (path.containsChar('/'))
        return false;
    const auto name = path.toLowerCase();
    return name.startsWith("licen") || name.startsWith("readme") || name.startsWith("copying");
}

// A WAV as a generated bank writes it: 16-bit PCM, mono, a 44-byte header,
// so its size is known before it is made (44 + 2 a frame).
juce::MemoryBlock wavOf(const std::vector<float>& samples, const int rate)
{
    juce::MemoryOutputStream out;
    const auto dataBytes = static_cast<int>(samples.size() * 2);
    out.write("RIFF", 4);
    out.writeInt(36 + dataBytes);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    out.writeInt(16);
    out.writeShort(1);
    out.writeShort(1);
    out.writeInt(rate);
    out.writeInt(rate * 2);
    out.writeShort(2);
    out.writeShort(16);
    out.write("data", 4);
    out.writeInt(dataBytes);
    for (const auto sample : samples)
        out.writeShort(static_cast<short>(std::lround(std::clamp(static_cast<double>(sample), -1.0, 1.0) * 32'767.0)));
    return out.getMemoryBlock();
}

} // namespace

class SoundBanks::Worker final : public juce::Thread {
public:
    explicit Worker(SoundBanks& owner) : juce::Thread("CyDAW bank download"), owner_(owner) {}
    void run() override { owner_.work(*this); }

private:
    SoundBanks& owner_;
};

SoundBanks::SoundBanks() = default;

SoundBanks::~SoundBanks()
{
    stopPreload_ = true;
    if (preloader_.joinable())
        preloader_.join();
    ++generation_;
    for (auto& worker : workers_)
        worker->signalThreadShouldExit();
    for (auto& worker : workers_) {
        wake_.signal();
        worker->stopThread(10'000);
    }
}

// ---- The registry ------------------------------------------------------------------------

juce::File SoundBanks::registryFile() const
{
    if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_BANKS_REGISTRY", {}); other.isNotEmpty())
        return juce::File(other);
    return app::factoryContentFolder().getChildFile("banks.json");
}

void SoundBanks::loadRegistry()
{
    // Not under a download, which reads the banks it was given.
    if (busy())
        return;
    const auto file = registryFile();
    const auto json = juce::JSON::parse(file.loadFileAsString());
    // The maps' files stay known: they do not depend on the list.
    banks_.clear();
    lists_.clear();
    indexes_.clear();
    forgetDisk();
    if (const auto* list = json.getProperty("banks", {}).getArray())
        for (const auto& item : *list) {
            Bank bank;
            bank.id = item.getProperty("id", {}).toString();
            bank.title = item.getProperty("title", bank.id).toString();
            bank.author = item.getProperty("author", {}).toString();
            bank.licence = item.getProperty("licence", {}).toString();
            bank.licenceUrl = item.getProperty("licenceUrl", {}).toString();
            bank.homepage = item.getProperty("homepage", {}).toString();
            bank.repository = item.getProperty("repository", {}).toString();
            bank.commit = item.getProperty("commit", {}).toString();
            bank.baseUrl = item.getProperty("baseUrl", {}).toString();
            bank.generator = item.getProperty("generator", {}).toString();
            bank.rate = juce::jlimit(8'000.0, 192'000.0, static_cast<double>(item.getProperty("rate", 48'000.0)));
            // In the interface's language ({"fr": …, "en": …}), else English.
            if (const auto description = item.getProperty("description", {}); description.isObject())
                bank.description = description.getProperty(juce::Identifier(ui::language()),
                                                           description.getProperty("en", {})).toString();
            else
                bank.description = description.toString();
            bank.list = file.getParentDirectory().getChildFile(item.getProperty("list", {}).toString());
            bank.count = static_cast<int>(item.getProperty("count", 0));
            bank.bytes = static_cast<juce::int64>(item.getProperty("bytes", 0));
            // A bank is downloaded into a folder of its name: nothing that
            // could leave the banks' folder.
            if (bank.id.isNotEmpty() && bank.id.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789-_.")
                && !bank.id.startsWith("."))
                banks_.push_back(std::move(bank));
        }
}

const SoundBanks::Bank* SoundBanks::find(const juce::String& id) const
{
    const auto found = std::ranges::find_if(banks_, [&id](const Bank& bank) { return bank.id == id; });
    return found != banks_.end() ? &*found : nullptr;
}

bool SoundBanks::generatedOnly(const juce::StringArray& ids) const
{
    if (ids.isEmpty())
        return false;
    for (const auto& id : ids)
        if (const auto* bank = find(id); bank == nullptr || bank->generator.isEmpty())
            return false;
    return true;
}

const std::vector<SoundBanks::BankFile>& SoundBanks::filesOf(const juce::String& id)
{
    if (const auto found = lists_.find(id); found != lists_.end())
        return found->second;
    auto& files = lists_[id];
    auto& index = indexes_[id];
    if (const auto* bank = find(id)) {
        // Read ahead on the preloading thread (waited for when it is on it),
        // else here.
        std::optional<PreloadedList> read;
        for (;;) {
            {
                const std::scoped_lock lock(preloadLock_);
                if (const auto found = preloadedLists_.find(id); found != preloadedLists_.end()) {
                    read = std::move(found->second);
                    preloadedLists_.erase(found);
                    break;
                }
                if (!listsWaiting_.contains(id))
                    break;
            }
            juce::Thread::sleep(2);
        }
        if (!read)
            read = readList(bank->list);
        files = std::move(read->files);
        index = std::move(read->index);
    }
    return files;
}

SoundBanks::PreloadedList SoundBanks::readList(const juce::File& listFile)
{
    PreloadedList read;
    const auto json = juce::JSON::parse(listFile.loadFileAsString());
    if (const auto* list = json.getProperty("files", {}).getArray())
        for (const auto& item : *list)
            if (const auto* fields = item.getArray(); fields != nullptr && fields->size() >= 2) {
                BankFile file;
                file.path = (*fields)[0].toString();
                file.size = static_cast<juce::int64>((*fields)[1]);
                file.lfs = fields->size() > 2 && static_cast<int>((*fields)[2]) != 0;
                if (fields->size() > 3)
                    file.recipe = (*fields)[3];
                // Inside the bank only.
                if (file.path.isEmpty() || file.path.contains("..") || juce::File::isAbsolutePath(file.path))
                    continue;
                read.index[file.path] = read.files.size();
                read.files.push_back(std::move(file));
            }
    return read;
}

juce::File SoundBanks::folderOf(const juce::String& id) const
{
    return app::banksFolder().getChildFile(id);
}

void SoundBanks::forgetDisk()
{
    {
        const std::scoped_lock lock(countLock_);
        counted_.clear();
        absent_.clear();
        ++countGeneration_;
    }
    ++diskGeneration_;
}

std::optional<SoundBanks::OnDisk> SoundBanks::onDisk(const juce::String& id)
{
    {
        const std::scoped_lock lock(countLock_);
        if (const auto found = counted_.find(id); found != counted_.end())
            return found->second;
        if (!counting_.insert(id).second)
            return std::nullopt;
    }
    counter_.addJob([this, id, files = filesOf(id), folder = folderOf(id), generation = countGeneration_.load()] {
        OnDisk disk;
        std::set<juce::String> absent;
        const auto there = folder.isDirectory();
        for (const auto& file : files)
            if (const auto found = folder.getChildFile(file.path); there && found.existsAsFile() && found.getSize() == file.size) {
                ++disk.count;
                disk.bytes += file.size;
            } else {
                absent.insert(file.path);
            }
        const std::scoped_lock lock(countLock_);
        counting_.erase(id);
        if (generation == countGeneration_.load()) {
            counted_[id] = disk;
            absent_[id] = std::move(absent);
        }
    });
    return std::nullopt;
}

// ---- What is missing ------------------------------------------------------------------------

void SoundBanks::preloadMaps(std::vector<juce::File> maps)
{
    if (preloader_.joinable())
        return;
    // The banks' lists first (each map's count needs them), then the maps.
    std::vector<std::pair<juce::String, juce::File>> lists;
    for (const auto& bank : banks_)
        if (!lists_.contains(bank.id) && bank.list.existsAsFile())
            lists.emplace_back(bank.id, bank.list);
    {
        const std::scoped_lock lock(preloadLock_);
        for (const auto& map : maps)
            preloadWaiting_.insert(map.getFullPathName());
        for (const auto& [id, file] : lists)
            listsWaiting_.insert(id);
    }
    preloader_ = std::thread([this, maps = std::move(maps), lists = std::move(lists)] {
        juce::Thread::setCurrentThreadName("CyDAW maps");
        for (const auto& [id, file] : lists) {
            if (stopPreload_)
                break;
            auto read = readList(file);
            const std::scoped_lock lock(preloadLock_);
            preloadedLists_[id] = std::move(read);
            listsWaiting_.erase(id);
        }
        {
            // Stopped early: nothing is left waiting for it.
            const std::scoped_lock lock(preloadLock_);
            listsWaiting_.clear();
        }
        for (const auto& map : maps) {
            if (stopPreload_)
                return;
            Preloaded entry;
            entry.stamp = map.getLastModificationTime().toMilliseconds();
            juce::String error;
            entry.paths = pathsOfMap(app::loadSamplerMapFile(map, error));
            const std::scoped_lock lock(preloadLock_);
            preloaded_[map.getFullPathName()] = std::move(entry);
            preloadWaiting_.erase(map.getFullPathName());
        }
    });
}

std::vector<juce::String> SoundBanks::bankPathsOf(const juce::var& mapJson) const
{
    return pathsOfMap(mapJson);
}

std::vector<juce::String> SoundBanks::pathsOfMap(const juce::var& mapJson)
{
    std::vector<juce::String> paths;
    if (const auto* zones = mapJson.getProperty("zones", {}).getArray())
        for (const auto& zone : *zones)
            if (const auto file = zone.getProperty("file", {}).toString(); app::bankOfSamplePath(file).isNotEmpty())
                paths.push_back(file);
    std::ranges::sort(paths);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

SoundBanks::Missing SoundBanks::missingFor(const juce::var& mapJson, const bool exact)
{
    Missing missing;
    for (const auto& stored : bankPathsOf(mapJson)) {
        const auto id = app::bankOfSamplePath(stored);
        const auto path = app::pathInBank(stored);
        (void) filesOf(id);
        const auto& index = indexes_[id];
        const auto found = index.find(path);
        if (found == index.end()) {
            if (!app::resolveSamplePath(stored).existsAsFile())
                missing.unknown.add(stored);
            continue;
        }
        const auto& file = lists_[id][found->second];
        // Exact: the disk, asked now (a file may have gone since the count).
        // Else what the bank's count found, or (the count on its way)
        // nothing said yet.
        std::optional<bool> absent;
        if (!exact) {
            const std::scoped_lock lock(countLock_);
            if (const auto known = absent_.find(id); known != absent_.end())
                absent = known->second.contains(path);
        }
        if (!absent.has_value()) {
            if (!exact) {
                (void) onDisk(id);
                missing.pending = true;
                continue;
            }
            const auto there = folderOf(id).getChildFile(path);
            absent = !(there.existsAsFile() && there.getSize() == file.size);
        }
        if (!*absent)
            continue;
        missing.files.push_back({ id, file });
        missing.bytes += file.size;
        missing.banks.addIfNotAlreadyThere(id);
    }
    return missing;
}

SoundBanks::Missing SoundBanks::missingForFile(const juce::File& mapFile, const bool exact)
{
    // The map read once (and again when it changes): only its files are
    // kept, and what is missing of them until something is downloaded.
    const auto stamp = mapFile.getLastModificationTime().toMilliseconds();
    auto& entry = maps_[mapFile.getFullPathName()];
    if (entry.stamp != stamp || entry.generation == -1) {
        // Read ahead on the preloading thread, or still to be: not here.
        auto read = false;
        {
            const std::scoped_lock lock(preloadLock_);
            if (const auto found = preloaded_.find(mapFile.getFullPathName()); found != preloaded_.end() && found->second.stamp == stamp) {
                entry.paths = std::move(found->second.paths);
                preloaded_.erase(found);
                read = true;
            } else if (!exact && preloadWaiting_.contains(mapFile.getFullPathName())) {
                Missing waiting;
                waiting.pending = true;
                return waiting;
            }
        }
        if (!read) {
            juce::String error;
            entry.paths = bankPathsOf(app::loadSamplerMapFile(mapFile, error));
        }
        entry.stamp = stamp;
        entry.generation = -1;
    }
    if (exact || entry.generation != diskGeneration_) {
        juce::Array<juce::var> zones;
        for (const auto& path : entry.paths) {
            auto* zone = new juce::DynamicObject();
            zone->setProperty("file", path);
            zones.add(juce::var(zone));
        }
        auto* json = new juce::DynamicObject();
        json->setProperty("zones", zones);
        auto missing = missingFor(juce::var(json), exact);
        if (exact)
            return missing;
        // Kept once every bank of it is counted.
        entry.missing = std::move(missing);
        entry.generation = entry.missing.pending ? -2 : diskGeneration_;
    }
    return entry.missing;
}

SoundBanks::Missing SoundBanks::missingOfBank(const juce::String& id)
{
    Missing missing;
    const auto folder = folderOf(id);
    for (const auto& file : filesOf(id))
        if (const auto there = folder.getChildFile(file.path); !there.existsAsFile() || there.getSize() != file.size) {
            missing.files.push_back({ id, file });
            missing.bytes += file.size;
        }
    if (!missing.files.empty())
        missing.banks.add(id);
    return missing;
}

// ---- Downloading ------------------------------------------------------------------------------

void SoundBanks::download(const juce::String& label, const Missing& missing)
{
    if (missing.files.empty())
        return;
    Job job;
    job.label = label;
    job.banks = missing.banks;
    std::set<juce::String> taken;
    for (const auto& wanted : missing.files)
        if (taken.insert(wanted.bank + "/" + wanted.file.path).second)
            job.items.push_back(wanted);
    // Each bank's licence and readme come along.
    for (const auto& id : missing.banks) {
        if (const auto* bank = find(id))
            writeBankRecord(*bank);
        const auto folder = folderOf(id);
        for (const auto& file : filesOf(id))
            if (isLicenceOrReadme(file.path) && !folder.getChildFile(file.path).existsAsFile()
                && taken.insert(id + "/" + file.path).second)
                job.items.push_back({ id, file });
    }
    job.total = static_cast<int>(job.items.size());
    for (const auto& item : job.items)
        job.bytesTotal += item.file.size;
    {
        const std::scoped_lock lock(mutex_);
        jobs_.push_back(std::move(job));
        if (jobs_.size() == 1)
            jobs_.front().started = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }
    if (workers_.empty())
        for (auto count = 0; count < kWorkers; ++count) {
            workers_.push_back(std::make_unique<Worker>(*this));
            workers_.back()->startThread(juce::Thread::Priority::low);
        }
    for (auto count = 0; count < kWorkers; ++count)
        wake_.signal();
}

void SoundBanks::cancel()
{
    const std::scoped_lock lock(mutex_);
    if (jobs_.empty())
        return;
    ++generation_;
    auto& front = jobs_.front();
    front.items.clear();
    front.cancelled = true;
    jobs_.erase(std::next(jobs_.begin()), jobs_.end());
    if (inFlight_ == 0)
        finishFrontJob();
}

bool SoundBanks::busy() const
{
    const std::scoped_lock lock(mutex_);
    return !jobs_.empty();
}

SoundBanks::Progress SoundBanks::progress() const
{
    const std::scoped_lock lock(mutex_);
    Progress progress;
    if (jobs_.empty())
        return progress;
    const auto& job = jobs_.front();
    progress.running = true;
    progress.label = job.label;
    progress.done = job.done;
    progress.total = job.total;
    progress.failed = job.failed;
    progress.queued = static_cast<int>(jobs_.size()) - 1;
    progress.bytesDone = job.bytesDone;
    progress.bytesTotal = job.bytesTotal;
    progress.error = job.error;
    progress.generating = generatedOnly(job.banks);
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() * 0.001 - job.started;
    progress.bytesPerSecond = elapsed > 0.5 ? static_cast<double>(job.bytesDone) / elapsed : 0.0;
    return progress;
}

std::vector<SoundBanks::Finished> SoundBanks::takeFinished()
{
    std::vector<Finished> finished;
    {
        const std::scoped_lock lock(mutex_);
        finished.swap(finished_);
    }
    // What is on disk changed.
    if (!finished.empty())
        forgetDisk();
    return finished;
}

// Called with the lock held, when the front job has nothing left running.
void SoundBanks::finishFrontJob()
{
    auto& job = jobs_.front();
    Finished done;
    done.label = job.label;
    done.banks = job.banks;
    done.files = job.done - job.failed;
    done.failed = job.failed;
    done.cancelled = job.cancelled;
    done.generated = generatedOnly(job.banks);
    done.error = job.error;
    finished_.push_back(std::move(done));
    jobs_.pop_front();
    if (!jobs_.empty())
        jobs_.front().started = juce::Time::getMillisecondCounterHiRes() * 0.001;
}

void SoundBanks::work(juce::Thread& thread)
{
    while (!thread.threadShouldExit()) {
        std::optional<Wanted> item;
        auto generation = 0;
        {
            const std::scoped_lock lock(mutex_);
            if (!jobs_.empty() && !jobs_.front().items.empty()) {
                item = jobs_.front().items.front();
                jobs_.front().items.pop_front();
                generation = generation_.load();
                ++inFlight_;
            }
        }
        if (!item.has_value()) {
            wake_.wait(200);
            continue;
        }
        juce::String error;
        const auto ok = fetch(*item, generation, error);
        const std::scoped_lock lock(mutex_);
        --inFlight_;
        if (jobs_.empty())
            continue;
        auto& job = jobs_.front();
        if (generation == generation_.load()) {
            ++job.done;
            if (!ok) {
                ++job.failed;
                job.error = error;
            }
        }
        if (job.items.empty() && inFlight_ == 0)
            finishFrontJob();
    }
}

juce::String SoundBanks::urlOf(const Bank& bank, const BankFile& file, const bool lfs) const
{
    if (bank.baseUrl.isNotEmpty())
        return bank.baseUrl + (bank.baseUrl.endsWithChar('/') ? "" : "/") + escapedPath(file.path);
    const auto host = lfs ? juce::String("https://media.githubusercontent.com/media/")
                          : juce::String("https://raw.githubusercontent.com/");
    return host + bank.repository + "/" + bank.commit + "/" + escapedPath(file.path);
}

bool SoundBanks::fetch(const Wanted& wanted, const int generation, juce::String& error)
{
    const auto stopped = [this, generation] { return generation != generation_.load(); };
    const auto addBytes = [this, generation](const juce::int64 bytes) {
        const std::scoped_lock lock(mutex_);
        if (!jobs_.empty() && generation == generation_.load())
            jobs_.front().bytesDone += bytes;
    };
    std::optional<Bank> bank;
    if (const auto* found = find(wanted.bank))
        bank = *found;
    if (!bank.has_value()) {
        error = "unknown bank " + wanted.bank;
        return false;
    }
    const auto target = folderOf(wanted.bank).getChildFile(wanted.file.path);
    if (target.existsAsFile() && target.getSize() == wanted.file.size) {
        addBytes(wanted.file.size);
        return true;
    }
    if (bank->generator.isNotEmpty())
        return generate(*bank, wanted, generation, error);
    const auto partial = locations::folder(Location::downloads)
                             .getChildFile(wanted.bank)
                             .getChildFile(wanted.file.path + ".part");
    if (!makeFolder(partial.getParentDirectory())) {
        error = "cannot make " + partial.getParentDirectory().getFullPathName();
        return false;
    }
    auto lfs = wanted.file.lfs;
    for (auto attempt = 0; attempt < 3; ++attempt) {
        if (stopped())
            return false;
        if (attempt > 0)
            juce::Thread::sleep(1'000 * attempt);
        auto status = 0;
        const auto url = urlOf(*bank, wanted.file, lfs);
        auto stream = juce::URL(url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                           .withConnectionTimeoutMs(30'000)
                                                           .withNumRedirectsToFollow(5)
                                                           .withStatusCode(&status));
        if (stream == nullptr || status >= 400) {
            error = wanted.bank + "/" + wanted.file.path + ": HTTP " + juce::String(status);
            continue;
        }
        juce::int64 written {};
        {
            (void) partial.deleteFile();
            juce::FileOutputStream out(partial);
            if (!out.openedOk()) {
                error = "cannot write " + partial.getFullPathName();
                return false;
            }
            juce::HeapBlock<char> buffer(1 << 16);
            for (;;) {
                if (stopped()) {
                    out.flush();
                    break;
                }
                const auto read = stream->read(buffer.get(), 1 << 16);
                if (read <= 0)
                    break;
                if (!out.write(buffer.get(), static_cast<std::size_t>(read))) {
                    error = "cannot write " + partial.getFullPathName();
                    break;
                }
                written += read;
                addBytes(read);
            }
        }
        if (stopped()) {
            (void) partial.deleteFile();
            return false;
        }
        if (wanted.file.size > 0 && written != wanted.file.size) {
            // A Git LFS pointer where the file was expected: from LFS then.
            if (!lfs && partial.loadFileAsString().startsWith(kLfsPointer))
                lfs = true;
            addBytes(-written);
            error = wanted.bank + "/" + wanted.file.path + ": " + juce::String(written) + " bytes of "
                    + juce::String(wanted.file.size);
            continue;
        }
        if (!makeFolder(target.getParentDirectory()) || !partial.moveFileTo(target)) {
            error = "cannot move into " + target.getFullPathName();
            (void) partial.deleteFile();
            return false;
        }
        return true;
    }
    (void) partial.deleteFile();
    return false;
}

bool SoundBanks::generate(const Bank& bank, const Wanted& wanted, const int generation, juce::String& error)
{
    const auto& recipe = wanted.file.recipe;
    const auto name = wanted.bank + "/" + wanted.file.path;
    juce::MemoryBlock bytes;
    if (recipe.hasProperty("text")) {
        const auto text = recipe.getProperty("text", {}).toString();
        bytes.append(text.toRawUTF8(), text.getNumBytesAsUTF8());
    } else if (bank.generator == "cydaw-drums") {
        engine::drums::Recipe drum;
        drum.kind = recipe.getProperty("kind", {}).toString().toStdString();
        if (const auto* values = recipe.getProperty("values", {}).getDynamicObject())
            for (const auto& property : values->getProperties())
                drum.values.emplace_back(property.name.toString().toStdString(), static_cast<double>(property.value));
        drum.seed = static_cast<std::uint32_t>(static_cast<juce::int64>(recipe.getProperty("seed", 1)));
        drum.seconds = static_cast<double>(recipe.getProperty("seconds", 0.0));
        drum.gainDb = static_cast<double>(recipe.getProperty("gainDb", 0.0));
        const auto samples = engine::drums::render(drum, bank.rate);
        if (samples.empty()) {
            error = name + ": no recipe for \"" + juce::String(drum.kind) + "\"";
            return false;
        }
        bytes = wavOf(samples, static_cast<int>(std::lround(bank.rate)));
    } else {
        error = name + ": unknown generator " + bank.generator;
        return false;
    }
    // What the list says it is: else this version makes another sound.
    if (wanted.file.size > 0 && static_cast<juce::int64>(bytes.getSize()) != wanted.file.size) {
        error = name + ": made " + juce::String(static_cast<juce::int64>(bytes.getSize())) + " bytes of "
                + juce::String(wanted.file.size);
        return false;
    }
    if (generation != generation_.load())
        return false;
    const auto target = folderOf(wanted.bank).getChildFile(wanted.file.path);
    const auto partial = locations::folder(Location::downloads)
                             .getChildFile(wanted.bank)
                             .getChildFile(wanted.file.path + ".part");
    if (!makeFolder(partial.getParentDirectory()) || !partial.replaceWithData(bytes.getData(), bytes.getSize())) {
        error = "cannot write " + partial.getFullPathName();
        return false;
    }
    if (!makeFolder(target.getParentDirectory()) || !partial.moveFileTo(target)) {
        error = "cannot move into " + target.getFullPathName();
        (void) partial.deleteFile();
        return false;
    }
    const std::scoped_lock lock(mutex_);
    if (!jobs_.empty() && generation == generation_.load())
        jobs_.front().bytesDone += static_cast<juce::int64>(bytes.getSize());
    return true;
}

void SoundBanks::writeBankRecord(const Bank& bank) const
{
    const auto record = folderOf(bank.id).getChildFile("bank.json");
    if (record.existsAsFile())
        return;
    auto* json = new juce::DynamicObject();
    json->setProperty("id", bank.id);
    json->setProperty("title", bank.title);
    json->setProperty("author", bank.author);
    json->setProperty("licence", bank.licence);
    json->setProperty("licenceUrl", bank.licenceUrl);
    json->setProperty("repository", bank.repository.isNotEmpty() ? "https://github.com/" + bank.repository : bank.baseUrl);
    json->setProperty("commit", bank.commit);
    if (bank.generator.isNotEmpty())
        json->setProperty("generator", bank.generator);
    json->setProperty("fetched", juce::Time::getCurrentTime().formatted("%Y-%m-%d"));
    (void) record.getParentDirectory().createDirectory();
    (void) record.replaceWithText(juce::JSON::toString(juce::var(json)));
}

bool SoundBanks::remove(const juce::String& id, juce::String& error)
{
    if (busy()) {
        error = "a download is running";
        return false;
    }
    const auto folder = folderOf(id);
    if (find(id) == nullptr || !folder.isDirectory() || folder == app::banksFolder()) {
        error = "nothing downloaded for " + id;
        return false;
    }
    if (!folder.moveToTrash() && !folder.deleteRecursively()) {
        error = "cannot remove " + folder.getFullPathName();
        return false;
    }
    forgetDisk();
    return true;
}

} // namespace cydaw::v2
