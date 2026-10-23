#include "SamplerMapFile.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>

namespace cydaw::app {

using cydaw::engine::SampleData;
using cydaw::engine::SampleDataRef;
using cydaw::engine::SamplerMap;
using cydaw::engine::SamplerMapRef;
using cydaw::engine::SamplerZone;

// ---- Paths ------------------------------------------------------------------------

namespace {

constexpr auto factoryPrefix = "factory:";
constexpr auto bankPrefix = "bank:";

std::mutex& factoryLock()
{
    static std::mutex lock;
    return lock;
}

juce::File& factoryOverride()
{
    static juce::File folder;
    return folder;
}

juce::File& banksOverride()
{
    static juce::File folder;
    return folder;
}

} // namespace

juce::File factoryContentFolder()
{
    {
        const std::scoped_lock lock(factoryLock());
        if (factoryOverride().isDirectory())
            return factoryOverride();
    }
    const auto beside = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                            .getParentDirectory()
                            .getChildFile("Factory");
    if (beside.isDirectory())
        return beside;
#ifdef CYDAW_FACTORY_CONTENT_DIR
    if (const juce::File source(juce::String::fromUTF8(CYDAW_FACTORY_CONTENT_DIR)); source.isDirectory())
        return source;
#endif
    return beside;
}

void setFactoryContentFolder(const juce::File& folder)
{
    const std::scoped_lock lock(factoryLock());
    factoryOverride() = folder;
}

juce::File banksFolder()
{
    {
        const std::scoped_lock lock(factoryLock());
        if (banksOverride() != juce::File {})
            return banksOverride();
    }
    if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_BANKS_DIR", {}); other.isNotEmpty())
        return juce::File(other);
   #if JUCE_WINDOWS
    const auto base = juce::File::getSpecialLocation(juce::File::windowsLocalAppData);
   #else
    const auto base = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #endif
    return base.getChildFile("CyDAW").getChildFile("Banks");
}

void setBanksFolder(const juce::File& folder)
{
    const std::scoped_lock lock(factoryLock());
    banksOverride() = folder;
}

juce::File resolveSamplePath(const juce::String& stored)
{
    if (stored.startsWith(factoryPrefix))
        return factoryContentFolder().getChildFile(stored.substring(juce::String(factoryPrefix).length()));
    if (stored.startsWith(bankPrefix))
        return banksFolder().getChildFile(stored.substring(juce::String(bankPrefix).length()));
    if (juce::File::isAbsolutePath(stored))
        return juce::File(stored);
    return factoryContentFolder().getChildFile(stored);
}

juce::String storedSamplePath(const juce::File& file)
{
    const auto factory = factoryContentFolder();
    if (factory.isDirectory() && file.isAChildOf(factory))
        return factoryPrefix + file.getRelativePathFrom(factory).replaceCharacter('\\', '/');
    if (const auto banks = banksFolder(); banks.isDirectory() && file.isAChildOf(banks))
        return bankPrefix + file.getRelativePathFrom(banks).replaceCharacter('\\', '/');
    return file.getFullPathName();
}

juce::String bankOfSamplePath(const juce::String& stored)
{
    if (!stored.startsWith(bankPrefix))
        return {};
    return stored.substring(juce::String(bankPrefix).length()).upToFirstOccurrenceOf("/", false, false);
}

juce::String pathInBank(const juce::String& stored)
{
    if (!stored.startsWith(bankPrefix))
        return {};
    return stored.substring(juce::String(bankPrefix).length()).fromFirstOccurrenceOf("/", false, false);
}

// ---- Older maps --------------------------------------------------------------------------

namespace {

// Factory/Instruments/legacy.json, read once per factory folder:
//   { "factory:Instruments/Kawai Grand/C4_2.flac":
//       { "file": "bank:vcsl/…/C4_v2.wav", "offset": 1234, "frames": 98765, "endFade": 2880 }, … }
juce::var legacyFactoryFiles()
{
    static std::mutex lock;
    static juce::File read;
    static juce::var table;
    const auto file = factoryContentFolder().getChildFile("Instruments").getChildFile("legacy.json");
    const std::scoped_lock hold(lock);
    if (file != read) {
        read = file;
        table = juce::JSON::parse(file.loadFileAsString());
    }
    return table;
}

} // namespace

juce::var upgradedSamplerMap(const juce::var& json)
{
    const auto* zones = json.getProperty("zones", {}).getArray();
    if (zones == nullptr)
        return json;
    const auto stale = [](const juce::var& zone) {
        const auto file = zone.getProperty("file", {}).toString();
        return file.startsWith(factoryPrefix) && !resolveSamplePath(file).existsAsFile();
    };
    if (std::ranges::none_of(*zones, stale))
        return json;
    const auto table = legacyFactoryFiles();
    if (!table.isObject())
        return json;
    auto copy = juce::JSON::parse(juce::JSON::toString(json, true));
    if (auto* list = copy.getProperty("zones", {}).getArray())
        for (auto& zone : *list) {
            auto* object = zone.getDynamicObject();
            if (object == nullptr || !stale(zone))
                continue;
            const auto entry = table.getProperty(juce::Identifier(zone.getProperty("file", {}).toString()), {});
            if (!entry.isObject())
                continue;
            // Frames of the cut file are frames of the recording, `offset` later.
            const auto offset = static_cast<juce::int64>(entry.getProperty("offset", 0));
            const auto frames = static_cast<juce::int64>(entry.getProperty("frames", 0));
            const auto start = static_cast<juce::int64>(zone.getProperty("start", 0));
            const auto end = static_cast<juce::int64>(zone.getProperty("end", -1));
            object->setProperty("file", entry.getProperty("file", {}));
            object->setProperty("start", offset + start);
            object->setProperty("end", offset + (end >= 0 ? std::min(end, frames) : frames));
            if (!object->hasProperty("endFade"))
                object->setProperty("endFade", entry.getProperty("endFade", 0));
            if (const auto* loop = zone.getProperty("loop", {}).getArray(); loop != nullptr && loop->size() == 2) {
                juce::Array<juce::var> moved;
                moved.add(offset + static_cast<juce::int64>((*loop)[0]));
                moved.add(offset + static_cast<juce::int64>((*loop)[1]));
                object->setProperty("loop", moved);
            }
        }
    return copy;
}

// ---- Decoding -------------------------------------------------------------------------

namespace {

// What a long recording keeps in memory: this long from where its zone
// starts (and through its loop); the rest is read from the disk as it plays.
constexpr double headSeconds = 0.6;
// A recording this much longer than its head is read from the disk.
constexpr double streamedPast = 2.5;
// The rings there are for every track (a lane playing past a head takes one).
constexpr int streamCount = 320;

[[nodiscard]] bool streamingAllowed()
{
    static const auto allowed = juce::SystemStats::getEnvironmentVariable("CYDAW_SAMPLE_STREAMING", "1") != "0";
    return allowed;
}

} // namespace

// `first` below 0: the whole file. Else, a recording long enough past
// `first` keeps only its head in memory (from `first`, through `until`).
bool decodeSampleWindow(const juce::File& file, SampleData& data, juce::String& error, const juce::int64 first,
                        const juce::int64 until)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr) {
        error = "cannot read " + file.getFullPathName();
        return false;
    }
    const auto channels = std::clamp(static_cast<int>(reader->numChannels), 1, 2);
    // Twenty minutes is more than a sample; past it, the rest is left out.
    const auto limit = static_cast<juce::int64>(reader->sampleRate * 60.0 * 20.0);
    const auto frames = std::min<juce::int64>(reader->lengthInSamples, limit);
    if (frames <= 0 || reader->sampleRate <= 0.0) {
        error = "empty " + file.getFullPathName();
        return false;
    }
    // Long past where it starts: its head only.
    juce::int64 from = 0;
    auto to = frames;
    auto streamed = false;
    if (first >= 0 && streamingAllowed()) {
        const auto head = static_cast<juce::int64>(reader->sampleRate * headSeconds);
        const auto kept = std::max(first + head, until + 64);
        if (frames - kept > static_cast<juce::int64>(reader->sampleRate * streamedPast)) {
            from = std::clamp<juce::int64>(first - 64, 0, frames);
            to = std::clamp<juce::int64>(kept, from + 1, frames);
            streamed = true;
            cydaw::engine::SampleStreams::instance().reserve(streamCount);
        }
    }
    const auto count = to - from;
    std::vector<float> decoded(static_cast<std::size_t>(count) * static_cast<std::size_t>(channels));
    constexpr int chunk = 1 << 16;
    juce::AudioBuffer<float> buffer(channels, chunk);
    auto peak = 0.0f;
    for (juce::int64 done = 0; done < count; done += chunk) {
        const auto part = static_cast<int>(std::min<juce::int64>(chunk, count - done));
        buffer.clear();
        if (!reader->read(&buffer, 0, part, from + done, true, channels > 1)) {
            error = "cannot decode " + file.getFullPathName();
            return false;
        }
        for (int channel = 0; channel < channels; ++channel) {
            const auto* source = buffer.getReadPointer(channel);
            for (int frame = 0; frame < part; ++frame) {
                const auto value = std::isfinite(source[frame]) ? source[frame] : 0.0f;
                decoded[static_cast<std::size_t>(done + frame) * static_cast<std::size_t>(channels)
                        + static_cast<std::size_t>(channel)] = value;
                peak = std::max(peak, std::abs(value));
            }
        }
    }
    // Normalised into sixteen bits, the level kept in the scale.
    const auto scale = peak > 0.0f ? peak / 32'767.0f : 1.0f / 32'767.0f;
    data.pcm.resize(decoded.size());
    for (std::size_t index = 0; index < decoded.size(); ++index)
        data.pcm[index] = static_cast<std::int16_t>(std::clamp(std::lround(decoded[index] / scale), -32'767L, 32'767L));
    data.channels = channels;
    data.frames = frames;
    data.streamed = streamed;
    data.heldFirst = from;
    data.heldFrames = count;
    data.sampleRate = reader->sampleRate;
    data.scale = scale;
    data.path = file.getFullPathName().toStdString();
    // A loop written in the file (a WAV's sampler chunk).
    const auto loopStart = reader->metadataValues.getValue("Loop0Start", "-1").getLargeIntValue();
    const auto loopEnd = reader->metadataValues.getValue("Loop0End", "-1").getLargeIntValue();
    data.fileLoopStart = loopStart >= 0 && loopEnd > loopStart && loopEnd <= frames ? loopStart : -1;
    data.fileLoopEnd = data.fileLoopStart >= 0 ? loopEnd : -1;
    return true;
}

bool decodeSampleFile(const juce::File& file, SampleData& data, juce::String& error)
{
    return decodeSampleWindow(file, data, error, -1, -1);
}

// ---- The store --------------------------------------------------------------------------

