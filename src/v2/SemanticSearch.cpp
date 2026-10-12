#include "SemanticSearch.h"

#include "Locations.h"
#include "Widgets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace cydaw::v2 {

namespace {

// How CLAP is asked about the categories: "the sound of a kick drum".
constexpr auto promptTemplate = "the sound of ";
// The scale of the categories' softmax, and how sure CLAP must be to name a
// sound nothing else named.
constexpr float categoryScale = 40.0f;
constexpr float categoryFloor = 0.4f;
// A batch: this many files, this much audio at most.
constexpr int batchFiles = 64;
constexpr double batchSeconds = 320.0;
// A long file is heard through this many windows at most.
constexpr int maxWindows = 4;
// A phrase's answer: this many more candidates than shown, for the words to
// lift; and what all of its words found in a sound's names add.
constexpr int candidatesFactor = 3;
constexpr float wordsLift = 0.08f;
// The model let go after this long unused.
constexpr double idleSeconds = 600.0;

juce::AudioFormatManager& formats()
{
    thread_local std::unique_ptr<juce::AudioFormatManager> manager;
    if (manager == nullptr) {
        manager = std::make_unique<juce::AudioFormatManager>();
        manager->registerBasicFormats();
    }
    return *manager;
}

std::uint16_t toHalf(const float value)
{
    std::uint32_t bits {};
    std::memcpy(&bits, &value, sizeof(bits));
    const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
    const auto exponent = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    auto mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10)
            return sign;
        mantissa |= 0x800000u;
        const auto shift = 14 - exponent;
        auto half = static_cast<std::uint16_t>(mantissa >> shift);
        if ((mantissa >> (shift - 1)) & 1u)
            ++half;
        return static_cast<std::uint16_t>(sign | half);
    }
    if (exponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    auto half = static_cast<std::uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
    if (mantissa & 0x1000u)
        ++half;
    return half;
}

