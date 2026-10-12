#include "AiModels.h"

#include "Locations.h"
#include "SamplerMapFile.h"
#include "Widgets.h"

#include <array>
#include <cstring>

namespace cydaw::v2 {

namespace {

// ---- SHA-256 (FIPS 180-4) -------------------------------------------------------------------

constexpr std::array<std::uint32_t, 64> kRound {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::uint32_t rotate(const std::uint32_t value, const int bits)
{
    return (value >> bits) | (value << (32 - bits));
}

juce::String languageText(const juce::var& value)
{
    if (value.isString())
        return value.toString();
    const auto text = value.getProperty(ui::french() ? "fr" : "en", {}).toString();
    return text.isNotEmpty() ? text : value.getProperty("en", {}).toString();
}

bool makeFolder(const juce::File& folder)
{
    return folder.isDirectory() || folder.createDirectory().wasOk();
}

} // namespace

Sha256::Sha256()
    : state_ { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 }
{
}

void Sha256::block(const std::uint8_t* data)
{
    std::uint32_t w[64];
    for (auto i = 0; i < 16; ++i)
        w[i] = (static_cast<std::uint32_t>(data[i * 4]) << 24) | (static_cast<std::uint32_t>(data[i * 4 + 1]) << 16)
               | (static_cast<std::uint32_t>(data[i * 4 + 2]) << 8) | static_cast<std::uint32_t>(data[i * 4 + 3]);
    for (auto i = 16; i < 64; ++i) {
        const auto s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const auto s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    auto a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    auto e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (auto i = 0; i < 64; ++i) {
        const auto s1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto t1 = h + s1 + choice + kRound[static_cast<std::size_t>(i)] + w[i];
        const auto s0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::add(const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    length_ += size;
    if (filled_ > 0) {
        const auto take = std::min(size, 64 - filled_);
        std::memcpy(buffer_ + filled_, bytes, take);
        filled_ += take;
        bytes += take;
        size -= take;
        if (filled_ < 64)
            return;
        block(buffer_);
        filled_ = 0;
    }
    while (size >= 64) {
        block(bytes);
        bytes += 64;
        size -= 64;
    }
    if (size > 0) {
        std::memcpy(buffer_, bytes, size);
        filled_ = size;
    }
}

juce::String Sha256::hex()
{
    const auto bits = length_ * 8;
    const std::uint8_t one = 0x80;
    const std::uint8_t zero = 0;
    const auto savedLength = length_;
    add(&one, 1);
    while (filled_ != 56)
        add(&zero, 1);
    std::uint8_t tail[8];
    for (auto i = 0; i < 8; ++i)
        tail[i] = static_cast<std::uint8_t>(bits >> (56 - i * 8));
    add(tail, 8);
    length_ = savedLength;
    juce::String out;
    for (const auto word : state_)
        out << juce::String::toHexString(static_cast<juce::int64>(word)).paddedLeft('0', 8);
    return out.toLowerCase();
}

juce::String Sha256::ofFile(const juce::File& file, const std::atomic<bool>* stop)
{
    juce::FileInputStream in(file);
    if (!in.openedOk())
        return {};
    Sha256 hash;
    juce::HeapBlock<char> buffer(1 << 20);
    for (;;) {
        if (stop != nullptr && stop->load())
            return {};
        const auto read = in.read(buffer.get(), 1 << 20);
        if (read <= 0)
            break;
        hash.add(buffer.get(), static_cast<std::size_t>(read));
    }
    return hash.hex();
}

// ---- The catalogue ---------------------------------------------------------------------------

juce::int64 AiModel::bytes() const
{
    juce::int64 total {};
    for (const auto& file : files)
        total += file.size;
    return total;
}

juce::String AiModel::describe() const
{
    return languageText(description);
}

juce::String AiModel::note() const
{
    return languageText(licenceNote);
}

AiModels::AiModels()
{
    loadCatalogue();
}

AiModels::~AiModels()
{
    cancel();
    job_.reset();
}

juce::File AiModels::catalogueFile() const
{
    if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_MODELS_CATALOGUE", {});
        other.isNotEmpty() && juce::File::isAbsolutePath(other))
        return juce::File(other);
    return app::factoryContentFolder().getChildFile("models.json");
}

void AiModels::loadCatalogue()
{
    models_.clear();
    packages_.clear();
    const auto json = juce::JSON::parse(catalogueFile().loadFileAsString());
    if (const auto* list = json.getProperty("engine", {}).getProperty("packages", {}).getArray())
        for (const auto& item : *list)
            packages_.push_back({ item.getProperty("name", {}).toString(), item.getProperty("licence", {}).toString(),
                                  item.getProperty("url", {}).toString() });
    if (const auto* list = json.getProperty("models", {}).getArray())
        for (const auto& item : *list) {
            AiModel model;
            model.id = item.getProperty("id", {}).toString();
            model.version = item.getProperty("version", {}).toString();
            model.title = item.getProperty("title", model.id).toString();
            model.provider = item.getProperty("provider", {}).toString();
            model.kind = item.getProperty("kind", "clap").toString();
            model.isDefault = static_cast<bool>(item.getProperty("default", false));
            model.validated = static_cast<bool>(item.getProperty("validated", true));
            model.description = item.getProperty("description", {});
            model.homepage = item.getProperty("homepage", {}).toString();
            const auto licence = item.getProperty("licence", {});
            model.licence = licence.getProperty("name", {}).toString();
            model.licenceUrl = licence.getProperty("url", {}).toString();
            model.licencePage = licence.getProperty("page", model.homepage).toString();
            model.licenceNote = licence.getProperty("note", {});
            model.requiresAcceptance = static_cast<bool>(licence.getProperty("requiresAcceptance", false));
            const auto source = item.getProperty("source", {});
            model.host = source.getProperty("host", {}).toString();
            model.revision = source.getProperty("revision", {}).toString();
            model.baseUrl = source.getProperty("baseUrl", {}).toString();
            if (const auto* files = item.getProperty("files", {}).getArray())
                for (const auto& file : *files)
                    model.files.push_back({ file.getProperty("path", {}).toString(),
                                            static_cast<juce::int64>(file.getProperty("size", 0)),
                                            file.getProperty("sha256", {}).toString().toLowerCase() });
            model.dimension = static_cast<int>(item.getProperty("dimension", 512));
            model.sampleRate = static_cast<double>(item.getProperty("sampleRate", 48'000.0));
            model.windowSeconds = static_cast<double>(item.getProperty("windowSeconds", 10.0));
            if (const auto* range = item.getProperty("similarity", {}).getArray(); range != nullptr && range->size() == 2) {
                model.similarityLow = static_cast<float>(static_cast<double>((*range)[0]));
                model.similarityHigh = static_cast<float>(static_cast<double>((*range)[1]));
            }
            // A file outside the model's folder is not one of its files.
            const auto safe = std::all_of(model.files.begin(), model.files.end(), [](const AiModel::File& file) {
                return file.path.isNotEmpty() && !file.path.contains("..") && !juce::File::isAbsolutePath(file.path);
            });
            if (model.id.isNotEmpty() && model.version.isNotEmpty() && !model.files.empty() && safe
                && model.baseUrl.startsWithIgnoreCase("http"))
                models_.push_back(std::move(model));
        }
}

const AiModel* AiModels::find(const juce::String& id) const
{
    for (const auto& model : models_)
        if (model.id == id || model.key() == id)
            return &model;
    return nullptr;
}

juce::File AiModels::folderOf(const AiModel& model) const
{
    return locations::folder(Location::aiModels).getChildFile(model.id).getChildFile(model.version);
}

bool AiModels::installed(const juce::String& id) const
{
    const auto* model = find(id);
    if (model == nullptr)
        return false;
    const auto folder = folderOf(*model);
    if (!folder.getChildFile("receipt.json").existsAsFile())
        return false;
    for (const auto& file : model->files)
        if (folder.getChildFile(file.path).getSize() != file.size)
            return false;
    return true;
}

juce::var AiModels::receiptOf(const juce::String& id) const
{
    const auto* model = find(id);
    return model != nullptr ? juce::JSON::parse(folderOf(*model).getChildFile("receipt.json")) : juce::var();
}

// ---- The download -----------------------------------------------------------------------------

class AiModels::Job final : public juce::Thread {
public:
    Job(AiModels& owner, AiModel model, juce::File target, juce::File from, const bool accepted)
        : juce::Thread("CyDAW model install"),
          owner_(owner), model_(std::move(model)), target_(std::move(target)), from_(std::move(from)), accepted_(accepted)
    {
    }

    ~Job() override
    {
        stop_ = true;
        stopThread(30'000);
    }

    void cancel() { stop_ = true; }

    void run() override
    {
        juce::String error;
        const auto ok = install(error);
        const std::scoped_lock lock(owner_.mutex_);
        owner_.progress_.running = false;
        owner_.progress_.error = ok ? juce::String() : error;
        owner_.finished_ = Finished { model_.id, ok, stop_.load() && !ok, error };
    }

private:
    void step(const juce::String& name, const juce::String& file)
    {
        const std::scoped_lock lock(owner_.mutex_);
        owner_.progress_.step = name;
        owner_.progress_.file = file;
    }

    void addDone(const juce::int64 bytes)
    {
        const std::scoped_lock lock(owner_.mutex_);
        owner_.progress_.done += bytes;
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() * 0.001 - started_;
        if (elapsed > 0.5)
            owner_.progress_.bytesPerSecond = static_cast<double>(transferred_ += std::max<juce::int64>(0, bytes)) / elapsed;
    }

    bool install(juce::String& error)
    {
        if (!makeFolder(target_)) {
            error = "cannot make " + target_.getFullPathName();
            return false;
        }
        const auto partials = locations::folder(Location::downloads).getChildFile("models").getChildFile(model_.id + "-" + model_.version);
        if (from_ == juce::File {} && !makeFolder(partials)) {
            error = "cannot make " + partials.getFullPathName();
            return false;
        }
        juce::Array<juce::var> files;
        for (const auto& file : model_.files) {
            if (stop_)
                return fail(error);
            const auto destination = target_.getChildFile(file.path);
            juce::String hash;
            // Already there and whole (a run stopped after it): checked again.
            if (destination.getSize() == file.size) {
                step("verify", file.path);
                hash = Sha256::ofFile(destination, &stop_);
                if (file.sha256.isEmpty() || hash == file.sha256) {
                    addDone(file.size);
                    files.add(entry(file, hash));
                    continue;
                }
                (void) destination.deleteFile();
            }
            if (!makeFolder(destination.getParentDirectory())) {
                error = "cannot make " + destination.getParentDirectory().getFullPathName();
                return false;
            }
            const auto ok = from_ != juce::File {} ? copy(file, destination, hash, error)
                                                   : download(file, partials.getChildFile(file.path + ".part"), destination, hash, error);
            if (!ok)
                return stop_ ? fail(error) : false;
            files.add(entry(file, hash));
        }
        // The receipt last: without it, the model is not installed.
        auto* receipt = new juce::DynamicObject();
        receipt->setProperty("model", model_.key());
        receipt->setProperty("title", model_.title);
        receipt->setProperty("revision", model_.revision);
        receipt->setProperty("source", from_ != juce::File {} ? from_.getFullPathName() : model_.baseUrl);
        receipt->setProperty("installed", juce::Time::getCurrentTime().toISO8601(true));
        auto* licence = new juce::DynamicObject();
        licence->setProperty("name", model_.licence);
        licence->setProperty("url", model_.licenceUrl);
        licence->setProperty("page", model_.licencePage);
        licence->setProperty("requiresAcceptance", model_.requiresAcceptance);
        // Ticked by the person, never by CyDAW: said for the record.
        licence->setProperty("acceptedByPerson", accepted_);
        receipt->setProperty("licence", juce::var(licence));
        receipt->setProperty("files", files);
        if (!target_.getChildFile("receipt.json").replaceWithText(juce::JSON::toString(juce::var(receipt)))) {
            error = "cannot write the receipt in " + target_.getFullPathName();
            return false;
        }
        (void) partials.deleteRecursively();
        return true;
    }

    static juce::var entry(const AiModel::File& file, const juce::String& hash)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty("path", file.path);
        object->setProperty("size", file.size);
        object->setProperty("sha256", hash);
        return juce::var(object);
    }

    bool fail(juce::String& error)
    {
        error = "cancelled";
        return false;
    }

    bool verify(const AiModel::File& file, const juce::File& written, const juce::String& hash, juce::String& error)
    {
        if (written.getSize() != file.size) {
            error = file.path + ": " + juce::String(written.getSize()) + " bytes of " + juce::String(file.size);
            return false;
        }
        if (file.sha256.isNotEmpty() && hash != file.sha256) {
            error = file.path + ": SHA-256 " + hash + " instead of " + file.sha256;
            return false;
        }
        return true;
    }

    bool copy(const AiModel::File& file, const juce::File& destination, juce::String& hash, juce::String& error)
    {
        const auto source = from_.getChildFile(file.path);
        step("verify", file.path);
        if (source.getSize() != file.size) {
            error = file.path + (source.existsAsFile() ? ": not the expected size in " : ": missing in ") + from_.getFullPathName();
            return false;
        }
        hash = Sha256::ofFile(source, &stop_);
        if (stop_)
            return false;
        if (file.sha256.isNotEmpty() && hash != file.sha256) {
            error = file.path + ": not the file expected (SHA-256 differs)";
            return false;
        }
        step("copy", file.path);
        const auto temporary = destination.getSiblingFile(destination.getFileName() + ".part");
        if (!source.copyFileTo(temporary) || !verify(file, temporary, hash, error) || !temporary.moveFileTo(destination)) {
            (void) temporary.deleteFile();
            if (error.isEmpty())
                error = "cannot copy " + file.path + " into " + target_.getFullPathName();
            return false;
        }
        addDone(file.size);
        return true;
    }

    bool download(const AiModel::File& file, const juce::File& partial, const juce::File& destination, juce::String& hash,
                  juce::String& error)
    {
        if (!makeFolder(partial.getParentDirectory())) {
            error = "cannot make " + partial.getParentDirectory().getFullPathName();
            return false;
        }
        const auto url = model_.baseUrl + juce::URL::addEscapeChars(file.path, false).replace("%2F", "/").replace("%2f", "/");
        for (auto attempt = 0; attempt < 4; ++attempt) {
            if (stop_)
                return false;
            if (attempt > 0)
                juce::Thread::sleep(1'500 * attempt);
            // Where a stopped download got to: asked for from there.
            auto have = partial.existsAsFile() ? partial.getSize() : 0;
            if (have > file.size) {
                (void) partial.deleteFile();
                have = 0;
            }
            step("download", file.path);
            auto status = 0;
            juce::StringPairArray headers;
            const auto options = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                     .withConnectionTimeoutMs(30'000)
                                     .withNumRedirectsToFollow(8)
                                     .withStatusCode(&status)
                                     .withResponseHeaders(&headers);
            std::unique_ptr<juce::InputStream> stream;
            if (have > 0 && have < file.size)
                stream = juce::URL(url).createInputStream(options.withExtraHeaders("Range: bytes=" + juce::String(have) + "-"));
            else if (have != file.size)
                stream = juce::URL(url).createInputStream(options);
            if (have != file.size) {
                if (stream == nullptr || status >= 400) {
                    error = file.path + ": HTTP " + juce::String(status) + " (" + model_.host + ")";
                    if (status == 416)
                        (void) partial.deleteFile();
                    continue;
                }
                // The server sent it from its start: written again whole.
                const auto resumed = status == 206 && have > 0;
                if (!resumed)
                    have = 0;
                juce::FileOutputStream out(partial);
                if (!out.openedOk()) {
                    error = "cannot write " + partial.getFullPathName();
                    return false;
                }
                if (!resumed) {
                    out.setPosition(0);
                    out.truncate();
                }
                addDone(have);
                juce::HeapBlock<char> buffer(1 << 16);
                auto written = have;
                while (!stop_) {
                    const auto read = stream->read(buffer.get(), 1 << 16);
                    if (read <= 0)
                        break;
                    if (written + read > file.size) {
                        error = file.path + ": more than " + juce::String(file.size) + " bytes";
                        break;
                    }
                    if (!out.write(buffer.get(), static_cast<std::size_t>(read))) {
                        error = "cannot write " + partial.getFullPathName();
                        break;
                    }
                    written += read;
                    addDone(read);
                }
                out.flush();
                if (stop_)
                    return false;
                if (written != file.size) {
                    // Kept: the next attempt goes on from there.
                    addDone(-written);
                    if (error.isEmpty())
                        error = file.path + ": " + juce::String(written) + " bytes of " + juce::String(file.size);
                    if (written > file.size)
                        (void) partial.deleteFile();
                    continue;
                }
            } else {
                addDone(have);
            }
            step("verify", file.path);
            hash = Sha256::ofFile(partial, &stop_);
            if (stop_)
                return false;
            if (!verify(file, partial, hash, error)) {
                // A file that is not the one expected is not kept.
                (void) partial.deleteFile();
                addDone(-file.size);
                continue;
            }
            if (!partial.moveFileTo(destination)) {
                error = "cannot move into " + destination.getFullPathName();
                return false;
            }
            error.clear();
            return true;
        }
        return false;
    }

    AiModels& owner_;
    AiModel model_;
    juce::File target_;
    juce::File from_;
    bool accepted_ {};
    std::atomic<bool> stop_ {};
    double started_ { juce::Time::getMillisecondCounterHiRes() * 0.001 };
    juce::int64 transferred_ {};
};

bool AiModels::start(const juce::String& id, const bool licenceAccepted, const juce::File& from, juce::String& error)
{
    const auto* model = find(id);
    if (model == nullptr) {
        error = ui::Ls("Modèle inconnu : ", "Unknown model: ") + id;
        return false;
    }
    if (model->requiresAcceptance && !licenceAccepted) {
        // Never accepted for the person: they read it and tick it themselves.
        error = ui::Ls("Lisez et acceptez la licence de ", "Read and accept the licence of ") + model->title + " : "
                + model->licencePage;
        return false;
    }
    {
        const std::scoped_lock lock(mutex_);
        if (progress_.running) {
            error = ui::Ls("Une installation est déjà en cours", "An install is already running");
            return false;
        }
    }
    if (from != juce::File {} && !from.isDirectory()) {
        error = ui::Ls("Pas de dossier ", "No folder ") + from.getFullPathName();
        return false;
    }
    const auto target = folderOf(*model);
    // Free space for what is not there yet, with room to spare.
    auto needed = model->bytes() + 256LL * 1024 * 1024;
    for (const auto& file : model->files)
        if (target.getChildFile(file.path).getSize() == file.size)
            needed -= file.size;
    auto existing = target;
    while (!existing.isDirectory() && existing.getParentDirectory() != existing)
        existing = existing.getParentDirectory();
    if (const auto free = existing.getBytesFreeOnVolume(); free > 0 && free < needed) {
        error = ui::Ls("Pas assez de place sur le disque : il faut ", "Not enough disk space: it needs ")
                + juce::File::descriptionOfSizeInBytes(needed);
        return false;
    }
    job_.reset();
    {
        const std::scoped_lock lock(mutex_);
        progress_ = {};
        progress_.running = true;
        progress_.id = model->id;
        progress_.total = model->bytes();
        progress_.step = from == juce::File {} ? "download" : "copy";
        finished_.reset();
    }
    job_ = std::make_unique<Job>(*this, *model, target, from, licenceAccepted);
    job_->startThread(juce::Thread::Priority::low);
    return true;
}

bool AiModels::install(const juce::String& id, const bool licenceAccepted, juce::String& error)
{
    return start(id, licenceAccepted, {}, error);
}

bool AiModels::importFrom(const juce::String& id, const juce::File& folder, const bool licenceAccepted, juce::String& error)
{
    return start(id, licenceAccepted, folder, error);
}

void AiModels::cancel()
{
    if (job_ != nullptr)
        job_->cancel();
}

bool AiModels::remove(const juce::String& id, juce::String& error)
{
    const auto* model = find(id);
    if (model == nullptr) {
        error = ui::Ls("Modèle inconnu : ", "Unknown model: ") + id;
        return false;
    }
    {
        const std::scoped_lock lock(mutex_);
        if (progress_.running && progress_.id == model->id) {
            error = ui::Ls("Il s'installe en ce moment", "It is being installed");
            return false;
        }
    }
    const auto folder = folderOf(*model);
    if (!folder.isDirectory())
        return true;
    if (!folder.moveToTrash() && !folder.deleteRecursively()) {
        error = ui::Ls("Impossible de retirer ", "Cannot remove ") + folder.getFullPathName();
        return false;
    }
    return true;
}

AiModels::Progress AiModels::progress() const
{
    const std::scoped_lock lock(mutex_);
    return progress_;
}

std::optional<AiModels::Finished> AiModels::takeFinished()
{
    const std::scoped_lock lock(mutex_);
    auto finished = std::move(finished_);
    finished_.reset();
    return finished;
}

} // namespace cydaw::v2