// Several threads decode at once (a piano is dozens of files): the queue is
// shared, a file is taken by one of them.
struct SampleStore::Impl final {
    struct Worker final : juce::Thread {
        Worker(Impl& owner, const int index) : juce::Thread("cydaw-sample-loader-" + juce::String(index)), owner_(owner) {}
        void run() override { owner_.work(*this); }
        Impl& owner_;
    };

    // A file to decode, and the part of it a zone needs in memory.
    struct Job {
        juce::File file;
        std::weak_ptr<SampleData> data;
        juce::int64 first { -1 };
        juce::int64 until { -1 };
    };

    // Fills the rings of the lanes playing past a head, from the disk.
    struct Streamer final : juce::Thread {
        Streamer() : juce::Thread("cydaw-sample-streamer") {}
        void run() override;
    };

    Impl()
    {
        const auto count = juce::jlimit(1, 6, juce::SystemStats::getNumCpus() / 2);
        for (int index = 0; index < count; ++index) {
            workers.push_back(std::make_unique<Worker>(*this, index));
            workers.back()->startThread(juce::Thread::Priority::low);
        }
    }

    ~Impl()
    {
        for (auto& worker : workers)
            worker->signalThreadShouldExit();
        for (auto& worker : workers) {
            wake.signal();
            worker->stopThread(5'000);
        }
        streamer.stopThread(5'000);
    }

    void startStreamer()
    {
        std::call_once(streamerStarted, [this] { streamer.startThread(juce::Thread::Priority::high); });
    }

    SampleDataRef acquire(const juce::File& file, const bool deferred, const juce::int64 first, const juce::int64 until)
    {
        auto key = file.getFullPathName() + "|" + juce::String(file.getLastModificationTime().toMilliseconds())
                   + "|" + juce::String(file.getSize());
        // A long recording keeps the part its zone starts with: one per part.
        if (first >= 0)
            key << "|" << first << "|" << until;
        const std::scoped_lock hold(lock);
        if (const auto found = cache.find(key); found != cache.end())
            if (auto alive = found->second.lock()) {
                // Asked for now, at once.
                if (!deferred && alive->asleep()) {
                    alive->request();
                    takeRequested();
                }
                return alive;
            }
        auto data = std::make_shared<SampleData>();
        data->path = file.getFullPathName().toStdString();
        cache[key] = data;
        if (!file.existsAsFile()) {
            data->failed.store(true);
            return data;
        }
        if (deferred) {
            data->deferred.store(true, std::memory_order_release);
            sleeping.push_back({ file, data, first, until });
            return data;
        }
        queue.push_back({ file, data, first, until });
        wake.signal();
        return data;
    }

    // The deferred samples asked for since the last look go to the queue.
    // Under the lock.
    void takeRequested()
    {
        const auto requests = cydaw::engine::sampleRequests.load(std::memory_order_acquire);
        if (requests == requestsSeen)
            return;
        requestsSeen = requests;
        auto moved = false;
        for (auto entry = sleeping.begin(); entry != sleeping.end();) {
            const auto data = entry->data.lock();
            if (data == nullptr) {
                entry = sleeping.erase(entry);
            } else if (data->wanted.load(std::memory_order_acquire)) {
                queue.push_back(*entry);
                entry = sleeping.erase(entry);
                moved = true;
            } else {
                ++entry;
            }
        }
        if (moved)
            wake.signal();
    }

    void work(juce::Thread& thread)
    {
        while (!thread.threadShouldExit()) {
            Job job;
            SampleDataRef next;
            auto more = false;
            {
                const std::scoped_lock hold(lock);
                takeRequested();
                while (!queue.empty() && next == nullptr) {
                    job = queue.front();
                    next = job.data.lock();
                    queue.pop_front();
                }
                more = !queue.empty();
                // Forget what nothing plays any more.
                for (auto entry = cache.begin(); entry != cache.end();)
                    entry = entry->second.expired() ? cache.erase(entry) : std::next(entry);
            }
            // Others to decode: another thread takes the next one.
            if (more)
                wake.signal();
            if (next == nullptr) {
                // A note may ask for a deferred sample any time: looked for
                // every 20 ms.
                wake.wait(20);
                continue;
            }
            juce::String error;
            if (decodeSampleWindow(job.file, *next, error, job.first, job.until)) {
                if (next->streamed)
                    startStreamer();
                next->ready.store(true, std::memory_order_release);
            } else {
                next->failed.store(true, std::memory_order_release);
            }
        }
    }

    std::mutex lock;
    std::map<juce::String, std::weak_ptr<SampleData>> cache;
    std::deque<Job> queue;
    // Deferred, not asked for yet.
    std::vector<Job> sleeping;
    std::uint32_t requestsSeen {};
    juce::WaitableEvent wake;
    std::vector<std::unique_ptr<Worker>> workers;
    Streamer streamer;
    std::once_flag streamerStarted;
};

// Every ring in turn: a lane's new one gets its file opened, a filling one
// its next frames (ahead of where the lane reads, or behind it played
// backwards), a let-go one closed and freed.
void SampleStore::Impl::Streamer::run()
{
    using cydaw::engine::SampleStream;
    constexpr int chunk = 2'048;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto& pool = cydaw::engine::SampleStreams::instance();
    std::vector<std::unique_ptr<juce::AudioFormatReader>> readers;
    juce::AudioBuffer<float> buffer(2, chunk);
    const auto fill = [&buffer](SampleStream& stream, juce::AudioFormatReader& reader) {
        const auto frames = stream.sample != nullptr ? stream.sample->frames : 0;
        const auto read = stream.reading.load(std::memory_order_acquire);
        juce::int64 first {};
        int count {};
        if (stream.direction > 0) {
            first = stream.high.load(std::memory_order_relaxed);
            const auto limit = std::min<juce::int64>(frames, read + SampleStream::capacity - SampleStream::margin);
            count = static_cast<int>(std::min<juce::int64>(chunk, limit - first));
        } else {
            const auto low = stream.low.load(std::memory_order_relaxed);
            const auto limit = std::max<juce::int64>(0, read - SampleStream::capacity + SampleStream::margin);
            count = static_cast<int>(std::min<juce::int64>(chunk, low - limit));
            first = low - count;
        }
        if (count <= 0)
            return false;
        buffer.clear();
        if (!reader.read(&buffer, 0, count, first, true, true))
            return false;
        const auto* left = buffer.getReadPointer(0);
        const auto* right = reader.numChannels > 1 ? buffer.getReadPointer(1) : left;
        const auto toShort = [](const float value) {
            return static_cast<std::int16_t>(std::lround(std::clamp(std::isfinite(value) ? value : 0.0f, -1.0f, 1.0f) * 32'767.0f));
        };
        for (int frame = 0; frame < count; ++frame) {
            auto* cell = stream.cell(first + frame);
            cell[0] = toShort(left[frame]);
            cell[1] = toShort(right[frame]);
        }
        // The ring holds the last `capacity` frames written; those it lost
        // are behind the lane by `margin` at least (it is filled no further).
        if (stream.direction > 0) {
            const auto high = first + count;
            stream.low.store(std::max<std::int64_t>(stream.low.load(std::memory_order_relaxed), high - SampleStream::capacity),
                             std::memory_order_release);
            stream.high.store(high, std::memory_order_release);
        } else {
            stream.high.store(std::min<std::int64_t>(stream.high.load(std::memory_order_relaxed), first + SampleStream::capacity),
                              std::memory_order_release);
            stream.low.store(first, std::memory_order_release);
        }
        return true;
    };
    while (!threadShouldExit()) {
        const auto count = pool.size();
        if (static_cast<int>(readers.size()) < count)
            readers.resize(static_cast<std::size_t>(count));
        auto busy = false;
        for (int index = 0; index < count; ++index) {
            auto& stream = pool.at(index);
            auto& reader = readers[static_cast<std::size_t>(index)];
            auto state = stream.state.load(std::memory_order_acquire);
            if (state == SampleStream::wanted) {
                reader.reset(stream.sample != nullptr ? formats.createReaderFor(juce::File(juce::String::fromUTF8(stream.sample->path.c_str())))
                                                      : nullptr);
                // Not readable: said so, an offline render does not wait for it.
                stream.unreadable.store(reader == nullptr, std::memory_order_release);
                auto expected = static_cast<int>(SampleStream::wanted);
                state = stream.state.compare_exchange_strong(expected, SampleStream::filling, std::memory_order_acq_rel)
                            ? static_cast<int>(SampleStream::filling)
                            : expected;
            }
            if (state == SampleStream::filling && reader != nullptr)
                busy = fill(stream, *reader) || busy;
            if (state == SampleStream::letGo) {
                reader.reset();
                stream.sample.reset();
                auto expected = static_cast<int>(SampleStream::letGo);
                (void) stream.state.compare_exchange_strong(expected, SampleStream::free, std::memory_order_acq_rel);
            }
        }
        if (!busy)
            wait(2);
    }
}

JUCE_IMPLEMENT_SINGLETON(SampleStore)

SampleStore& SampleStore::instance()
{
    return *getInstance();
}

SampleStore::SampleStore() : impl_(std::make_unique<Impl>()) {}

SampleStore::~SampleStore()
{
    impl_.reset();
    clearSingletonInstance();
}

SampleDataRef SampleStore::acquire(const juce::File& file, const bool deferred, const juce::int64 first,
                                   const juce::int64 until)
{
    return impl_->acquire(file, deferred, first, until);
}

void SampleStore::want(const SamplerMap& map, const cydaw::engine::SamplerCues& cues)
{
    auto asked = false;
    for (const auto& zone : map.zones)
        if (zone.sample != nullptr && zone.sample->asleep() && cydaw::engine::samplerZoneReachable(map, zone, cues)) {
            zone.sample->request();
            asked = true;
        }
    if (asked) {
        const std::scoped_lock hold(impl_->lock);
        impl_->takeRequested();
    }
}

bool SampleStore::waitUntilReady(const SamplerMap& map, const int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    for (;;) {
        auto pending = false;
        for (const auto& zone : map.zones)
            if (zone.sample != nullptr && !zone.sample->asleep() && !zone.sample->ready.load(std::memory_order_acquire)
                && !zone.sample->failed.load(std::memory_order_acquire))
                pending = true;
        if (!pending)
            return std::ranges::all_of(map.zones, [](const SamplerZone& zone) {
                return zone.sample != nullptr && (zone.sample->asleep() || zone.sample->ready.load(std::memory_order_acquire));
            });
        if (juce::Time::getMillisecondCounterHiRes() > deadline)
            return false;
        juce::Thread::sleep(5);
    }
}

SampleStore::Stats SampleStore::stats()
{
    Stats stats;
    const std::scoped_lock hold(impl_->lock);
    for (const auto& [key, weak] : impl_->cache)
        if (const auto data = weak.lock()) {
            if (data->ready.load(std::memory_order_acquire)) {
                ++stats.loaded;
                stats.bytes += static_cast<juce::int64>(data->pcm.size() * sizeof(std::int16_t));
            } else if (data->failed.load(std::memory_order_acquire)) {
                ++stats.failed;
            } else if (data->asleep()) {
                ++stats.deferred;
            } else {
                ++stats.pending;
            }
            if (data->ready.load(std::memory_order_acquire) && data->streamed)
                ++stats.streamed;
        }
    return stats;
}

// ---- Names and pitch ------------------------------------------------------------------------

int noteFromName(const juce::String& name)
{
    static constexpr int pitchClass[] = { 9, 11, 0, 2, 4, 5, 7 }; // A..G
    const auto text = name.toStdString();
    const auto isLetter = [](const char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    const auto isDigit = [](const char c) { return c >= '0' && c <= '9'; };
    // Upper-case names first: "a1" may be a word, "A1" rarely is.
    for (const auto upperOnly : { true, false }) {
        for (std::size_t index = 0; index < text.size(); ++index) {
            const auto c = text[index];
            const auto upper = c >= 'A' && c <= 'G';
            const auto lower = c >= 'a' && c <= 'g';
            if (!(upper || (!upperOnly && lower)))
                continue;
            if (index > 0 && isLetter(text[index - 1]))
                continue;
            auto at = index + 1;
            auto accidental = 0;
            if (at < text.size() && (text[at] == '#' || text[at] == 's')) {
                accidental = 1;
                ++at;
            } else if (at + 1 < text.size() && text[at] == 'b' && (isDigit(text[at + 1]) || text[at + 1] == '-')) {
                accidental = -1;
                ++at;
            }
            auto negative = false;
            if (at < text.size() && text[at] == '-') {
                negative = true;
                ++at;
            }
            if (at >= text.size() || !isDigit(text[at]))
                continue;
            const auto octave = (text[at] - '0') * (negative ? -1 : 1);
            ++at;
            if (at < text.size() && isDigit(text[at]))
                continue;
            const auto letter = (upper ? c : static_cast<char>(c - 'a' + 'A')) - 'A';
            const auto note = 12 * (octave + 1) + pitchClass[letter] + accidental;
            if (note >= 0 && note <= 127)
                return note;
        }
    }
    // A MIDI number standing on its own: "_60_", "-72.".
    juce::StringArray tokens;
    tokens.addTokens(name, "_- .()[]", "");
    for (const auto& token : tokens)
        if (token.containsOnly("0123456789") && token.length() >= 2 && token.length() <= 3) {
            const auto note = token.getIntValue();
            if (note >= 12 && note <= 120)
                return note;
        }
    return -1;
}

namespace {

// The mono mix of a decoded sample from `start`, `count` frames.
std::vector<float> monoSection(const SampleData& data, const std::int64_t start, const std::int64_t count)
{
    // What is in memory of it (a long recording: its head).
    std::vector<float> mono;
    const auto end = std::min(data.heldEnd(), start + count);
    if (start < data.heldStart() || start >= end)
        return mono;
    mono.reserve(static_cast<std::size_t>(end - start));
    for (auto frame = start; frame < end; ++frame) {
        auto sum = 0.0f;
        for (int channel = 0; channel < data.channels; ++channel)
            sum += static_cast<float>(data.held(frame, channel));
        mono.push_back(sum * data.scale / static_cast<float>(data.channels));
    }
    return mono;
}

// The frame of the loudest instant.
std::int64_t peakFrame(const SampleData& data)
{
    auto best = 0;
    std::int64_t at = data.heldStart();
    for (auto frame = data.heldStart(); frame < data.heldEnd(); ++frame)
        for (int channel = 0; channel < data.channels; ++channel) {
            const auto value = std::abs(static_cast<int>(data.held(frame, channel)));
            if (value > best) {
                best = value;
                at = frame;
            }
        }
    return at;
}

// Root-mean-square level of the first half second from the loudest instant.
double loudnessOf(const SampleData& data)
{
    const auto section = monoSection(data, peakFrame(data), static_cast<std::int64_t>(data.sampleRate * 0.5));
    if (section.empty())
        return 0.0;
    auto sum = 0.0;
    for (const auto value : section)
        sum += static_cast<double>(value) * value;
    return std::sqrt(sum / static_cast<double>(section.size()));
}

} // namespace

double detectSamplePitch(const SampleData& data)
{
    if (!data.ready.load(std::memory_order_acquire) || data.frames <= 0)
        return -1.0;
    // YIN over a stretch just after the attack: lags down to 21 Hz.
    const auto rate = data.sampleRate;
    const auto window = static_cast<int>(std::clamp(rate * 0.09, 2'048.0, 8'192.0));
    const auto longestLag = static_cast<int>(std::min(rate / 21.0, static_cast<double>(window)));
    const auto shortestLag = std::max(2, static_cast<int>(rate / 4'200.0));
    const auto start = peakFrame(data) + static_cast<std::int64_t>(rate * 0.03);
    auto section = monoSection(data, start, window + longestLag + 2);
    if (static_cast<int>(section.size()) < window + longestLag + 2) {
        section = monoSection(data, data.heldStart(), window + longestLag + 2);
        if (static_cast<int>(section.size()) < window + longestLag + 2)
            return -1.0;
    }
    std::vector<double> difference(static_cast<std::size_t>(longestLag + 1), 0.0);
    for (int lag = 1; lag <= longestLag; ++lag) {
        auto sum = 0.0;
        for (int index = 0; index < window; ++index) {
            const auto delta = static_cast<double>(section[static_cast<std::size_t>(index)])
                               - section[static_cast<std::size_t>(index + lag)];
            sum += delta * delta;
        }
        difference[static_cast<std::size_t>(lag)] = sum;
    }
    std::vector<double> normalised(difference.size(), 1.0);
    auto running = 0.0;
    for (int lag = 1; lag <= longestLag; ++lag) {
        running += difference[static_cast<std::size_t>(lag)];
        normalised[static_cast<std::size_t>(lag)] = running > 0.0
            ? difference[static_cast<std::size_t>(lag)] * lag / running : 1.0;
    }
    auto chosen = -1;
    for (int lag = shortestLag; lag < longestLag; ++lag)
        if (normalised[static_cast<std::size_t>(lag)] < 0.12) {
            while (lag + 1 < longestLag && normalised[static_cast<std::size_t>(lag + 1)] < normalised[static_cast<std::size_t>(lag)])
                ++lag;
            chosen = lag;
            break;
        }
    if (chosen < 0) {
        auto best = 0.3;
        for (int lag = shortestLag; lag < longestLag; ++lag)
            if (normalised[static_cast<std::size_t>(lag)] < best) {
                best = normalised[static_cast<std::size_t>(lag)];
                chosen = lag;
            }
    }
    if (chosen < 0)
        return -1.0;
    auto period = static_cast<double>(chosen);
    if (chosen > 1 && chosen < longestLag) {
        const auto a = normalised[static_cast<std::size_t>(chosen - 1)];
        const auto b = normalised[static_cast<std::size_t>(chosen)];
        const auto c = normalised[static_cast<std::size_t>(chosen + 1)];
        const auto denominator = a - 2.0 * b + c;
        if (std::abs(denominator) > 1e-12)
            period += std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5);
    }
    const auto frequency = rate / period;
    return 69.0 + 12.0 * std::log2(frequency / 440.0);
}

// ---- Maps ---------------------------------------------------------------------------------------

namespace {

int intOf(const juce::var& value, const int fallback)
{
    return value.isVoid() || value.isUndefined() ? fallback : static_cast<int>(value);
}

juce::int64 int64Of(const juce::var& value, const juce::int64 fallback)
{
    return value.isVoid() || value.isUndefined() ? fallback : static_cast<juce::int64>(value);
}

float floatOf(const juce::var& value, const float fallback)
{
    if (value.isVoid() || value.isUndefined())
        return fallback;
    const auto number = static_cast<float>(static_cast<double>(value));
    return std::isfinite(number) ? number : fallback;
}

std::pair<int, int> pairOf(const juce::var& value, const std::pair<int, int> fallback)
{
    if (const auto* list = value.getArray(); list != nullptr && list->size() == 2)
        return { static_cast<int>((*list)[0]), static_cast<int>((*list)[1]) };
    return fallback;
}

std::pair<juce::int64, juce::int64> pair64Of(const juce::var& value, const std::pair<juce::int64, juce::int64> fallback)
{
    if (const auto* list = value.getArray(); list != nullptr && list->size() == 2)
        return { static_cast<juce::int64>((*list)[0]), static_cast<juce::int64>((*list)[1]) };
    return fallback;
}

juce::var pairVar(const juce::int64 first, const juce::int64 second)
{
    juce::Array<juce::var> list;
    list.add(first);
    list.add(second);
    return list;
}

juce::String noteName(const int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[((note % 12) + 12) % 12]) + juce::String(note / 12 - 1);
}

// A controller's rule on a zone, as stored: {"cc": 70, "level": 100} (one
// amount) or {"cc": 4, "range": [0, 63]} (two), and "curve" when it follows
// one of the map's.
struct RuleSpec {
    juce::String kind;
    int controller {};
    float a {};
    float b {};
    int curve { -1 };
};

constexpr std::array<std::pair<const char*, cydaw::engine::SamplerRuleKind>, 8> ruleKinds { {
    { "range", cydaw::engine::SamplerRuleKind::range },
    { "level", cydaw::engine::SamplerRuleKind::level },
    { "volume", cydaw::engine::SamplerRuleKind::volume },
    { "fadeIn", cydaw::engine::SamplerRuleKind::fadeIn },
    { "fadeOut", cydaw::engine::SamplerRuleKind::fadeOut },
    { "pan", cydaw::engine::SamplerRuleKind::pan },
    { "tune", cydaw::engine::SamplerRuleKind::tune },
    { "offset", cydaw::engine::SamplerRuleKind::offset },
} };

[[nodiscard]] bool twoAmounts(const juce::String& kind)
{
    return kind == "range" || kind == "fadeIn" || kind == "fadeOut";
}

// One zone of the JSON, as edited and stored.
struct ZoneSpec {
    juce::String file;
    int root { 60 };
    int lowKey {};
    int highKey { 127 };
    int lowVelocity { 1 };
    int highVelocity { 127 };
    float tune {};
    float gain {};
    float pan {};
    juce::int64 start {};
    juce::int64 end { -1 };
    juce::int64 loopStart { -1 };
    juce::int64 loopEnd { -1 };
    int loopCrossfade {};
    int endFade {};
    int sequenceLength { 1 };
    int sequencePosition { 1 };
    int group {};
    int offBy {};
    // "release", "first", "legato"; empty: a key pressed.
    juce::String trigger;
    float releaseDecay {};
    int keyswitch { -1 };
    std::vector<RuleSpec> rules;
};

ZoneSpec zoneFromVar(const juce::var& value)
{
    ZoneSpec zone;
    zone.file = value.getProperty("file", {}).toString();
    zone.root = std::clamp(intOf(value.getProperty("root", {}), 60), 0, 127);
    const auto keys = pairOf(value.getProperty("keys", {}), { 0, 127 });
    zone.lowKey = std::clamp(std::min(keys.first, keys.second), 0, 127);
    zone.highKey = std::clamp(std::max(keys.first, keys.second), 0, 127);
    const auto velocities = pairOf(value.getProperty("velocities", {}), { 1, 127 });
    zone.lowVelocity = std::clamp(std::min(velocities.first, velocities.second), 1, 127);
    zone.highVelocity = std::clamp(std::max(velocities.first, velocities.second), 1, 127);
    zone.tune = std::clamp(floatOf(value.getProperty("tune", {}), 0.0f), -2'400.0f, 2'400.0f);
    zone.gain = std::clamp(floatOf(value.getProperty("gain", {}), 0.0f), -96.0f, 24.0f);
    zone.pan = std::clamp(floatOf(value.getProperty("pan", {}), 0.0f), -1.0f, 1.0f);
    zone.start = std::max<juce::int64>(0, int64Of(value.getProperty("start", {}), 0));
    zone.end = int64Of(value.getProperty("end", {}), -1);
    const auto loop = pair64Of(value.getProperty("loop", {}), { -1, -1 });
    zone.loopStart = loop.first;
    zone.loopEnd = loop.second;
    zone.loopCrossfade = std::max(0, intOf(value.getProperty("loopCrossfade", {}), 0));
    zone.endFade = std::max(0, intOf(value.getProperty("endFade", {}), 0));
    const auto sequence = pairOf(value.getProperty("sequence", {}), { 1, 1 });
    zone.sequenceLength = std::clamp(sequence.first, 1, 64);
    zone.sequencePosition = std::clamp(sequence.second, 1, zone.sequenceLength);
    zone.group = std::max(0, intOf(value.getProperty("group", {}), 0));
    zone.offBy = std::max(0, intOf(value.getProperty("offBy", {}), 0));
    if (const auto trigger = value.getProperty("trigger", {}).toString();
        trigger == "release" || trigger == "first" || trigger == "legato")
        zone.trigger = trigger;
    zone.releaseDecay = std::clamp(floatOf(value.getProperty("releaseDecay", {}), 0.0f), 0.0f, 200.0f);
    zone.keyswitch = std::clamp(intOf(value.getProperty("keyswitch", {}), -1), -1, 127);
    if (const auto* rules = value.getProperty("rules", {}).getArray())
        for (const auto& item : *rules) {
            RuleSpec rule;
            rule.controller = intOf(item.getProperty("cc", {}), -1);
            if (rule.controller < 0 || rule.controller > 127)
                continue;
            for (const auto& [name, kind] : ruleKinds) {
                const auto amount = item.getProperty(name, {});
                if (amount.isVoid())
                    continue;
                rule.kind = name;
                if (const auto* list = amount.getArray(); list != nullptr && list->size() == 2) {
                    rule.a = floatOf((*list)[0], 0.0f);
                    rule.b = floatOf((*list)[1], 127.0f);
                } else {
                    rule.a = floatOf(amount, 0.0f);
                }
                break;
            }
            if (rule.kind.isEmpty())
                continue;
            rule.curve = intOf(item.getProperty("curve", {}), -1);
            zone.rules.push_back(rule);
        }
    return zone;
}

juce::var zoneToVar(const ZoneSpec& zone)
{
    auto* value = new juce::DynamicObject();
    value->setProperty("file", zone.file);
    value->setProperty("root", zone.root);
    value->setProperty("keys", pairVar(zone.lowKey, zone.highKey));
    value->setProperty("velocities", pairVar(zone.lowVelocity, zone.highVelocity));
    if (zone.tune != 0.0f)
        value->setProperty("tune", zone.tune);
    if (zone.gain != 0.0f)
        value->setProperty("gain", zone.gain);
    if (zone.pan != 0.0f)
        value->setProperty("pan", zone.pan);
    if (zone.start > 0)
        value->setProperty("start", zone.start);
    if (zone.end >= 0)
        value->setProperty("end", zone.end);
    if (zone.loopStart >= 0 && zone.loopEnd > zone.loopStart) {
        value->setProperty("loop", pairVar(zone.loopStart, zone.loopEnd));
        if (zone.loopCrossfade > 0)
            value->setProperty("loopCrossfade", zone.loopCrossfade);
    }
    if (zone.endFade > 0)
        value->setProperty("endFade", zone.endFade);
    if (zone.sequenceLength > 1)
        value->setProperty("sequence", pairVar(zone.sequenceLength, zone.sequencePosition));
    if (zone.group != 0)
        value->setProperty("group", zone.group);
    if (zone.offBy != 0)
        value->setProperty("offBy", zone.offBy);
    if (zone.trigger.isNotEmpty())
        value->setProperty("trigger", zone.trigger);
    if (zone.releaseDecay > 0.0f)
        value->setProperty("releaseDecay", zone.releaseDecay);
    if (zone.keyswitch >= 0)
        value->setProperty("keyswitch", zone.keyswitch);
    if (!zone.rules.empty()) {
        juce::Array<juce::var> rules;
        for (const auto& rule : zone.rules) {
            auto* item = new juce::DynamicObject();
            item->setProperty("cc", rule.controller);
            if (twoAmounts(rule.kind)) {
                juce::Array<juce::var> amounts;
                amounts.add(rule.a);
                amounts.add(rule.b);
                item->setProperty(juce::Identifier(rule.kind), amounts);
            } else {
                item->setProperty(juce::Identifier(rule.kind), rule.a);
            }
            if (rule.curve >= 0)
                item->setProperty("curve", rule.curve);
            rules.add(juce::var(item));
        }
        value->setProperty("rules", rules);
    }
    return juce::var(value);
}

std::vector<ZoneSpec> zonesOf(const juce::var& json)
{
    std::vector<ZoneSpec> zones;
    if (const auto* list = json.getProperty("zones", {}).getArray())
        for (const auto& item : *list)
            if (item.isObject())
                zones.push_back(zoneFromVar(item));
    return zones;
}

// What a map says beyond its zones: its controllers' starting values, the
// controls a card shows, its curves, its keyswitches.
// "whole": its recordings kept whole in memory, never read from the disk as they
// play (a single sound: the simple sampler, read anywhere by its grains).
constexpr std::array<const char*, 5> mapExtras { "controllers", "controls", "curves", "keyswitches", "whole" };

// `from`: the map whose extras it keeps (an SFZ read: its own).
juce::var mapVar(const juce::String& name, const std::vector<ZoneSpec>& zones, const juce::var& settings,
                 const juce::var& from = {})
{
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "cydaw-sampler");
    root->setProperty("version", 1);
    root->setProperty("name", name);
    juce::Array<juce::var> list;
    for (const auto& zone : zones)
        list.add(zoneToVar(zone));
    root->setProperty("zones", list);
    if (settings.isObject())
        root->setProperty("settings", settings);
    for (const auto* extra : mapExtras)
        if (const auto value = from.getProperty(extra, {}); !value.isVoid())
            root->setProperty(extra, value);
    return juce::var(root);
}

// Each root's keys: from just above the midpoint with the root below to the
// midpoint with the root above; the lowest from 0, the highest to 127.
void spreadKeys(std::vector<ZoneSpec>& zones)
{
    std::set<int> roots;
    for (const auto& zone : zones)
        roots.insert(zone.root);
    const std::vector<int> ordered(roots.begin(), roots.end());
    for (auto& zone : zones) {
        const auto at = std::ranges::lower_bound(ordered, zone.root) - ordered.begin();
        const auto below = at > 0 ? ordered[static_cast<std::size_t>(at - 1)] : -1;
        const auto above = static_cast<std::size_t>(at + 1) < ordered.size() ? ordered[static_cast<std::size_t>(at + 1)] : -1;
        zone.lowKey = below < 0 ? 0 : (below + zone.root) / 2 + 1;
        zone.highKey = above < 0 ? 127 : (zone.root + above) / 2;
    }
}

// The layer a name says: "v2", "vl3", "vel1", "dyn2", "layer4", or a
// dynamic ("pp" 2 … "ff" 7); -1 when it says none.
int layerFromName(const juce::String& name)
{
    juce::StringArray tokens;
    tokens.addTokens(name.toLowerCase(), "_- .()[]", "");
    for (const auto& token : tokens)
        for (const auto* prefix : { "vl", "vel", "dyn", "layer", "v" })
            if (token.startsWith(prefix)) {
                const auto rest = token.substring(juce::String(prefix).length());
                if (rest.isNotEmpty() && rest.containsOnly("0123456789"))
                    return rest.getIntValue();
            }
    static const std::pair<const char*, int> dynamics[] = {
        { "ppp", 1 }, { "pp", 2 }, { "p", 3 }, { "mp", 4 }, { "mf", 5 }, { "f", 6 }, { "ff", 7 }, { "fff", 8 }
    };
    for (const auto& token : tokens)
        for (const auto& [word, layer] : dynamics)
            if (token == word)
                return layer;
    return -1;
}

int roundRobinFromName(const juce::String& name)
{
    juce::StringArray tokens;
    tokens.addTokens(name.toLowerCase(), "_- .()[]", "");
    for (const auto& token : tokens)
        if (token.startsWith("rr") && token.length() > 2 && token.substring(2).containsOnly("0123456789"))
            return token.substring(2).getIntValue();
    return -1;
}

struct Candidate {
    juce::File file;
    SampleDataRef data;
    int root { -1 };
    int layer { -1 };
    int roundRobin { -1 };
    double loudness {};
};

// New zones for files, with everything a file and its name say.
std::vector<ZoneSpec> zonesForFiles(const juce::Array<juce::File>& files, std::vector<ZoneSpec> existing,
                                    bool& unpitched)
{
    std::vector<Candidate> candidates;
    for (const auto& file : files)
        if (file.existsAsFile())
            candidates.push_back({ file, SampleStore::instance().acquire(file) });
    // Decoded once here; the cards that play them find them in the store.
    SamplerMap waiting;
    for (const auto& candidate : candidates) {
        SamplerZone zone;
        zone.sample = candidate.data;
        waiting.zones.push_back(zone);
    }
    SampleStore::instance().waitUntilReady(waiting, 120'000);

    auto pitched = 0;
    for (auto& candidate : candidates) {
        const auto name = candidate.file.getFileNameWithoutExtension();
        const auto named = noteFromName(name);
        const auto heard = candidate.data->ready.load() ? detectSamplePitch(*candidate.data) : -1.0;
        if (named >= 0 && heard >= 0.0) {
            // The name's note, in the octave the sound has (banks number
            // their octaves differently).
            auto best = named;
            for (int shift = -24; shift <= 24; shift += 12)
                if (named + shift >= 0 && named + shift <= 127
                    && std::abs(named + shift - heard) < std::abs(best - heard))
                    best = named + shift;
            candidate.root = best;
        } else if (named >= 0) {
            candidate.root = named;
        } else if (heard >= 0.0) {
            candidate.root = std::clamp(static_cast<int>(std::lround(heard)), 0, 127);
        }
        candidate.layer = layerFromName(name);
        candidate.roundRobin = roundRobinFromName(name);
        candidate.loudness = candidate.data->ready.load() ? loudnessOf(*candidate.data) : 0.0;
        if (candidate.root >= 0)
            ++pitched;
    }
    unpitched = pitched == 0 && !candidates.empty();

    std::vector<ZoneSpec> zones = std::move(existing);
    // A zone on one key is a drum's: it keeps its key when the others spread.
    std::vector<bool> fixedKey;
    for (const auto& zone : zones)
        fixedKey.push_back(zone.lowKey == zone.highKey);
    // A sound without a pitch gets one key of its own, from C1 up, past the
    // keys already taken.
    auto nextKey = 36;
    for (const auto& zone : zones)
        if (zone.lowKey == zone.highKey)
            nextKey = std::max(nextKey, zone.highKey + 1);
    std::map<int, std::vector<const Candidate*>> byRoot;
    for (const auto& candidate : candidates) {
        if (candidate.root < 0) {
            ZoneSpec zone;
            zone.file = storedSamplePath(candidate.file);
            zone.root = std::min(127, nextKey);
            zone.lowKey = zone.root;
            zone.highKey = zone.root;
            zones.push_back(zone);
            fixedKey.push_back(true);
            nextKey = std::min(127, nextKey + 1);
            continue;
        }
        byRoot[candidate.root].push_back(&candidate);
    }
    for (auto& [root, group] : byRoot) {
        // The layers: by the names' numbers when every file has one, else by
        // loudness; files of one layer that differ by "rr" are round robins.
        const auto named = std::ranges::all_of(group, [](const Candidate* candidate) { return candidate->layer >= 0; });
        std::map<double, std::vector<const Candidate*>> layers;
        for (const auto* candidate : group) {
            const auto key = named ? static_cast<double>(candidate->layer)
                                   : candidate->roundRobin >= 0 ? -1.0 : candidate->loudness;
            layers[key].push_back(candidate);
        }
        if (!named && layers.contains(-1.0) && layers.size() > 1) {
            // Round robins without a layer name join the loudest layer.
            auto robins = layers[-1.0];
            layers.erase(-1.0);
            auto& last = std::prev(layers.end())->second;
            last.insert(last.end(), robins.begin(), robins.end());
        }
        const auto layerCount = static_cast<int>(layers.size());
        auto layerIndex = 0;
        for (auto& [key, members] : layers) {
            const auto low = layerIndex * 127 / layerCount + 1;
            const auto high = (layerIndex + 1) * 127 / layerCount;
            auto ordered = members;
            std::ranges::sort(ordered, [](const Candidate* a, const Candidate* b) {
                return a->roundRobin < b->roundRobin;
            });
            const auto length = static_cast<int>(ordered.size());
            auto position = 1;
            for (const auto* candidate : ordered) {
                ZoneSpec zone;
                zone.file = storedSamplePath(candidate->file);
                zone.root = root;
                zone.lowVelocity = low;
                zone.highVelocity = std::max(low, high);
                if (length > 1) {
                    zone.sequenceLength = length;
                    zone.sequencePosition = position++;
                }
                if (candidate->data->ready.load() && candidate->data->fileLoopStart >= 0) {
                    zone.loopStart = candidate->data->fileLoopStart;
                    zone.loopEnd = candidate->data->fileLoopEnd;
                }
                zones.push_back(zone);
                fixedKey.push_back(false);
            }
            ++layerIndex;
        }
    }
    // Keys spread among the pitched zones; a drum keeps its one key.
    std::vector<ZoneSpec> spread;
    for (std::size_t index = 0; index < zones.size(); ++index)
        if (!fixedKey[index])
            spread.push_back(zones[index]);
    if (!spread.empty()) {
        spreadKeys(spread);
        std::size_t next = 0;
        for (std::size_t index = 0; index < zones.size(); ++index)
            if (!fixedKey[index])
                zones[index] = spread[next++];
    }
    return zones;
}

} // namespace

SamplerMapRef samplerMapFromVar(const juce::var& json, juce::String& error)
{
    if (json.getProperty("format", {}).toString() != "cydaw-sampler") {
        error = "not a cydaw-sampler map";
        return {};
    }
    auto map = std::make_shared<SamplerMap>();
    map->name = json.getProperty("name", {}).toString().toStdString();
    std::vector<juce::String> files;
    for (const auto& spec : zonesOf(json)) {
        if (spec.file.isEmpty())
            continue;
        SamplerZone zone;
        files.push_back(spec.file);
        zone.rootNote = spec.root;
        zone.lowKey = spec.lowKey;
        zone.highKey = spec.highKey;
        zone.lowVelocity = spec.lowVelocity;
        zone.highVelocity = spec.highVelocity;
        zone.tuneCents = spec.tune;
        zone.gainDb = spec.gain;
        zone.pan = spec.pan;
        zone.start = spec.start;
        zone.end = spec.end;
        zone.loopStart = spec.loopStart;
        zone.loopEnd = spec.loopEnd;
        zone.loopCrossfade = spec.loopCrossfade;
        zone.endFade = spec.endFade;
        zone.sequenceLength = spec.sequenceLength;
        zone.sequencePosition = spec.sequencePosition;
        zone.group = spec.group;
        zone.offBy = spec.offBy;
        zone.trigger = spec.trigger == "release" ? cydaw::engine::SamplerTrigger::release
                       : spec.trigger == "first" ? cydaw::engine::SamplerTrigger::first
                       : spec.trigger == "legato" ? cydaw::engine::SamplerTrigger::legato
                                                  : cydaw::engine::SamplerTrigger::attack;
        zone.releaseDecayDb = spec.releaseDecay;
        zone.keyswitch = spec.keyswitch;
        for (const auto& rule : spec.rules) {
            cydaw::engine::SamplerRule made;
            for (const auto& [name, kind] : ruleKinds)
                if (rule.kind == name)
                    made.kind = kind;
            made.controller = static_cast<std::uint8_t>(std::clamp(rule.controller, 0, 127));
            made.curve = static_cast<std::int16_t>(std::clamp(rule.curve, -1, 1'000));
            made.a = rule.a;
            made.b = rule.b;
            zone.rules.push_back(made);
        }
        map->hasReleaseZones = map->hasReleaseZones || zone.trigger == cydaw::engine::SamplerTrigger::release;
        if (zone.keyswitch >= 0)
            map->keyswitchKeys[static_cast<std::size_t>(zone.keyswitch)] = true;
        map->zones.push_back(std::move(zone));
    }
    if (map->zones.empty()) {
        error = "the map has no zone";
        return {};
    }
    // Where its controllers start, what a card shows of them, their curves.
    if (const auto* controllers = json.getProperty("controllers", {}).getDynamicObject())
        for (const auto& [name, value] : controllers->getProperties())
            if (const auto number = name.toString().getIntValue();
                number >= 0 && number < 128 && name.toString().containsOnly("0123456789"))
                map->controllerStart[static_cast<std::size_t>(number)] = std::clamp(floatOf(value, 0.0f), 0.0f, 127.0f);
    if (const auto* controls = json.getProperty("controls", {}).getArray())
        for (const auto& item : *controls)
            if (const auto number = intOf(item.getProperty("cc", {}), -1); number >= 0 && number < 128)
                map->controls.push_back({ number, item.getProperty("label", {}).toString().toStdString() });
    if (const auto* curves = json.getProperty("curves", {}).getArray())
        for (const auto& item : *curves) {
            std::array<float, 128> table {};
            for (std::size_t at = 0; at < table.size(); ++at)
                table[at] = static_cast<float>(at) / 127.0f;
            if (const auto* values = item.getArray())
                for (int at = 0; at < std::min(128, values->size()); ++at)
                    table[static_cast<std::size_t>(at)] = floatOf((*values)[at], table[static_cast<std::size_t>(at)]);
            map->curves.push_back(table);
        }
    // Its keyswitches: the keys that choose, the one it opens with, their names.
    if (const auto keyswitches = json.getProperty("keyswitches", {}); keyswitches.isObject()) {
        if (const auto* keys = keyswitches.getProperty("keys", {}).getArray())
            for (const auto& key : *keys)
                if (const auto number = intOf(key, -1); number >= 0 && number < 128)
                    map->keyswitchKeys[static_cast<std::size_t>(number)] = true;
        map->keyswitchStart = std::clamp(intOf(keyswitches.getProperty("start", {}), -1), -1, 127);
        if (const auto* labels = keyswitches.getProperty("labels", {}).getDynamicObject())
            for (const auto& [name, value] : labels->getProperties())
                if (const auto number = name.toString().getIntValue(); number >= 0 && number < 128)
                    map->keyswitchLabels.emplace_back(number, value.toString().toStdString());
        std::ranges::sort(map->keyswitchLabels, {}, &std::pair<int, std::string>::first);
    }
    if (map->keyswitchStart < 0)
        for (int key = 0; key < 128; ++key)
            if (map->keyswitchKeys[static_cast<std::size_t>(key)]) {
                map->keyswitchStart = key;
                break;
            }
    // The samples: decoded now those the instrument reaches as it opens,
    // the others (a microphone at zero, another articulation) once asked for;
    // a long one keeps its head in memory and is read from the disk past it,
    // unless the map keeps them whole.
    const auto whole = static_cast<bool>(json.getProperty("whole", false));
    for (std::size_t index = 0; index < map->zones.size(); ++index) {
        auto& zone = map->zones[index];
        const auto reached = cydaw::engine::samplerZoneReachable(*map, zone, {});
        zone.sample = SampleStore::instance().acquire(resolveSamplePath(files[index]), !reached,
                                                      whole ? -1 : std::max<std::int64_t>(0, zone.start),
                                                      whole ? -1 : zone.loops() ? zone.loopEnd : -1);
    }
    error.clear();
    return map;
}

juce::var loadSamplerMapFile(const juce::File& file, juce::String& error)
{
    if (file.hasFileExtension("sfz"))
        return samplerMapFromSfz(file, error);
    const auto json = juce::JSON::parse(file.loadFileAsString());
    if (json.getProperty("format", {}).toString() != "cydaw-sampler") {
        error = "not a cydaw-sampler map: " + file.getFullPathName();
        return {};
    }
    // Relative files are the map's neighbours.
    std::vector<ZoneSpec> zones = zonesOf(json);
    for (auto& zone : zones)
        if (!zone.file.startsWith(factoryPrefix) && !zone.file.startsWith(bankPrefix) && !juce::File::isAbsolutePath(zone.file))
            zone.file = storedSamplePath(file.getParentDirectory().getChildFile(zone.file));
    error.clear();
    return mapVar(json.getProperty("name", file.getFileNameWithoutExtension()).toString(), zones,
                  json.getProperty("settings", {}), json);
}

juce::String describeSamplerMap(const juce::var& json)
{
    const auto zones = zonesOf(json);
    if (zones.empty())
        return {};
    // Its notes from the lowest recorded to the highest (the outer zones
    // reach the ends of the keyboard, transposed).
    auto low = 127;
    auto high = 0;
    std::map<int, std::set<std::pair<int, int>>> layers;
    for (const auto& zone : zones) {
        low = std::min(low, zone.root);
        high = std::max(high, zone.root);
        layers[zone.root].insert({ zone.lowVelocity, zone.highVelocity });
    }
    std::size_t layerCount = 0;
    for (const auto& [root, ranges] : layers)
        layerCount = std::max(layerCount, ranges.size());
    const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
    return json.getProperty("name", {}).toString() + dot + juce::String(static_cast<int>(zones.size()))
           + (zones.size() > 1 ? " zones" : " zone") + dot + noteName(low) + juce::String::fromUTF8("\xe2\x80\x93")
           + noteName(high) + dot + juce::String(static_cast<int>(layerCount))
           + (layerCount > 1 ? " layers" : " layer");
}

juce::var samplerMapFromFiles(const juce::Array<juce::File>& files, const juce::String& name)
{
    auto unpitched = false;
    const auto zones = zonesForFiles(files, {}, unpitched);
    juce::var settings;
    if (unpitched) {
        // Drums: each plays to its end at its own pitch.
        auto* values = new juce::DynamicObject();
        values->setProperty("one-shot", 1);
        values->setProperty("key-track", 0);
        settings = juce::var(values);
    }
    return mapVar(name, zones, settings);
}

juce::var simpleSamplerMap(const juce::File& file)
{
    auto json = samplerMapFromFiles(juce::Array<juce::File> { file }, file.getFileNameWithoutExtension());
    const auto unpitched = static_cast<int>(json.getProperty("settings", {}).getProperty("key-track", 1)) == 0;
    if (auto* zones = json.getProperty("zones", {}).getArray(); zones != nullptr && !zones->isEmpty()) {
        zones->removeRange(1, zones->size() - 1);
        if (auto* zone = zones->getReference(0).getDynamicObject()) {
            zone->setProperty("keys", juce::Array<juce::var> { 0, 127 });
            zone->setProperty("velocities", juce::Array<juce::var> { 1, 127 });
            if (unpitched)
                zone->setProperty("root", 60);
        }
    }
    if (auto* object = json.getDynamicObject()) {
        object->removeProperty("settings");
        // One sound, read anywhere (its grains): kept whole in memory.
        object->setProperty("whole", true);
    }
    return json;
}

juce::var withFilesAdded(const juce::var& json, const juce::Array<juce::File>& files)
{
    auto unpitched = false;
    auto zones = zonesForFiles(files, zonesOf(json), unpitched);
    return mapVar(json.getProperty("name", {}).toString(), zones, json.getProperty("settings", {}), json);
}

juce::var withKeysSpread(const juce::var& json)
{
    auto zones = zonesOf(json);
    std::vector<ZoneSpec> spread;
    for (const auto& zone : zones)
        spread.push_back(zone);
    spreadKeys(spread);
    return mapVar(json.getProperty("name", {}).toString(), spread, json.getProperty("settings", {}), json);
}

// ---- SFZ --------------------------------------------------------------------------------------

namespace {

// The text with its #include files in place and its #define names replaced.
juce::String expandSfz(const juce::File& file, const juce::File& root, std::map<juce::String, juce::String>& defines,
                       int depth, int& includes)
{
    if (depth > 16 || !file.existsAsFile())
        return {};
    juce::StringArray lines;
    lines.addLines(file.loadFileAsString());
    juce::String out;
    for (auto line : lines) {
        // Comments: // to the end of the line (block comments are rare).
        if (const auto comment = line.indexOf("//"); comment >= 0)
            line = line.substring(0, comment);
        const auto trimmed = line.trim();
        if (trimmed.startsWith("#define")) {
            juce::StringArray parts;
            parts.addTokens(trimmed.substring(7).trim(), " \t", "");
            parts.removeEmptyStrings();
            if (parts.size() >= 2)
                defines[parts[0]] = parts.joinIntoString(" ", 1);
            continue;
        }
        if (trimmed.startsWith("#include")) {
            const auto name = trimmed.fromFirstOccurrenceOf("\"", false, false).upToLastOccurrenceOf("\"", false, false);
            // A budget of includes for the whole map: one including itself
            // several times would read millions of files before the depth stops it.
            if (--includes >= 0)
                out << expandSfz(root.getChildFile(name.replaceCharacter('\\', '/')), root, defines, depth + 1, includes) << "\n";
            continue;
        }
        // Longest names first, so "$A10" is not read as "$A1" then "0".
        std::vector<std::pair<juce::String, juce::String>> ordered(defines.begin(), defines.end());
        std::ranges::sort(ordered, [](const auto& a, const auto& b) { return a.first.length() > b.first.length(); });
        for (const auto& [name, value] : ordered)
            line = line.replace(name, value);
        out << line << "\n";
    }
    return out;
}

using Opcodes = std::map<juce::String, juce::String>;

// The SFZ's headers and their opcodes, in order. An opcode's value runs to
// the next opcode, header or line end (a sample's path may hold spaces).
std::vector<std::pair<juce::String, Opcodes>> parseSfz(const juce::String& text)
{
    std::vector<std::pair<juce::String, Opcodes>> sections;
    juce::StringArray lines;
    lines.addLines(text);
    for (const auto& rawLine : lines) {
        auto line = rawLine;
        while (line.isNotEmpty()) {
            line = line.trimStart();
            if (line.isEmpty())
                break;
            if (line.startsWithChar('<')) {
                const auto close = line.indexOfChar('>');
                if (close < 0)
                    break;
                sections.push_back({ line.substring(1, close).trim().toLowerCase(), {} });
                line = line.substring(close + 1);
                continue;
            }
            const auto equals = line.indexOfChar('=');
            if (equals <= 0)
                break;
            const auto key = line.substring(0, equals).trim().toLowerCase();
            auto rest = line.substring(equals + 1);
            // The value ends where the next "name=" or "<header>" begins.
            auto end = rest.length();
            for (int at = 0; at < rest.length(); ++at) {
                if (rest[at] == '<') {
                    end = at;
                    break;
                }
                if (at > 0 && juce::CharacterFunctions::isWhitespace(rest[at - 1])) {
                    auto scan = at;
                    while (scan < rest.length() && (juce::CharacterFunctions::isLetterOrDigit(rest[scan]) || rest[scan] == '_'))
                        ++scan;
                    if (scan > at && scan < rest.length() && rest[scan] == '=') {
                        end = at;
                        break;
                    }
                }
            }
            if (sections.empty())
                sections.push_back({ "region", {} });
            sections.back().second[key] = rest.substring(0, end).trim();
            line = rest.substring(end);
        }
    }
    return sections;
}

int sfzNote(const juce::String& value, const int fallback)
{
    const auto text = value.trim();
    if (text.isEmpty())
        return fallback;
    if (text.containsOnly("-0123456789"))
        return std::clamp(text.getIntValue(), 0, 127);
    // SFZ names middle C "c4" (60).
    const auto note = noteFromName(text);
    return note >= 0 ? note : fallback;
}

} // namespace

juce::var samplerMapFromSfz(const juce::File& file, juce::String& error, const std::map<int, double>& given)
{
    if (!file.existsAsFile()) {
        error = "no file " + file.getFullPathName();
        return {};
    }
    std::map<juce::String, juce::String> defines;
    auto includes = 1'024;
    const auto sections = parseSfz(expandSfz(file, file.getParentDirectory(), defines, 0, includes));
    // What the instrument opens with: its controllers' starting values
    // (set_ccN; MIDI's own otherwise), their names, its curves, its
    // keyswitches.
    std::map<int, double> controllers;
    std::map<int, juce::String> labels;
    std::map<int, std::array<float, 128>> curves;
    juce::String defaultSwitch;
    std::set<int> switchKeys;
    std::map<int, juce::String> switchLabels;
    for (const auto& [header, opcodes] : sections) {
        if (header == "curve") {
            // Points given at some controller values, straight lines between
            // them; 0 at 0 and 1 at 127 unless given.
            const auto index = opcodes.contains("curve_index") ? opcodes.at("curve_index").getIntValue() : -1;
            if (index < 0)
                continue;
            std::map<int, float> points { { 0, 0.0f }, { 127, 1.0f } };
            for (const auto& [key, value] : opcodes)
                if (key.startsWithChar('v') && key.substring(1).containsOnly("0123456789") && key.length() > 1)
                    points[std::clamp(key.substring(1).getIntValue(), 0, 127)] = value.getFloatValue();
            std::array<float, 128> table {};
            for (int at = 0; at < 128; ++at) {
                const auto above = points.lower_bound(at);
                if (above->first == at) {
                    table[static_cast<std::size_t>(at)] = above->second;
                    continue;
                }
                const auto below = std::prev(above);
                const auto t = static_cast<float>(at - below->first) / static_cast<float>(above->first - below->first);
                table[static_cast<std::size_t>(at)] = below->second + (above->second - below->second) * t;
            }
            curves[index] = table;
            continue;
        }
        for (const auto& [key, value] : opcodes) {
            if (header == "control" && key.startsWith("set_cc"))
                controllers[key.substring(6).getIntValue()] = value.getDoubleValue();
            else if (header == "control" && (key.startsWith("set_hdcc") || key.startsWith("set_realcc")))
                controllers[key.fromFirstOccurrenceOf("cc", false, false).getIntValue()] = value.getDoubleValue() * 127.0;
            else if (key.startsWith("label_cc"))
                labels[key.substring(8).getIntValue()] = value.trim();
            else if (key == "sw_default" && defaultSwitch.isEmpty())
                defaultSwitch = value;
        }
    }
    for (const auto& [number, value] : given)
        controllers[number] = value;
    // A region's curves: the SFZ's numbers, made the map's (0: straight, no curve).
    std::vector<int> usedCurves;
    const auto curveOf = [&](const Opcodes& merged, const juce::String& opcode, const int controller) {
        const auto found = merged.find(opcode + juce::String(controller));
        if (found == merged.end())
            return -1;
        const auto index = found->second.getIntValue();
        if (index == 0 || (index > 6 && !curves.contains(index)))
            return -1;
        if (!curves.contains(index)) {
            // SFZ's own: 1 bipolar, 2 and 3 the same inverted, 4 concave, 5
            // and 6 the crossfades' power curves.
            std::array<float, 128> table {};
            for (int at = 0; at < 128; ++at) {
                const auto x = static_cast<float>(at) / 127.0f;
                table[static_cast<std::size_t>(at)] = index == 1   ? x * 2.0f - 1.0f
                                                      : index == 2 ? 1.0f - x
                                                      : index == 3 ? 1.0f - x * 2.0f
                                                      : index == 4 ? x * x
                                                      : index == 5 ? std::sqrt(x)
                                                                   : std::sqrt(1.0f - x);
            }
            curves[index] = table;
        }
        const auto at = std::ranges::find(usedCurves, index);
        if (at != usedCurves.end())
            return static_cast<int>(at - usedCurves.begin());
        usedCurves.push_back(index);
        return static_cast<int>(usedCurves.size()) - 1;
    };
    juce::AudioFormatManager formats;
    Opcodes control;
    Opcodes global;
    Opcodes master;
    Opcodes group;
    std::vector<ZoneSpec> zones;
    std::set<int> readControllers;
    auto oneShots = 0;
    auto regions = 0;
    for (const auto& [header, opcodes] : sections) {
        if (header == "control") {
            control = opcodes;
            continue;
        }
        if (header == "global") {
            global = opcodes;
            master.clear();
            group.clear();
            continue;
        }
        if (header == "master") {
            master = opcodes;
            group.clear();
            continue;
        }
        if (header == "group") {
            group = opcodes;
            continue;
        }
        if (header != "region")
            continue;
        Opcodes merged = global;
        const std::array<const Opcodes*, 3> levels { &master, &group, &opcodes };
        for (const auto* level : levels)
            for (const auto& [key, value] : *level)
                merged[key] = value;
        const auto get = [&merged](const char* key) {
            const auto found = merged.find(key);
            return found != merged.end() ? found->second : juce::String();
        };
        // When it sounds: a key pressed (alone or over another), or let go.
        // A keyswitch held, or pressed just before, is not played.
        auto trigger = get("trigger");
        if (trigger.isEmpty() || trigger == "attack")
            trigger = {};
        else if (trigger == "release_key")
            trigger = "release";
        else if (trigger != "release" && trigger != "first" && trigger != "legato")
            continue;
        if (get("sw_down").isNotEmpty() || get("sw_up").isNotEmpty() || get("sw_previous").isNotEmpty())
            continue;
        if (get("lochan").getIntValue() > 1 || (get("hichan").isNotEmpty() && get("hichan").getIntValue() < 1)
            || get("loprog").getIntValue() > 0 || get("lobend").getIntValue() > 0
            || (get("hibend").isNotEmpty() && get("hibend").getIntValue() < 0))
            continue;
        // The keys that choose an articulation.
        if (get("sw_lokey").isNotEmpty() || get("sw_hikey").isNotEmpty()) {
            const auto low = sfzNote(get("sw_lokey"), 0);
            const auto high = sfzNote(get("sw_hikey"), low);
            for (auto key = std::min(low, high); key <= std::max(low, high); ++key)
                switchKeys.insert(key);
        }
        // Its levels that never move (ARIA's add up; an opcode of a lower
        // level replaces), and what the controllers do to it.
        auto amplitude = get("amplitude").isNotEmpty() ? get("amplitude").getDoubleValue() : 100.0;
        auto factor = 1.0;
        auto extraDb = 0.0;
        std::vector<RuleSpec> rules;
        std::map<int, std::pair<double, double>> ranges;
        std::map<int, std::pair<double, double>> fadesIn;
        std::map<int, std::pair<double, double>> fadesOut;
        const auto addRule = [&](const char* kind, const int controller, const double amount, const char* curveOpcode) {
            if (controller < 0 || controller > 127)
                return;
            rules.push_back({ kind, controller, static_cast<float>(amount), 0.0f, curveOf(merged, curveOpcode, controller) });
        };
        for (const auto& [key, value] : merged) {
            const auto after = [&key](const char* prefix) {
                return key.substring(static_cast<int>(std::strlen(prefix))).getIntValue();
            };
            const auto number = value.getDoubleValue();
            if (key.startsWith("locc"))
                ranges.try_emplace(after("locc"), 0.0, 127.0).first->second.first = number;
            else if (key.startsWith("hicc"))
                ranges.try_emplace(after("hicc"), 0.0, 127.0).first->second.second = number;
            else if (key.startsWith("lohdcc"))
                ranges.try_emplace(after("lohdcc"), 0.0, 127.0).first->second.first = number * 127.0;
            else if (key.startsWith("hihdcc"))
                ranges.try_emplace(after("hihdcc"), 0.0, 127.0).first->second.second = number * 127.0;
            else if (key.startsWith("xfin_locc"))
                fadesIn[after("xfin_locc")].first = number;
            else if (key.startsWith("xfin_hicc"))
                fadesIn[after("xfin_hicc")].second = number;
            else if (key.startsWith("xfout_locc"))
                fadesOut[after("xfout_locc")].first = number;
            else if (key.startsWith("xfout_hicc"))
                fadesOut[after("xfout_hicc")].second = number;
            // A controller's amplitude scales (a microphone's fader): its
            // percentage at the controller's top, nothing at zero.
            else if (key.startsWith("amplitude_oncc"))
                addRule("level", after("amplitude_oncc"), number, "amplitude_curvecc");
            else if (key.startsWith("amplitude_cc"))
                addRule("level", after("amplitude_cc"), number, "amplitude_curvecc");
            else if (key.startsWith("volume_oncc"))
                addRule("volume", after("volume_oncc"), number, "volume_curvecc");
            else if (key.startsWith("gain_oncc"))
                addRule("volume", after("gain_oncc"), number, "volume_curvecc");
            else if (key.startsWith("gain_cc"))
                addRule("volume", after("gain_cc"), number, "volume_curvecc");
            else if (key.startsWith("pan_oncc"))
                addRule("pan", after("pan_oncc"), number / 100.0, "pan_curvecc");
            else if (key.startsWith("pan_cc"))
                addRule("pan", after("pan_cc"), number / 100.0, "pan_curvecc");
            else if (key.startsWith("tune_oncc"))
                addRule("tune", after("tune_oncc"), number, "tune_curvecc");
            else if (key.startsWith("tune_cc"))
                addRule("tune", after("tune_cc"), number, "tune_curvecc");
            else if (key.startsWith("pitch_oncc"))
                addRule("tune", after("pitch_oncc"), number, "pitch_curvecc");
            else if (key.startsWith("pitch_cc"))
                addRule("tune", after("pitch_cc"), number, "pitch_curvecc");
            else if (key.startsWith("offset_oncc"))
                addRule("offset", after("offset_oncc"), number, "offset_curvecc");
            else if (key.startsWith("offset_cc"))
                addRule("offset", after("offset_cc"), number, "offset_curvecc");
            else if (key == "global_volume" || key == "master_volume" || key == "group_volume")
                extraDb += number;
            else if (key == "global_amplitude" || key == "master_amplitude" || key == "group_amplitude")
                factor *= number / 100.0;
        }
        for (const auto& [controller, range] : ranges)
            if (controller >= 0 && controller < 128 && (range.first > 0.0 || range.second < 127.0))
                rules.push_back({ "range", controller, static_cast<float>(range.first), static_cast<float>(range.second), -1 });
        for (const auto& [controller, range] : fadesIn)
            if (controller >= 0 && controller < 128)
                rules.push_back({ "fadeIn", controller, static_cast<float>(range.first), static_cast<float>(range.second), -1 });
        for (const auto& [controller, range] : fadesOut)
            if (controller >= 0 && controller < 128)
                rules.push_back({ "fadeOut", controller, static_cast<float>(range.first), static_cast<float>(range.second), -1 });
        factor *= std::max(0.0, amplitude) / 100.0;
        if (factor < 0.001)
            continue;
        extraDb += 20.0 * std::log10(factor);
        auto sample = get("sample").replaceCharacter('\\', '/');
        if (sample.isEmpty() || sample.startsWithChar('*'))
            continue;
        const auto defaultPath = control.contains("default_path")
                                     ? control.at("default_path").replaceCharacter('\\', '/') : juce::String();
        // Where the SFZ says, else without a $name no #define gave, else in
        // ../Samples: a mapping written to be included by another.
        auto sampleFile = file.getParentDirectory().getChildFile(defaultPath + sample);
        if (!sampleFile.existsAsFile()) {
            const auto folder = file.getParentDirectory();
            for (const auto& candidate : { folder.getChildFile(sample), folder.getChildFile("../Samples/" + sample),
                                           folder.getChildFile("../samples/" + sample),
                                           folder.getChildFile("../Samples/" + defaultPath.fromLastOccurrenceOf("$", false, false)
                                                                   .fromFirstOccurrenceOf("/", false, false) + sample) })
                if (candidate.existsAsFile()) {
                    sampleFile = candidate;
                    break;
                }
        }
        ZoneSpec zone;
        zone.file = storedSamplePath(sampleFile);
        zone.trigger = trigger;
        zone.releaseDecay = trigger == "release" ? std::clamp(get("rt_decay").getFloatValue(), 0.0f, 200.0f) : 0.0f;
        if (const auto keyswitch = get("sw_last"); keyswitch.isNotEmpty()) {
            zone.keyswitch = sfzNote(keyswitch, -1);
            if (zone.keyswitch >= 0) {
                switchKeys.insert(zone.keyswitch);
                if (const auto label = get("sw_label"); label.isNotEmpty() && !switchLabels.contains(zone.keyswitch))
                    switchLabels[zone.keyswitch] = label.trim();
            }
        }
        zone.rules = rules;
        for (const auto& rule : rules)
            readControllers.insert(rule.controller);
        if (trigger.isEmpty() || trigger == "first" || trigger == "legato")
            ++regions;
        const auto keyOffset = (control.contains("note_offset") ? control.at("note_offset").getIntValue() : 0)
                               + 12 * (control.contains("octave_offset") ? control.at("octave_offset").getIntValue() : 0);
        if (const auto key = get("key"); key.isNotEmpty()) {
            const auto note = sfzNote(key, 60);
            zone.lowKey = note;
            zone.highKey = note;
            zone.root = note;
        }
        zone.lowKey = sfzNote(get("lokey"), zone.lowKey);
        zone.highKey = sfzNote(get("hikey"), zone.highKey);
        // SFZ's pitch centre is 60 unless "key" or "pitch_keycenter" says.
        zone.root = sfzNote(get("pitch_keycenter"), get("key").isNotEmpty() ? zone.root : 60);
        zone.lowKey = std::clamp(zone.lowKey + keyOffset, 0, 127);
        zone.highKey = std::clamp(zone.highKey + keyOffset, 0, 127);
        zone.root = std::clamp(zone.root + keyOffset, 0, 127);
        if (get("lovel").isNotEmpty())
            zone.lowVelocity = std::clamp(get("lovel").getIntValue(), 1, 127);
        if (get("hivel").isNotEmpty())
            zone.highVelocity = std::clamp(get("hivel").getIntValue(), 1, 127);
        zone.tune = static_cast<float>(get("tune").getDoubleValue() + 100.0 * get("transpose").getDoubleValue());
        zone.gain = static_cast<float>(get("volume").getDoubleValue() + extraDb);
        zone.pan = static_cast<float>(std::clamp(get("pan").getDoubleValue() / 100.0, -1.0, 1.0));
        zone.start = std::max<juce::int64>(0, get("offset").getLargeIntValue());
        if (get("end").isNotEmpty())
            zone.end = get("end").getLargeIntValue();
        const auto loopMode = get("loop_mode").isNotEmpty() ? get("loop_mode") : get("loopmode");
        if (loopMode == "one_shot" && (trigger.isEmpty() || trigger == "first" || trigger == "legato"))
            ++oneShots;
        const auto loopStart = get("loop_start").isNotEmpty() ? get("loop_start") : get("loopstart");
        const auto loopEnd = get("loop_end").isNotEmpty() ? get("loop_end") : get("loopend");
        if (trigger == "release") {
            // A release's sound plays once through.
        } else if ((loopMode == "loop_continuous" || loopMode == "loop_sustain" || loopMode.isEmpty())
                   && loopStart.isNotEmpty() && loopEnd.isNotEmpty()) {
            zone.loopStart = loopStart.getLargeIntValue();
            zone.loopEnd = loopEnd.getLargeIntValue();
        } else if (loopMode == "loop_continuous" || loopMode == "loop_sustain") {
            // The loop the file carries (a WAV's sampler chunk), from its
            // header alone: nothing decoded for it.
            if (formats.getNumKnownFormats() == 0)
                formats.registerBasicFormats();
            if (const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(sampleFile)); reader != nullptr) {
                const auto start = reader->metadataValues.getValue("Loop0Start", "-1").getLargeIntValue();
                const auto end = reader->metadataValues.getValue("Loop0End", "-1").getLargeIntValue();
                if (start >= 0 && end > start && end <= reader->lengthInSamples) {
                    zone.loopStart = start;
                    zone.loopEnd = end;
                }
            }
        }
        // Choke: a region of group n is cut by a note of the group its off_by names.
        zone.group = std::max(0, get("group").getIntValue());
        zone.offBy = std::max(0, get("off_by").getIntValue());
        if (const auto length = get("seq_length").getIntValue(); length > 1) {
            zone.sequenceLength = std::min(length, 64);
            zone.sequencePosition = std::clamp(get("seq_position").getIntValue(), 1, zone.sequenceLength);
        } else if (get("lorand").isNotEmpty() || get("hirand").isNotEmpty()) {
            // A random choice among n, played here in turn.
            const auto low = get("lorand").getFloatValue();
            const auto high = get("hirand").isNotEmpty() ? get("hirand").getFloatValue() : 1.0f;
            if (high > low && high - low < 0.99f) {
                zone.sequenceLength = std::clamp(static_cast<int>(std::lround(1.0f / (high - low))), 1, 64);
                zone.sequencePosition = std::clamp(static_cast<int>(std::lround(low / (high - low))) + 1, 1,
                                                   zone.sequenceLength);
            }
        }
        zones.push_back(zone);
    }
    if (zones.empty()) {
        error = "no playable region in " + file.getFullPathName();
        return {};
    }
    juce::var settings;
    if (regions > 0 && oneShots == regions) {
        auto* values = new juce::DynamicObject();
        values->setProperty("one-shot", 1);
        settings = juce::var(values);
    }
    // What the map carries beyond its zones.
    auto* extras = new juce::DynamicObject();
    const juce::var extrasVar(extras);
    if (!controllers.empty()) {
        auto* starts = new juce::DynamicObject();
        for (const auto& [number, value] : controllers)
            if (number >= 0 && number < 128)
                starts->setProperty(juce::Identifier(juce::String(number)), std::clamp(value, 0.0, 127.0));
        extras->setProperty("controllers", juce::var(starts));
    }
    // The controls a card shows: those the zones read, by their numbers
    // (an SFZ numbers a piece's together), named as the SFZ names them.
    juce::Array<juce::var> controls;
    for (const auto number : readControllers) {
        auto* item = new juce::DynamicObject();
        item->setProperty("cc", number);
        item->setProperty("label", labels.contains(number) ? labels[number] : "CC " + juce::String(number));
        controls.add(juce::var(item));
    }
    if (!controls.isEmpty())
        extras->setProperty("controls", controls);
    if (!usedCurves.empty()) {
        juce::Array<juce::var> list;
        for (const auto index : usedCurves) {
            juce::Array<juce::var> values;
            for (const auto value : curves[index])
                values.add(value);
            list.add(values);
        }
        extras->setProperty("curves", list);
    }
    if (!switchKeys.empty()) {
        auto* keyswitches = new juce::DynamicObject();
        juce::Array<juce::var> keys;
        for (const auto key : switchKeys)
            keys.add(key);
        keyswitches->setProperty("keys", keys);
        const auto start = defaultSwitch.isNotEmpty() ? sfzNote(defaultSwitch, -1) : -1;
        keyswitches->setProperty("start", switchKeys.contains(start) ? start : *switchKeys.begin());
        if (!switchLabels.empty()) {
            auto* names = new juce::DynamicObject();
            for (const auto& [key, label] : switchLabels)
                names->setProperty(juce::Identifier(juce::String(key)), label);
            keyswitches->setProperty("labels", juce::var(names));
        }
        extras->setProperty("keyswitches", juce::var(keyswitches));
    }
    error.clear();
    return mapVar(file.getFileNameWithoutExtension(), zones, settings, extrasVar);
}

juce::String samplerMapToSfz(const juce::var& json, const juce::File& sfzFile)
{
    juce::String out;
    out << "// " << json.getProperty("name", {}).toString() << " - written by CyDAW\n";
    // Its controllers' names and starting values, its keyswitches, its curves.
    const auto* starts = json.getProperty("controllers", {}).getDynamicObject();
    const auto* controls = json.getProperty("controls", {}).getArray();
    const auto keyswitches = json.getProperty("keyswitches", {});
    if (starts != nullptr || controls != nullptr || keyswitches.isObject()) {
        out << "<control>\n";
        if (controls != nullptr)
            for (const auto& item : *controls)
                out << "label_cc" << static_cast<int>(item.getProperty("cc", 0)) << "="
                    << item.getProperty("label", {}).toString() << "\n";
        if (starts != nullptr)
            for (const auto& [name, value] : starts->getProperties())
                out << "set_cc" << name.toString() << "=" << juce::String(static_cast<double>(value), 2) << "\n";
    }
    if (const auto* curves = json.getProperty("curves", {}).getArray())
        for (int index = 0; index < curves->size(); ++index) {
            // Numbered past SFZ's own (0..6).
            out << "<curve> curve_index=" << (index + 10) << "\n";
            if (const auto* values = (*curves)[index].getArray())
                for (int at = 0; at < values->size(); ++at)
                    out << "v" << juce::String(at).paddedLeft('0', 3) << "=" << juce::String(static_cast<double>((*values)[at]), 4)
                        << (at % 8 == 7 ? "\n" : " ");
            out << "\n";
        }
    if (keyswitches.isObject()) {
        out << "<global>";
        if (const auto start = static_cast<int>(keyswitches.getProperty("start", -1)); start >= 0)
            out << " sw_default=" << start;
        out << "\n";
    }
    for (const auto& zone : zonesOf(json)) {
        const auto sample = resolveSamplePath(zone.file);
        const auto path = sample.isAChildOf(sfzFile.getParentDirectory())
                              ? sample.getRelativePathFrom(sfzFile.getParentDirectory())
                              : sample.getFullPathName();
        out << "<region> sample=" << path.replaceCharacter('\\', '/') << "\n";
        out << " pitch_keycenter=" << zone.root << " lokey=" << zone.lowKey << " hikey=" << zone.highKey
            << " lovel=" << zone.lowVelocity << " hivel=" << zone.highVelocity;
        if (zone.tune != 0.0f)
            out << " tune=" << juce::String(zone.tune, 1);
        if (zone.gain != 0.0f)
            out << " volume=" << juce::String(zone.gain, 2);
        if (zone.pan != 0.0f)
            out << " pan=" << juce::String(zone.pan * 100.0f, 1);
        if (zone.start > 0)
            out << " offset=" << zone.start;
        if (zone.end >= 0)
            out << " end=" << zone.end;
        if (zone.loopStart >= 0 && zone.loopEnd > zone.loopStart)
            out << " loop_mode=loop_continuous loop_start=" << zone.loopStart << " loop_end=" << zone.loopEnd;
        if (zone.sequenceLength > 1)
            out << " seq_length=" << zone.sequenceLength << " seq_position=" << zone.sequencePosition;
        if (zone.group != 0)
            out << " group=" << zone.group;
        if (zone.offBy != 0)
            out << " off_by=" << zone.offBy;
        if (zone.trigger.isNotEmpty())
            out << " trigger=" << zone.trigger;
        if (zone.releaseDecay > 0.0f)
            out << " rt_decay=" << juce::String(zone.releaseDecay, 2);
        if (zone.keyswitch >= 0)
            out << " sw_last=" << zone.keyswitch;
        for (const auto& rule : zone.rules) {
            const auto cc = juce::String(rule.controller);
            if (rule.kind == "range")
                out << " locc" << cc << "=" << juce::String(rule.a, 0) << " hicc" << cc << "=" << juce::String(rule.b, 0);
            else if (rule.kind == "fadeIn")
                out << " xfin_locc" << cc << "=" << juce::String(rule.a, 0) << " xfin_hicc" << cc << "=" << juce::String(rule.b, 0);
            else if (rule.kind == "fadeOut")
                out << " xfout_locc" << cc << "=" << juce::String(rule.a, 0) << " xfout_hicc" << cc << "=" << juce::String(rule.b, 0);
            else {
                const auto opcode = rule.kind == "level"    ? "amplitude"
                                    : rule.kind == "volume" ? "volume"
                                    : rule.kind == "pan"    ? "pan"
                                    : rule.kind == "tune"   ? "tune"
                                                            : "offset";
                const auto amount = rule.kind == "pan" ? rule.a * 100.0f : rule.a;
                out << " " << opcode << "_oncc" << cc << "=" << juce::String(amount, 2);
                if (rule.curve >= 0)
                    out << " " << opcode << "_curvecc" << cc << "=" << (rule.curve + 10);
            }
        }
        out << "\n";
    }
    return out;
}

} // namespace cydaw::app