float fromHalf(const std::uint16_t half)
{
    const auto sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    auto exponent = static_cast<int>((half >> 10) & 0x1fu);
    auto mantissa = static_cast<std::uint32_t>(half & 0x3ffu);
    std::uint32_t bits {};
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3ffu;
            bits = sign | (static_cast<std::uint32_t>(exponent - 15 + 127) << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | (static_cast<std::uint32_t>(exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float value {};
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void normalise(float* vector, const int dimension)
{
    double sum {};
    for (auto i = 0; i < dimension; ++i)
        sum += static_cast<double>(vector[i]) * vector[i];
    const auto norm = static_cast<float>(std::sqrt(std::max(sum, 1.0e-24)));
    for (auto i = 0; i < dimension; ++i)
        vector[i] /= norm;
}

float dot(const float* a, const float* b, const int dimension)
{
    auto sum = 0.0f;
    for (auto i = 0; i < dimension; ++i)
        sum += a[i] * b[i];
    return sum;
}

std::vector<float> floatsOf(const juce::var& value)
{
    std::vector<float> out;
    if (const auto* list = value.getArray()) {
        out.reserve(static_cast<std::size_t>(list->size()));
        for (const auto& item : *list)
            out.push_back(static_cast<float>(static_cast<double>(item)));
    }
    return out;
}

// A sound's windows, mono at the model's rate: the whole of a short one, a
// few spread over a long one.
bool decodeWindows(const juce::File& file, const double rate, const double windowSeconds, std::vector<std::vector<float>>& out)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formats().createReaderFor(file));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return false;
    const auto sourceRate = reader->sampleRate;
    const auto length = reader->lengthInSamples;
    const auto window = static_cast<juce::int64>(windowSeconds * sourceRate);
    std::vector<juce::int64> starts;
    if (static_cast<double>(length) <= static_cast<double>(window) * 1.15) {
        starts.push_back(0);
    } else {
        const auto count = std::min<juce::int64>(maxWindows, (length + window - 1) / window);
        for (juce::int64 index = 0; index < count; ++index)
            starts.push_back(count > 1 ? index * (length - window) / (count - 1) : 0);
    }
    const auto channels = static_cast<int>(std::min<unsigned>(reader->numChannels, 2));
    for (const auto start : starts) {
        const auto count = static_cast<int>(std::min<juce::int64>(window, length - start));
        if (count <= 0)
            continue;
        juce::AudioBuffer<float> buffer(std::max(1, channels), count);
        if (!reader->read(&buffer, 0, count, start, true, channels > 1))
            return false;
        std::vector<float> mono(static_cast<std::size_t>(count));
        for (auto frame = 0; frame < count; ++frame) {
            auto sum = 0.0f;
            for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
                sum += buffer.getSample(channel, frame);
            mono[static_cast<std::size_t>(frame)] = sum / static_cast<float>(buffer.getNumChannels());
        }
        if (std::abs(sourceRate - rate) < 0.5) {
            out.push_back(std::move(mono));
            continue;
        }
        const auto ratio = sourceRate / rate;
        const auto produced = std::max(1, static_cast<int>(std::floor(static_cast<double>(count) / ratio)) - 4);
        std::vector<float> resampled(static_cast<std::size_t>(produced));
        juce::LagrangeInterpolator interpolator;
        interpolator.process(ratio, mono.data(), resampled.data(), produced);
        out.push_back(std::move(resampled));
    }
    return !out.empty();
}

} // namespace

// ---- The model's process -----------------------------------------------------------------------

class SemanticSearch::Worker final : private juce::Thread {
public:
    Worker() : juce::Thread("CyDAW CLAP output") {}

    ~Worker() override { close(); }

    bool open(const juce::StringArray& command, juce::String& error, const std::atomic<bool>* stop)
    {
        if (!process_.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr)) {
            error = ui::Ls("Python ne démarre pas : ", "Python does not start: ") + command[0];
            return false;
        }
        startThread();
        // The model loads first: long on a processor, the first time.
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 300'000.0;
        while (port_.load() == 0) {
            if (ended_.load() || !process_.isRunning()) {
                juce::Thread::sleep(100);
                const std::scoped_lock lock(outputMutex_);
                error = lastError_.isNotEmpty() ? lastError_ : ui::Ls("le modèle s'est arrêté au chargement", "the model stopped while loading");
                close();
                return false;
            }
            if ((stop != nullptr && stop->load()) || juce::Time::getMillisecondCounterHiRes() > deadline) {
                if (stop == nullptr || !stop->load())
                    error = ui::Ls("le modèle ne répond pas", "the model does not answer");
                close();
                return false;
            }
            juce::Thread::sleep(40);
        }
        if (!socket_.connect("127.0.0.1", port_.load(), 10'000)) {
            error = ui::Ls("pas de connexion au modèle", "no connection to the model");
            close();
            return false;
        }
        return true;
    }

    juce::var request(const juce::var& json, juce::String& error, const int timeoutMs = 600'000)
    {
        const std::scoped_lock lock(requestMutex_);
        if (!socket_.isConnected()) {
            error = ui::Ls("le modèle n'est plus là", "the model is gone");
            return {};
        }
        const auto line = juce::JSON::toString(json, true).replace("\n", " ") + "\n";
        const auto* bytes = line.toRawUTF8();
        const auto size = static_cast<int>(line.getNumBytesAsUTF8());
        for (auto sent = 0; sent < size;) {
            const auto wrote = socket_.write(bytes + sent, size - sent);
            if (wrote <= 0) {
                error = ui::Ls("le modèle n'écoute plus", "the model stopped listening");
                socket_.close();
                return {};
            }
            sent += wrote;
        }
        juce::MemoryBlock received;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        char buffer[1 << 14];
        for (;;) {
            const auto ready = socket_.waitUntilReady(true, 200);
            if (ready < 0) {
                error = ui::Ls("le modèle s'est arrêté", "the model stopped");
                socket_.close();
                return {};
            }
            if (ready == 0) {
                if (juce::Time::getMillisecondCounterHiRes() > deadline || !process_.isRunning()) {
                    error = ui::Ls("le modèle ne répond pas", "the model does not answer");
                    socket_.close();
                    return {};
                }
                continue;
            }
            const auto read = socket_.read(buffer, sizeof(buffer), false);
            if (read <= 0) {
                error = ui::Ls("le modèle s'est arrêté", "the model stopped");
                socket_.close();
                return {};
            }
            received.append(buffer, static_cast<std::size_t>(read));
            if (buffer[read - 1] == '\n')
                break;
        }
        const auto answer = juce::JSON::parse(juce::String::fromUTF8(static_cast<const char*>(received.getData()),
                                                                     static_cast<int>(received.getSize())));
        if (!static_cast<bool>(answer.getProperty("ok", false))) {
            error = answer.getProperty("error", ui::Ls("réponse illisible du modèle", "unreadable answer from the model")).toString();
            return {};
        }
        return answer;
    }

    [[nodiscard]] bool alive() { return socket_.isConnected() && process_.isRunning(); }
    [[nodiscard]] juce::String device() const
    {
        const std::scoped_lock lock(outputMutex_);
        return device_;
    }

    void close()
    {
        if (socket_.isConnected()) {
            const auto* bye = "{\"op\": \"shutdown\"}\n";
            (void) socket_.write(bye, static_cast<int>(std::strlen(bye)));
            socket_.close();
        }
        if (process_.isRunning())
            (void) process_.waitForProcessToFinish(1'500);
        (void) process_.kill();
        signalThreadShouldExit();
        stopThread(3'000);
    }

private:
    // Its output read to the end: a pipe left full would stop it.
    void run() override
    {
        juce::MemoryBlock line;
        while (!threadShouldExit()) {
            char byte {};
            if (process_.readProcessOutput(&byte, 1) <= 0) {
                if (!process_.isRunning())
                    break;
                juce::Thread::sleep(20);
                continue;
            }
            if (byte != '\n') {
                if (line.getSize() < 4096)
                    line.append(&byte, 1);
                continue;
            }
            const auto text = juce::String::fromUTF8(static_cast<const char*>(line.getData()), static_cast<int>(line.getSize())).trim();
            line.reset();
            if (text.isEmpty())
                continue;
            const auto value = juce::JSON::parse(text);
            const std::scoped_lock lock(outputMutex_);
            if (value.isObject() && value.hasProperty("port")) {
                device_ = value.getProperty("device", {}).toString();
                port_ = static_cast<int>(value.getProperty("port", 0));
            } else if (value.isObject() && value.hasProperty("error")) {
                lastError_ = value.getProperty("error", {}).toString();
            } else if (!text.containsIgnoreCase("warning") && !text.startsWith(" ")) {
                lastError_ = text;
            }
        }
        ended_ = true;
    }

    juce::ChildProcess process_;
    juce::StreamingSocket socket_;
    std::mutex requestMutex_;
    mutable std::mutex outputMutex_;
    juce::String lastError_;
    juce::String device_;
    std::atomic<int> port_ {};
    std::atomic<bool> ended_ {};
};

// ---- The vectors ------------------------------------------------------------------------------

class SemanticSearch::Store final {
public:
    Store(juce::String model, const int dimension) : model_(std::move(model)), dimension_(dimension) {}

    [[nodiscard]] int dimension() const noexcept { return dimension_; }
    [[nodiscard]] const juce::String& model() const noexcept { return model_; }

    bool load(const juce::File& file)
    {
        juce::FileInputStream in(file);
        if (!in.openedOk())
            return false;
        juce::BufferedInputStream buffered(in, 1 << 16);
        char magic[6] {};
        if (buffered.read(magic, 6) != 6 || std::memcmp(magic, "CYVEC1", 6) != 0)
            return false;
        if (buffered.readInt() != 1)
            return false;
        const auto dimension = buffered.readInt();
        const auto count = buffered.readInt();
        const auto model = buffered.readString();
        if (dimension != dimension_ || model != model_ || count < 0)
            return false;
        const std::unique_lock lock(mutex_);
        std::vector<std::uint16_t> halves(static_cast<std::size_t>(dimension));
        for (auto row = 0; row < count; ++row) {
            const auto path = buffered.readString();
            const auto size = buffered.readInt64();
            const auto modified = buffered.readInt64();
            const auto bytes = static_cast<int>(halves.size() * sizeof(std::uint16_t));
            if (path.isEmpty() || buffered.read(halves.data(), bytes) != bytes)
                break;
            rows_[path] = static_cast<int>(paths_.size());
            paths_.push_back(path);
            sizes_.push_back(size);
            modified_.push_back(modified);
            for (const auto half : halves)
                data_.push_back(fromHalf(juce::ByteOrder::swapIfBigEndian(half)));
        }
        return true;
    }

    bool save(const juce::File& file) const
    {
        (void) file.getParentDirectory().createDirectory();
        const auto temporary = file.getSiblingFile(file.getFileName() + ".tmp");
        {
            juce::FileOutputStream out(temporary, 1 << 16);
            if (!out.openedOk())
                return false;
            out.setPosition(0);
            out.truncate();
            auto& buffered = out;
            const std::shared_lock lock(mutex_);
            buffered.write("CYVEC1", 6);
            buffered.writeInt(1);
            buffered.writeInt(dimension_);
            buffered.writeInt(static_cast<int>(paths_.size()));
            buffered.writeString(model_);
            std::vector<std::uint16_t> halves(static_cast<std::size_t>(dimension_));
            for (std::size_t row = 0; row < paths_.size(); ++row) {
                buffered.writeString(paths_[row]);
                buffered.writeInt64(sizes_[row]);
                buffered.writeInt64(modified_[row]);
                for (auto i = 0; i < dimension_; ++i)
                    halves[static_cast<std::size_t>(i)]
                        = juce::ByteOrder::swapIfBigEndian(toHalf(data_[row * static_cast<std::size_t>(dimension_) + static_cast<std::size_t>(i)]));
                buffered.write(halves.data(), halves.size() * sizeof(std::uint16_t));
            }
            out.flush();
            if (out.getStatus().failed())
                return false;
        }
        return temporary.moveFileTo(file);
    }

    [[nodiscard]] bool fresh(const juce::String& path, const juce::int64 size, const juce::int64 modified) const
    {
        const std::shared_lock lock(mutex_);
        const auto found = rows_.find(path);
        return found != rows_.end() && sizes_[static_cast<std::size_t>(found->second)] == size
               && modified_[static_cast<std::size_t>(found->second)] == modified;
    }

    void put(const juce::String& path, const juce::int64 size, const juce::int64 modified, const float* vector)
    {
        const std::unique_lock lock(mutex_);
        auto row = 0;
        if (const auto found = rows_.find(path); found != rows_.end()) {
            row = found->second;
        } else {
            row = static_cast<int>(paths_.size());
            rows_[path] = row;
            paths_.push_back(path);
            sizes_.push_back(0);
            modified_.push_back(0);
            data_.resize(data_.size() + static_cast<std::size_t>(dimension_));
        }
        sizes_[static_cast<std::size_t>(row)] = size;
        modified_[static_cast<std::size_t>(row)] = modified;
        std::copy(vector, vector + dimension_, data_.begin() + static_cast<std::ptrdiff_t>(row) * dimension_);
        ++generation_;
    }

    // The rows of files the library no longer has, dropped.
    void keepOnly(const std::set<juce::String>& paths)
    {
        const std::unique_lock lock(mutex_);
        std::vector<juce::String> keptPaths;
        std::vector<juce::int64> keptSizes, keptModified;
        std::vector<float> keptData;
        for (std::size_t row = 0; row < paths_.size(); ++row)
            if (paths.contains(paths_[row])) {
                keptPaths.push_back(paths_[row]);
                keptSizes.push_back(sizes_[row]);
                keptModified.push_back(modified_[row]);
                keptData.insert(keptData.end(), data_.begin() + static_cast<std::ptrdiff_t>(row) * dimension_,
                                data_.begin() + static_cast<std::ptrdiff_t>(row + 1) * dimension_);
            }
        if (keptPaths.size() == paths_.size())
            return;
        paths_ = std::move(keptPaths);
        sizes_ = std::move(keptSizes);
        modified_ = std::move(keptModified);
        data_ = std::move(keptData);
        rows_.clear();
        for (std::size_t row = 0; row < paths_.size(); ++row)
            rows_[paths_[row]] = static_cast<int>(row);
        ++generation_;
    }

    [[nodiscard]] std::optional<std::vector<float>> vectorOf(const juce::String& path) const
    {
        const std::shared_lock lock(mutex_);
        const auto found = rows_.find(path);
        if (found == rows_.end())
            return std::nullopt;
        const auto begin = data_.begin() + static_cast<std::ptrdiff_t>(found->second) * dimension_;
        return std::vector<float>(begin, begin + dimension_);
    }

    // The nearest rows to a unit vector, best first.
    [[nodiscard]] std::vector<std::pair<juce::String, float>> nearest(const std::vector<float>& query, const int limit,
                                                                    const juce::String& skip = {}) const
    {
        std::vector<std::pair<juce::String, float>> out;
        if (static_cast<int>(query.size()) != dimension_)
            return out;
        const std::shared_lock lock(mutex_);
        std::vector<std::pair<float, int>> scored;
        scored.reserve(paths_.size());
        for (std::size_t row = 0; row < paths_.size(); ++row)
            scored.emplace_back(dot(query.data(), data_.data() + row * static_cast<std::size_t>(dimension_), dimension_),
                                static_cast<int>(row));
        const auto keep = std::min<std::size_t>(scored.size(), static_cast<std::size_t>(std::max(1, limit + 1)));
        std::partial_sort(scored.begin(), scored.begin() + static_cast<std::ptrdiff_t>(keep), scored.end(),
                          [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::size_t index = 0; index < keep && static_cast<int>(out.size()) < limit; ++index) {
            const auto& path = paths_[static_cast<std::size_t>(scored[index].second)];
            if (path != skip)
                out.emplace_back(path, scored[index].first);
        }
        return out;
    }

    [[nodiscard]] int size() const
    {
        const std::shared_lock lock(mutex_);
        return static_cast<int>(paths_.size());
    }
    [[nodiscard]] int generation() const noexcept { return generation_.load(); }

private:
    const juce::String model_;
    const int dimension_;
    mutable std::shared_mutex mutex_;
    std::vector<juce::String> paths_;
    std::vector<juce::int64> sizes_;
    std::vector<juce::int64> modified_;
    std::vector<float> data_;
    std::map<juce::String, int> rows_;
    std::atomic<int> generation_ {};
};

// ---- The indexing ------------------------------------------------------------------------------

class SemanticSearch::Indexer final : public juce::Thread {
public:
    struct Item {
        juce::String path;
        juce::int64 size {};
        juce::int64 modified {};
        double length {};
    };

    Indexer(SemanticSearch& owner, AiModel model, std::shared_ptr<Store> store, juce::File storeFile, juce::File work,
            std::vector<Item> items, juce::StringArray uncategorised, const bool categories)
        : juce::Thread("CyDAW semantic index"),
          owner_(owner), search_(&owner), model_(std::move(model)), store_(std::move(store)), storeFile_(std::move(storeFile)),
          work_(std::move(work)), items_(std::move(items)), uncategorised_(std::move(uncategorised)), categories_(categories)
    {
    }

    ~Indexer() override
    {
        stop_ = true;
        stopThread(60'000);
    }

    void cancel() { stop_ = true; }

    void run() override
    {
        juce::String error;
        auto worker = owner_.workerFor(model_, error, &stop_);
        if (worker == nullptr) {
            finish(error);
            return;
        }
        {
            const std::scoped_lock lock(owner_.mutex_);
            owner_.status_.loadingModel = false;
            owner_.status_.workerUp = true;
            owner_.status_.device = worker->device();
            owner_.status_.message = ui::Ls("Indexation des sons…", "Indexing the sounds…");
        }
        owner_.changed();
        const auto work = work_;
        (void) work.createDirectory();
        juce::ThreadPool decoders(std::max(2, juce::SystemStats::getNumCpus() - 2));
        auto sinceSave = 0;
        for (std::size_t start = 0; start < items_.size() && !stop_;) {
            // A batch: so many files, so much audio.
            std::vector<std::size_t> batch;
            auto seconds = 0.0;
            for (auto index = start; index < items_.size() && static_cast<int>(batch.size()) < batchFiles && seconds < batchSeconds;
                 ++index) {
                batch.push_back(index);
                const auto length = std::max(0.1, items_[index].length);
                const auto windows = length <= model_.windowSeconds * 1.15
                                         ? 1.0
                                         : std::min<double>(maxWindows, std::ceil(length / model_.windowSeconds));
                seconds += windows * std::min(length, model_.windowSeconds);
            }
            start += batch.size();
            std::vector<std::vector<std::vector<float>>> decoded(batch.size());
            std::vector<char> ok(batch.size(), 0);
            for (std::size_t slot = 0; slot < batch.size(); ++slot)
                decoders.addJob([this, &decoded, &ok, &batch, slot] {
                    ok[slot] = decodeWindows(juce::File(items_[batch[slot]].path), model_.sampleRate, model_.windowSeconds, decoded[slot])
                                   ? 1
                                   : 0;
                });
            while (decoders.getNumJobs() > 0)
                juce::Thread::sleep(5);
            // The windows one after another in a file, for the model.
            const auto audioFile = work.getChildFile("batch.f32");
            const auto vectorFile = work.getChildFile("vectors.f32");
            juce::Array<juce::var> windows;
            std::vector<std::pair<std::size_t, int>> owners; // (slot, windows)
            {
                (void) audioFile.deleteFile();
                juce::FileOutputStream out(audioFile);
                if (!out.openedOk()) {
                    finish("cannot write " + audioFile.getFullPathName());
                    return;
                }
                juce::int64 offset {};
                for (std::size_t slot = 0; slot < batch.size(); ++slot) {
                    if (ok[slot] == 0) {
                        failed_.fetch_add(1);
                        continue;
                    }
                    for (const auto& window : decoded[slot]) {
                        (void) out.write(window.data(), window.size() * sizeof(float));
                        juce::Array<juce::var> item;
                        item.add(offset);
                        item.add(static_cast<juce::int64>(window.size()));
                        windows.add(item);
                        offset += static_cast<juce::int64>(window.size());
                    }
                    owners.emplace_back(slot, static_cast<int>(decoded[slot].size()));
                }
                out.flush();
            }
            if (!windows.isEmpty()) {
                auto* request = new juce::DynamicObject();
                request->setProperty("op", "embed_audio");
                request->setProperty("file", audioFile.getFullPathName());
                request->setProperty("items", windows);
                request->setProperty("out", vectorFile.getFullPathName());
                const auto answer = worker->request(juce::var(request), error);
                if (!answer.isObject()) {
                    finish(error);
                    return;
                }
                const auto dimension = static_cast<int>(answer.getProperty("dimension", 0));
                if (dimension != model_.dimension) {
                    finish(ui::Ls("le modèle donne des vecteurs de ", "the model gives vectors of ") + juce::String(dimension));
                    return;
                }
                juce::MemoryBlock vectors;
                if (!vectorFile.loadFileAsData(vectors)
                    || vectors.getSize() != static_cast<std::size_t>(windows.size()) * static_cast<std::size_t>(dimension) * sizeof(float)) {
                    finish(ui::Ls("vecteurs illisibles", "unreadable vectors"));
                    return;
                }
                const auto* data = static_cast<const float*>(vectors.getData());
                auto row = 0;
                std::vector<float> mean(static_cast<std::size_t>(dimension));
                for (const auto& [slot, count] : owners) {
                    std::fill(mean.begin(), mean.end(), 0.0f);
                    for (auto window = 0; window < count; ++window, ++row)
                        for (auto i = 0; i < dimension; ++i)
                            mean[static_cast<std::size_t>(i)] += data[static_cast<std::size_t>(row) * static_cast<std::size_t>(dimension) + static_cast<std::size_t>(i)];
                    normalise(mean.data(), dimension);
                    const auto& item = items_[batch[slot]];
                    store_->put(item.path, item.size, item.modified, mean.data());
                }
            }
            done_.fetch_add(static_cast<int>(batch.size()));
            {
                const std::scoped_lock lock(owner_.mutex_);
                owner_.status_.done = done_.load();
                owner_.status_.failed = failed_.load();
                owner_.status_.pending = static_cast<int>(items_.size()) - done_.load();
            }
            owner_.changed();
            if ((sinceSave += static_cast<int>(batch.size())) >= 1'000) {
                sinceSave = 0;
                (void) store_->save(storeFile_);
            }
        }
        (void) audioFile().deleteFile();
        (void) work_.getChildFile("vectors.f32").deleteFile();
        if (!items_.empty())
            (void) store_->save(storeFile_);
        if (!stop_ && categories_ && !uncategorised_.isEmpty())
            classify(*worker);
        finish({});
    }

private:
    juce::File audioFile() const { return work_.getChildFile("batch.f32"); }

    // The sounds nothing named: the category CLAP finds nearest, when clear.
    void classify(Worker& worker)
    {
        {
            const std::scoped_lock lock(owner_.mutex_);
            owner_.status_.message = ui::Ls("Catégories des sons sans nom…", "Categories of the unnamed sounds…");
        }
        owner_.changed();
        juce::Array<juce::var> texts;
        std::vector<int> categoryOf;
        const auto& taxonomy = app::SampleLibrary::categoryTaxonomy();
        for (std::size_t index = 0; index < taxonomy.size(); ++index)
            for (const auto* prompt : taxonomy[index].prompts) {
                texts.add(juce::String(promptTemplate) + prompt);
                categoryOf.push_back(static_cast<int>(index));
            }
        auto* request = new juce::DynamicObject();
        request->setProperty("op", "embed_text");
        request->setProperty("texts", texts);
        juce::String error;
        const auto answer = worker.request(juce::var(request), error);
        const auto* list = answer.getProperty("vectors", {}).getArray();
        if (list == nullptr || list->size() != texts.size())
            return;
        std::vector<std::vector<float>> prompts;
        for (const auto& value : *list)
            prompts.push_back(floatsOf(value));
        std::vector<app::SampleLibrary::Classified> results;
        for (const auto& path : uncategorised_) {
            if (stop_)
                return;
            const auto vector = store_->vectorOf(path);
            if (!vector)
                continue;
            std::vector<float> best(taxonomy.size(), -1.0f);
            for (std::size_t prompt = 0; prompt < prompts.size(); ++prompt) {
                if (static_cast<int>(prompts[prompt].size()) != model_.dimension)
                    continue;
                const auto similarity = dot(vector->data(), prompts[prompt].data(), model_.dimension);
                auto& slot = best[static_cast<std::size_t>(categoryOf[prompt])];
                slot = std::max(slot, similarity);
            }
            // A softmax over the categories that have prompts.
            auto top = -1;
            auto highest = -1.0e9f;
            for (std::size_t index = 0; index < best.size(); ++index)
                if (best[index] > -1.0f && best[index] > highest) {
                    highest = best[index];
                    top = static_cast<int>(index);
                }
            if (top < 0)
                continue;
            auto sum = 0.0;
            for (const auto value : best)
                if (value > -1.0f)
                    sum += std::exp(static_cast<double>(categoryScale * (value - highest)));
            const auto probability = static_cast<float>(1.0 / sum);
            if (probability >= categoryFloor)
                results.push_back({ path, taxonomy[static_cast<std::size_t>(top)].key, probability });
        }
        juce::MessageManager::callAsync([search = search_, results = std::move(results)] {
            if (auto* owner = search.get()) {
                const auto count = owner->library_.applyClassified(results);
                {
                    const std::scoped_lock lock(owner->mutex_);
                    owner->status_.classified = count;
                }
                owner->changed();
            }
        });
    }

    void finish(const juce::String& error)
    {
        {
            const std::scoped_lock lock(owner_.mutex_);
            owner_.status_.running = false;
            owner_.status_.loadingModel = false;
            owner_.status_.error = stop_ ? juce::String() : error;
            owner_.status_.message = error.isNotEmpty() && !stop_ ? error
                                     : stop_                       ? ui::Ls("Indexation arrêtée", "Indexing stopped")
                                                                   : ui::Ls("Index à jour", "Index up to date");
        }
        owner_.changed();
    }

    SemanticSearch& owner_;
    juce::WeakReference<SemanticSearch> search_;
    AiModel model_;
    std::shared_ptr<Store> store_;
    juce::File storeFile_;
    juce::File work_;
    std::vector<Item> items_;
    juce::StringArray uncategorised_;
    bool categories_ {};
    std::atomic<bool> stop_ {};
    std::atomic<int> done_ {};
    std::atomic<int> failed_ {};
};

// ---- The search ---------------------------------------------------------------------------------

SemanticSearch::SemanticSearch(app::SampleLibrary& library, AiModels& models, Companion& companion)
    : library_(library), models_(models), companion_(companion)
{
    loadSettings();
    library_.addListener(this);
    openStore();
    startTimer(1'000);
}

SemanticSearch::~SemanticSearch()
{
    closing_ = true;
    stopTimer();
    library_.removeListener(this);
    if (indexer_ != nullptr) {
        indexer_->cancel();
        indexer_.reset();
    }
    queries_.removeAllJobs(true, 20'000);
    checks_.removeAllJobs(true, 20'000);
    stopWorker();
}

void SemanticSearch::setAutomatic(const bool automatic)
{
    automatic_ = automatic;
    if (automatic_ && !activeModel().isEmpty())
        checkEngine();
}

void SemanticSearch::setOnChange(std::function<void()> callback)
{
    const std::scoped_lock lock(changeMutex_);
    onChange_ = std::move(callback);
}

void SemanticSearch::changed()
{
    const std::scoped_lock lock(changeMutex_);
    if (onChange_)
        onChange_();
}

void SemanticSearch::loadSettings()
{
    autoIndex_ = static_cast<bool>(library_.setting("semanticAutoIndex", true));
    autoCategories_ = static_cast<bool>(library_.setting("semanticAutoCategories", true));
    chosenModel_ = library_.setting("semanticModel", juce::String()).toString();
}

void SemanticSearch::saveSettings()
{
    library_.setSetting("semanticAutoIndex", autoIndex_);
    library_.setSetting("semanticAutoCategories", autoCategories_);
    library_.setSetting("semanticModel", chosenModel_);
}

juce::String SemanticSearch::activeModel() const
{
    if (chosenModel_.isNotEmpty() && models_.installed(chosenModel_))
        return chosenModel_;
    for (const auto& model : models_.models())
        if (model.isDefault && models_.installed(model.id))
            return model.id;
    for (const auto& model : models_.models())
        if (models_.installed(model.id))
            return model.id;
    return {};
}

void SemanticSearch::setActiveModel(const juce::String& id)
{
    chosenModel_ = id;
    saveSettings();
    modelsChanged();
}

void SemanticSearch::modelsChanged()
{
    const auto active = activeModel();
    const auto* model = models_.find(active);
    // Another model, or the cache elsewhere (Options › Emplacements).
    if (active != storeModel_ || (model != nullptr && storeFileOf(*model) != storeFile_)) {
        if (indexer_ != nullptr) {
            indexer_->cancel();
            indexer_.reset();
        }
        stopWorker();
        openStore();
    }
    if (active.isNotEmpty()) {
        indexOwed_ = true;
        indexOwedAt_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (!engine().checked)
            checkEngine();
    }
    changed();
}

juce::File SemanticSearch::storeFolder() const
{
    return library_.cacheFolder().getChildFile("semantic");
}

juce::File SemanticSearch::workFolder() const
{
    return storeFolder().getChildFile("work");
}

juce::File SemanticSearch::storeFileOf(const AiModel& model) const
{
    return storeFolder().getChildFile(model.key().replaceCharacter('@', '_') + ".cyvec");
}

void SemanticSearch::openStore()
{
    const auto active = activeModel();
    std::shared_ptr<Store> store;
    juce::File file;
    if (const auto* model = models_.find(active); model != nullptr) {
        store = std::make_shared<Store>(model->key(), model->dimension);
        file = storeFileOf(*model);
        (void) store->load(file);
    }
    const std::scoped_lock lock(mutex_);
    store_ = std::move(store);
    storeModel_ = active;
    storeFile_ = file;
}

std::shared_ptr<SemanticSearch::Store> SemanticSearch::store() const
{
    const std::scoped_lock lock(mutex_);
    return store_;
}

bool SemanticSearch::available() const
{
    const auto current = store();
    return current != nullptr && current->size() > 0;
}

SemanticSearch::Engine SemanticSearch::engine() const
{
    const std::scoped_lock lock(mutex_);
    return engine_;
}

void SemanticSearch::checkEngine()
{
    {
        const std::scoped_lock lock(mutex_);
        if (engine_.checking)
            return;
        engine_.checking = true;
        engine_.python = companion_.pythonCommand();
    }
    changed();
    checks_.addJob([this] {
        juce::String error;
        const auto answer = companion_.runNow({ "clap-check" }, error, 120'000);
        {
            const std::scoped_lock lock(mutex_);
            engine_.checking = false;
            engine_.checked = true;
            engine_.executable = answer.getProperty("executable", {}).toString();
            engine_.torch = answer.getProperty("torch", {}).toString();
            engine_.transformers = answer.getProperty("transformers", {}).toString();
            engine_.cuda = static_cast<bool>(answer.getProperty("cuda", false));
            engine_.gpu = answer.getProperty("gpu", {}).toString();
            engine_.ready = engine_.torch.isNotEmpty() && engine_.transformers.isNotEmpty();
            engine_.error = engine_.ready ? juce::String()
                            : error.isNotEmpty() ? error
                            : engine_.torch.isEmpty() ? ui::Ls("PyTorch manque", "PyTorch is missing")
                                                      : ui::Ls("transformers manque", "transformers is missing");
        }
        changed();
    });
}

std::shared_ptr<SemanticSearch::Worker> SemanticSearch::workerFor(const AiModel& model, juce::String& error,
                                                                   const std::atomic<bool>* stop)
{
    const std::scoped_lock creating(creating_);
    {
        const std::scoped_lock lock(mutex_);
        if (worker_ != nullptr && workerModel_ == model.key() && worker_->alive()) {
            workerUsed_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
            return worker_;
        }
        status_.loadingModel = true;
        status_.message = ui::Ls("Chargement du modèle…", "Loading the model…");
    }
    changed();
    // One process at a time: another model's goes.
    stopWorker();
    auto worker = std::make_shared<Worker>();
    const auto command = companion_.commandLine({ "clap-worker", "--model", models_.folderOf(model).getFullPathName() });
    // Stopped by whoever asked (an indexing cancelled, CyDAW closing).
    if (!worker->open(command, error, stop != nullptr ? stop : &closing_)) {
        if (closing_.load())
            error = ui::Ls("arrêté", "stopped");
        const std::scoped_lock lock(mutex_);
        status_.loadingModel = false;
        status_.workerUp = false;
        return nullptr;
    }
    const std::scoped_lock lock(mutex_);
    worker_ = worker;
    workerModel_ = model.key();
    workerUsed_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    status_.loadingModel = false;
    status_.workerUp = true;
    status_.device = worker->device();
    if (!status_.running)
        status_.message = ui::Ls("Modèle prêt", "Model ready");
    return worker;
}

void SemanticSearch::stopWorker()
{
    std::shared_ptr<Worker> worker;
    {
        const std::scoped_lock lock(mutex_);
        worker = std::move(worker_);
        worker_.reset();
        workerModel_.clear();
        status_.workerUp = false;
    }
    if (worker != nullptr)
        worker->close();
}

SemanticSearch::Status SemanticSearch::status() const
{
    const std::scoped_lock lock(mutex_);
    return status_;
}

void SemanticSearch::startIndexing()
{
    const auto* model = models_.find(activeModel());
    if (model == nullptr) {
        const std::scoped_lock lock(mutex_);
        status_.error = ui::Ls("Aucun modèle installé", "No model installed");
        return;
    }
    if (indexer_ != nullptr && indexer_->isThreadRunning())
        return;
    indexer_.reset();
    if (storeModel_ != model->id || storeFileOf(*model) != storeFile_)
        openStore();
    auto current = store();
    if (current == nullptr)
        return;
    // What the library has, without a vector or with one of an older file.
    std::vector<Indexer::Item> items;
    std::set<juce::String> present;
    juce::StringArray uncategorised;
    for (const auto& record : library_.samples()) {
        if (!record.analysed)
            continue;
        present.insert(record.path);
        if (!current->fresh(record.path, record.size, record.modified))
            items.push_back({ record.path, record.size, record.modified, record.length });
        if (!record.userEdited && (record.category.isEmpty() || record.classifier == "clap"))
            uncategorised.add(record.path);
    }
    // Forgotten files leave the index.
    if (library_.loaded() && !library_.isScanning())
        current->keepOnly(present);
    indexOwed_ = false;
    {
        const std::scoped_lock lock(mutex_);
        status_.running = true;
        status_.error.clear();
        status_.done = 0;
        status_.failed = 0;
        status_.classified = 0;
        status_.pending = static_cast<int>(items.size());
        status_.message = items.empty() ? ui::Ls("Index à jour", "Index up to date") : ui::Ls("Préparation…", "Preparing…");
    }
    if (items.empty() && (!autoCategories_ || uncategorised.isEmpty())) {
        const std::scoped_lock lock(mutex_);
        status_.running = false;
        return;
    }
    indexer_ = std::make_unique<Indexer>(*this, *model, current, storeFileOf(*model), workFolder(),
                                         std::move(items), std::move(uncategorised), autoCategories_);
    indexer_->startThread(juce::Thread::Priority::low);
    changed();
}

void SemanticSearch::stopIndexing()
{
    if (indexer_ != nullptr) {
        indexer_->cancel();
        indexer_.reset();
    }
    indexOwed_ = false;
    changed();
}

void SemanticSearch::setAutoIndex(const bool on)
{
    autoIndex_ = on;
    saveSettings();
    if (on)
        indexOwed_ = true;
}

void SemanticSearch::setAutoCategories(const bool on)
{
    autoCategories_ = on;
    saveSettings();
}

void SemanticSearch::classifyNow()
{
    const auto previous = autoCategories_;
    autoCategories_ = true;
    startIndexing();
    autoCategories_ = previous;
}

void SemanticSearch::sampleLibraryChanged(const bool structure)
{
    libraryChanged_ = true;
    if (structure) {
        indexOwed_ = true;
        indexOwedAt_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }
}

void SemanticSearch::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (indexer_ != nullptr && !indexer_->isThreadRunning())
        indexer_.reset();
    // The library's sounds with a vector, counted again when either changed.
    if (const auto current = store();
        libraryChanged_ || current.get() != countedStore_ || (current != nullptr && current->generation() != countedGeneration_)) {
        libraryChanged_ = false;
        countedStore_ = current.get();
        countedGeneration_ = current != nullptr ? current->generation() : -1;
        auto indexed = 0;
        if (current != nullptr && current->size() > 0)
            for (const auto& record : library_.samples())
                if (current->fresh(record.path, record.size, record.modified))
                    ++indexed;
        {
            const std::scoped_lock lock(mutex_);
            status_.indexed = indexed;
            status_.total = static_cast<int>(library_.samples().size());
        }
        changed();
    }
    // New sounds indexed once the library is quiet.
    if (indexOwed_ && automatic_ && autoIndex_ && indexer_ == nullptr && library_.loaded() && !library_.isScanning()
        && now - indexOwedAt_ > 3.0 && activeModel().isNotEmpty()) {
        const auto state = engine();
        if (!state.checked && !state.checking)
            checkEngine();
        else if (state.ready)
            startIndexing();
    }
    // The model let go after a while unused (its memory, the graphics card's).
    {
        auto idle = false;
        {
            const std::scoped_lock lock(mutex_);
            idle = worker_ != nullptr && now - workerUsed_ > idleSeconds && !status_.running && searching_.load() == 0;
        }
        if (idle && indexer_ == nullptr)
            stopWorker();
    }
}

// ---- Queries ------------------------------------------------------------------------------------

juce::String SemanticSearch::toEnglish(const juce::String& query)
{
    // Phrases first, then words; what is not here stays (often English).
    static const std::vector<std::pair<const char*, const char*>> phrases {
        { "grosse caisse", "kick drum" }, { "caisse claire", "snare drum" }, { "charleston ouvert", "open hi-hat" },
        { "charleston ferme", "closed hi-hat" }, { "cymbale crash", "crash cymbal" }, { "cymbale ride", "ride cymbal" },
        { "piano electrique", "electric piano" }, { "guitare electrique", "electric guitar" },
        { "guitare acoustique", "acoustic guitar" }, { "contrebasse", "double bass" }, { "boite a rythmes", "drum machine" },
        { "bruit blanc", "white noise" }, { "coup de", "hit of" }, { "effet sonore", "sound effect" },
    };
    static const std::map<juce::String, const char*> words {
        { "batterie", "drums" }, { "caisse", "drum" }, { "tambour", "drum" }, { "charleston", "hi-hat" },
        { "charley", "hi-hat" }, { "cymbale", "cymbal" }, { "cymbales", "cymbals" }, { "basse", "bass" },
        { "basses", "basses" }, { "nappe", "pad" }, { "nappes", "pads" }, { "cordes", "strings" }, { "corde", "string" },
        { "voix", "voice" }, { "chant", "singing" }, { "chanteuse", "female singer" }, { "chanteur", "male singer" },
        { "choeur", "choir" }, { "chœur", "choir" }, { "guitare", "guitar" }, { "guitares", "guitars" }, { "clavier", "keyboard" },
        { "orgue", "organ" }, { "synthe", "synth" }, { "synthes", "synths" }, { "synthetiseur", "synthesizer" },
        { "violon", "violin" }, { "violoncelle", "cello" }, { "alto", "viola" }, { "harpe", "harp" }, { "flute", "flute" },
        { "trompette", "trumpet" }, { "cor", "horn" }, { "cuivres", "brass" }, { "cuivre", "brass" }, { "bois", "woodwind" },
        { "clarinette", "clarinet" }, { "hautbois", "oboe" }, { "basson", "bassoon" }, { "cloche", "bell" },
        { "cloches", "bells" }, { "clochette", "small bell" }, { "accord", "chord" }, { "accords", "chords" },
        { "melodie", "melody" }, { "boucle", "loop" }, { "boucles", "loops" }, { "rythme", "rhythm" }, { "bruit", "noise" },
        { "bruits", "noises" }, { "vent", "wind" }, { "pluie", "rain" }, { "eau", "water" }, { "feu", "fire" },
        { "orage", "thunderstorm" }, { "tonnerre", "thunder" }, { "oiseau", "bird" }, { "oiseaux", "birds" },
        { "chien", "dog" }, { "chat", "cat" }, { "porte", "door" }, { "pas", "footsteps" }, { "foule", "crowd" },
        { "ville", "city" }, { "moteur", "engine" }, { "voiture", "car" }, { "explosion", "explosion" },
        { "applaudissements", "applause" }, { "claquement", "snap" }, { "souffle", "breath" }, { "respiration", "breathing" },
        { "rire", "laughter" }, { "cri", "scream" }, { "montee", "riser" }, { "descente", "downlifter" },
        { "frappe", "hit" }, { "coup", "hit" }, { "sombre", "dark" }, { "brillant", "bright" }, { "brillante", "bright" },
        { "doux", "soft" }, { "douce", "soft" }, { "chaud", "warm" }, { "chaude", "warm" }, { "froid", "cold" },
        { "froide", "cold" }, { "agressif", "aggressive" }, { "agressive", "aggressive" }, { "court", "short" },
        { "courte", "short" }, { "longue", "long" }, { "grave", "low" }, { "graves", "low" }, { "aigu", "high-pitched" },
        { "aigue", "high-pitched" }, { "metallique", "metallic" }, { "sature", "distorted" }, { "saturee", "distorted" },
        { "distordu", "distorted" }, { "reverbere", "reverberant" }, { "reverb", "reverb" }, { "reverbe", "reverb" },
        { "echo", "echo" }, { "lointain", "distant" }, { "proche", "close" }, { "ancien", "old" }, { "vieux", "old" },
        { "craquement", "crackle" }, { "craquements", "crackles" }, { "vinyle", "vinyl" }, { "electronique", "electronic" },
        { "acoustique", "acoustic" }, { "analogique", "analog" }, { "numerique", "digital" }, { "gros", "big" },
        { "grosse", "big" }, { "petit", "small" }, { "petite", "small" }, { "lourd", "heavy" }, { "lourde", "heavy" },
        { "leger", "light" }, { "legere", "light" }, { "rapide", "fast" }, { "lent", "slow" }, { "lente", "slow" },
        { "triste", "sad" }, { "joyeux", "happy" }, { "sec", "dry" }, { "seche", "dry" }, { "mouille", "wet" },
        { "profond", "deep" }, { "profonde", "deep" }, { "epais", "thick" }, { "fin", "thin" }, { "fine", "thin" },
        { "percutant", "punchy" }, { "percutante", "punchy" }, { "claquant", "snappy" }, { "etouffe", "muffled" },
        { "aerien", "airy" }, { "aerienne", "airy" }, { "spatial", "spacey" }, { "inquietant", "eerie" },
        { "effrayant", "scary" }, { "balais", "brushes" }, { "balai", "brush" }, { "feutre", "felt" }, { "feutree", "felt" },
        { "sourd", "muffled" }, { "sourde", "muffled" }, { "tambourin", "tambourine" }, { "baguettes", "sticks" },
        { "pince", "plucked" }, { "pincee", "plucked" }, { "frotte", "bowed" }, { "archet", "bow" },
        { "un", "a" }, { "une", "a" }, { "le", "the" }, { "la", "the" }, { "les", "the" }, { "de", "of" }, { "du", "of" },
        { "des", "of" }, { "avec", "with" }, { "sans", "without" }, { "et", "and" }, { "tres", "very" }, { "plus", "more" },
        { "son", "sound" }, { "sons", "sounds" },
    };
    auto text = query.toLowerCase().trim();
    // Accents off: "métallique" is found as "metallique".
    static const char* accents[][2] = { { "é", "e" }, { "è", "e" }, { "ê", "e" }, { "ë", "e" }, { "à", "a" }, { "â", "a" },
                                        { "î", "i" }, { "ï", "i" }, { "ô", "o" }, { "ù", "u" }, { "û", "u" }, { "ç", "c" },
                                        { "œ", "oe" } };
    for (const auto& pair : accents)
        text = text.replace(juce::String::fromUTF8(pair[0]), pair[1]);
    text = text.replace("'", " ").replace(juce::String::fromUTF8("\xe2\x80\x99"), " ");
    for (const auto& [french, english] : phrases)
        text = text.replace(french, english);
    juce::StringArray out;
    for (const auto& word : juce::StringArray::fromTokens(text, " ", "")) {
        if (word.isEmpty())
            continue;
        // The French "l'", "d'" gone; the rest translated when known.
        if (word == "l" || word == "d")
            continue;
        const auto found = words.find(word);
        out.add(found != words.end() ? juce::String(found->second) : word);
    }
    return out.joinIntoString(" ");
}

SemanticSearch::Answer SemanticSearch::run(const Query& query)
{
    Answer answer;
    answer.id = query.id;
    answer.query = query.text;
    answer.similarTo = query.similarTo;
    const auto* model = models_.find(activeModel());
    auto current = store();
    if (model == nullptr || current == nullptr) {
        answer.failed = true;
        answer.error = ui::Ls("Aucun modèle de recherche installé", "No search model installed");
        return answer;
    }
    std::vector<float> vector;
    if (query.similarTo.isNotEmpty()) {
        const auto found = current->vectorOf(query.similarTo);
        if (!found) {
            answer.failed = true;
            answer.error = ui::Ls("Ce son n'est pas encore indexé", "This sound is not indexed yet");
            return answer;
        }
        vector = *found;
    } else {
        answer.english = toEnglish(query.text);
        if (answer.english.isEmpty()) {
            answer.failed = true;
            answer.error = ui::Ls("Rien à chercher", "Nothing to search");
            return answer;
        }
        juce::String error;
        auto worker = workerFor(*model, error, &closing_);
        if (worker == nullptr) {
            answer.failed = true;
            answer.error = error;
            return answer;
        }
        // The phrase as it is, and as the categories are asked about: the two
        // meanings averaged.
        auto* request = new juce::DynamicObject();
        request->setProperty("op", "embed_text");
        juce::Array<juce::var> texts;
        texts.add(answer.english);
        texts.add(juce::String(promptTemplate) + answer.english);
        request->setProperty("texts", texts);
        const auto reply = worker->request(juce::var(request), error, 60'000);
        const auto* list = reply.getProperty("vectors", {}).getArray();
        if (list == nullptr || list->isEmpty()) {
            answer.failed = true;
            answer.error = error.isNotEmpty() ? error : ui::Ls("pas de réponse du modèle", "no answer from the model");
            return answer;
        }
        vector.assign(static_cast<std::size_t>(model->dimension), 0.0f);
        for (const auto& item : *list) {
            const auto part = floatsOf(item);
            if (static_cast<int>(part.size()) == model->dimension)
                for (std::size_t i = 0; i < part.size(); ++i)
                    vector[i] += part[i];
        }
        normalise(vector.data(), model->dimension);
        {
            const std::scoped_lock lock(mutex_);
            workerUsed_ = juce::Time::getMillisecondCounterHiRes() * 0.001;
        }
    }
    const auto range = std::max(0.01f, model->similarityHigh - model->similarityLow);
    // A sound's likeness to another runs higher than to a phrase.
    const auto low = query.similarTo.isNotEmpty() ? 0.3f : model->similarityLow;
    const auto span = query.similarTo.isNotEmpty() ? 0.7f : range;
    answer.low = low;
    answer.span = span;
    const auto pool = query.similarTo.isNotEmpty() ? query.limit : query.limit * candidatesFactor;
    for (const auto& [path, similarity] : current->nearest(vector, pool, query.similarTo))
        answer.hits.push_back({ path, similarity, juce::jlimit(0.0f, 100.0f, (similarity - low) / span * 100.0f) });
    answer.limit = query.limit;
    return answer;
}

void SemanticSearch::rerank(Answer& answer, app::SampleLibrary& library, const int limit)
{
    const auto keep = std::max(1, std::min(limit, answer.limit > 0 ? answer.limit : limit));
    if (answer.failed || answer.similarTo.isNotEmpty() || answer.hits.empty()) {
        if (static_cast<int>(answer.hits.size()) > keep)
            answer.hits.resize(static_cast<std::size_t>(keep));
        return;
    }
    // The words of the phrase in English (as the model was asked), without the
    // small ones; each counts once, found in a name in English or in French.
    static const juce::StringArray ignored { "the", "and", "with", "sound", "sounds", "of", "for", "very", "more", "without" };
    juce::StringArray english;
    for (auto word : juce::StringArray::fromTokens(answer.english, " ,;.'", ""))
        if (word = word.trim().toLowerCase(); word.length() >= 3 && !ignored.contains(word))
            english.addIfNotAlreadyThere(word);
    if (english.isEmpty()) {
        if (static_cast<int>(answer.hits.size()) > keep)
            answer.hits.resize(static_cast<std::size_t>(keep));
        return;
    }
    for (auto& hit : answer.hits) {
        const auto* record = library.find(hit.path);
        if (record == nullptr)
            continue;
        auto haystack = record->name + " " + record->category + " " + record->instrument + " " + library.sourceTitle(*record) + " "
                        + record->file().getParentDirectory().getFileName() + " "
                        + record->file().getParentDirectory().getParentDirectory().getFileName();
        if (const auto* info = app::SampleLibrary::categoryInfo(record->category))
            haystack << " " << juce::String::fromUTF8(info->french) << " " << info->english;
        haystack = toEnglish(haystack.replaceCharacters("_-/()", "     ")) + " " + haystack.toLowerCase();
        auto found = 0;
        for (const auto& word : english)
            if (haystack.contains(word))
                ++found;
        if (found > 0) {
            hit.similarity += wordsLift * static_cast<float>(found) / static_cast<float>(english.size());
            hit.score = juce::jlimit(0.0f, 100.0f, (hit.similarity - answer.low) / answer.span * 100.0f);
        }
    }
    std::stable_sort(answer.hits.begin(), answer.hits.end(), [](const Hit& a, const Hit& b) { return a.similarity > b.similarity; });
    if (static_cast<int>(answer.hits.size()) > keep)
        answer.hits.resize(static_cast<std::size_t>(keep));
}

void SemanticSearch::post(Answer answer)
{
    {
        const std::scoped_lock lock(mutex_);
        answers_[answer.owner] = std::move(answer);
    }
    changed();
}

int SemanticSearch::ask(Query query)
{
    {
        const std::scoped_lock lock(mutex_);
        latest_[query.owner] = query.id;
    }
    searching_.fetch_add(1);
    queries_.addJob([this, query] {
        // One asked since by the same owner makes this one useless.
        auto newest = false;
        {
            const std::scoped_lock lock(mutex_);
            newest = latest_[query.owner] == query.id;
        }
        if (newest) {
            auto answer = run(query);
            answer.owner = query.owner;
            post(std::move(answer));
        }
        searching_.fetch_sub(1);
        changed();
    });
    return query.id;
}

int SemanticSearch::searchText(const juce::String& text, const int limit, const int owner)
{
    return ask({ nextQuery_++, text.trim(), {}, std::max(1, limit), owner });
}

int SemanticSearch::searchSimilar(const juce::String& path, const int limit, const int owner)
{
    return ask({ nextQuery_++, {}, path, std::max(1, limit), owner });
}

std::optional<SemanticSearch::Answer> SemanticSearch::takeAnswer(const int owner)
{
    const std::scoped_lock lock(mutex_);
    const auto found = answers_.find(owner);
    if (found == answers_.end())
        return std::nullopt;
    auto answer = std::move(found->second);
    answers_.erase(found);
    return answer;
}

SemanticSearch::Answer SemanticSearch::searchNow(const juce::String& text, const int limit, const int timeoutMs)
{
    juce::ignoreUnused(timeoutMs);
    return run({ nextQuery_++, text.trim(), {}, std::max(1, limit) });
}

SemanticSearch::Answer SemanticSearch::similarNow(const juce::String& path, const int limit, const int timeoutMs)
{
    juce::ignoreUnused(timeoutMs);
    return run({ nextQuery_++, {}, path, std::max(1, limit) });
}

} // namespace cydaw::v2
