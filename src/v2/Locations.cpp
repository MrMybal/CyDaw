#include "Locations.h"

#include <map>

namespace cydaw::v2::locations {

namespace {

std::mutex& settingsLock()
{
    static std::mutex lock;
    return lock;
}

std::map<Location, juce::File>& chosen()
{
    static std::map<Location, juce::File> folders;
    return folders;
}

std::atomic<bool>& saving()
{
    static std::atomic<bool> on { true };
    return on;
}

juce::File localData()
{
   #if JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::windowsLocalAppData).getChildFile("CyDAW");
   #else
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("CyDAW");
   #endif
}

juce::File documents()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("CyDAW");
}

void save(const std::map<Location, juce::File>& folders)
{
    if (!saving().load())
        return;
    auto* list = new juce::DynamicObject();
    for (const auto& [location, folder] : folders)
        list->setProperty(key(location), folder.getFullPathName());
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "CyDAW Locations");
    root->setProperty("version", 1);
    root->setProperty("folders", juce::var(list));
    const auto file = settingsFile();
    (void) file.getParentDirectory().createDirectory();
    (void) file.replaceWithText(juce::JSON::toString(juce::var(root)));
}

} // namespace

const char* key(const Location location)
{
    switch (location) {
    case Location::banks: return "banks";
    case Location::downloads: return "downloads";
    case Location::cache: return "cache";
    case Location::companion: return "companion";
    case Location::aiModels: return "aiModels";
    case Location::soundModels: return "soundModels";
    case Location::devices: return "devices";
    case Location::pluginExports: return "pluginExports";
    case Location::presets: return "presets";
    case Location::mixes: return "mixes";
    case Location::foundry: return "foundry";
    case Location::processings: return "processings";
    }
    return "";
}

std::optional<Location> fromKey(const juce::String& name)
{
    for (const auto location : allLocations)
        if (name == key(location))
            return location;
    return std::nullopt;
}

juce::File defaultFolder(const Location location)
{
    switch (location) {
    case Location::banks:
        if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_BANKS_DIR", {}); other.isNotEmpty())
            return juce::File(other);
        return localData().getChildFile("Banks");
    case Location::downloads: return localData().getChildFile("Downloads");
    case Location::cache: return localData().getChildFile("Cache");
    // Local, not roaming: with PyTorch for CUDA it weighs a few gigabytes.
    case Location::companion: return localData().getChildFile("companion");
    // Local too: a model weighs from a few hundred megabytes to a gigabyte.
    case Location::aiModels: return localData().getChildFile("Models");
    case Location::soundModels: return documents().getChildFile("Sound Models");
    case Location::devices: return documents().getChildFile("Devices");
    case Location::pluginExports: return documents().getChildFile("Plugin Exports");
    case Location::presets: return documents().getChildFile("Presets");
    case Location::mixes: return documents().getChildFile("Mixes");
    case Location::foundry: return documents().getChildFile("Foundry");
    case Location::processings: return documents().getChildFile("Processings");
    }
    return localData();
}

juce::File folder(const Location location)
{
    {
        const std::scoped_lock lock(settingsLock());
        if (const auto found = chosen().find(location); found != chosen().end())
            return found->second;
    }
    return defaultFolder(location);
}

bool isDefault(const Location location)
{
    const std::scoped_lock lock(settingsLock());
    return !chosen().contains(location);
}

juce::File settingsFile()
{
    if (const auto other = juce::SystemStats::getEnvironmentVariable("CYDAW_LOCATIONS_FILE", {}); other.isNotEmpty())
        return juce::File(other);
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("CyDAW")
        .getChildFile("Locations.json");
}

void load()
{
    const auto json = juce::JSON::parse(settingsFile().loadFileAsString());
    const std::scoped_lock lock(settingsLock());
    chosen().clear();
    if (const auto* folders = json.getProperty("folders", {}).getDynamicObject())
        for (const auto& [name, value] : folders->getProperties())
            if (const auto location = fromKey(name.toString()); location.has_value())
                if (const auto path = value.toString(); juce::File::isAbsolutePath(path))
                    chosen()[*location] = juce::File(path);
}

void setSaving(const bool on)
{
    saving() = on;
}

void set(const Location location, const juce::File& where)
{
    std::map<Location, juce::File> folders;
    {
        const std::scoped_lock lock(settingsLock());
        if (where == juce::File {} || where == defaultFolder(location))
            chosen().erase(location);
        else
            chosen()[location] = where;
        folders = chosen();
    }
    save(folders);
}

bool downloaded(const Location location)
{
    return location == Location::banks || location == Location::downloads || location == Location::cache
           || location == Location::companion || location == Location::aiModels;
}

juce::File inside(const Location location, const juce::File& root)
{
    return root.getChildFile(defaultFolder(location).getFileName());
}

bool movable(const Location location)
{
    return location != Location::downloads && location != Location::pluginExports;
}

juce::Array<juce::File> contentOf(const Location location, const juce::File& where)
{
    juce::Array<juce::File> items;
    if (!where.isDirectory())
        return items;
    switch (location) {
    case Location::banks:
        // A bank is a folder with its bank.json; the lists beside them.
        for (const auto& child : where.findChildFiles(juce::File::findDirectories, false))
            if (child.getChildFile("bank.json").existsAsFile())
                items.add(child);
        for (const auto& pattern : { "banks.json", "manifest.json", "*-tree.json" })
            items.addArray(where.findChildFiles(juce::File::findFiles, false, pattern));
        break;
    case Location::cache:
        // VST3 builds stay: CMake's build folder cannot move (it is made again).
        if (const auto samples = where.getChildFile("samples"); samples.isDirectory())
            items.add(samples);
        break;
    case Location::aiModels:
        // A folder by model, each with its versions.
        for (const auto& child : where.findChildFiles(juce::File::findDirectories, false))
            if (!child.findChildFiles(juce::File::findFiles, true, "receipt.json").isEmpty())
                items.add(child);
        break;
    case Location::companion:
        // The environment itself, when it is one.
        if (where.getChildFile("pyvenv.cfg").existsAsFile())
            items.addArray(where.findChildFiles(juce::File::findFilesAndDirectories, false));
        break;
    case Location::soundModels:
        items.addArray(where.findChildFiles(juce::File::findFiles, false, "*.cysound"));
        break;
    case Location::devices:
        items.addArray(where.findChildFiles(juce::File::findFiles, false, "*.cydawdevice"));
        break;
    case Location::presets:
        // A folder by kind: devices', plug-ins', chains'.
        for (const auto* kind : { "device", "plugin", "chain" })
            if (const auto folder = where.getChildFile(kind); folder.isDirectory())
                items.add(folder);
        break;
    case Location::processings:
        items.addArray(where.findChildFiles(juce::File::findFiles, false, "*.cyprocess"));
        break;
    case Location::mixes:
    case Location::foundry:
        // The sound files made there.
        for (const auto* pattern : { "*.wav", "*.flac", "*.aiff", "*.aif", "*.ogg" })
            items.addArray(where.findChildFiles(juce::File::findFiles, false, pattern));
        break;
    case Location::downloads:
    case Location::pluginExports:
        break;
    }
    return items;
}

// ---- Moving ----------------------------------------------------------------------------

Move::Move(const Location location, juce::File from, juce::File to)
    : juce::Thread("CyDAW location move"), location_(location), from_(std::move(from)), to_(std::move(to))
{
    startThread(juce::Thread::Priority::low);
}

Move::~Move()
{
    stopThread(60'000);
}

Move::Progress Move::progress() const
{
    const std::scoped_lock lock(mutex_);
    return progress_;
}

void Move::cancel()
{
    signalThreadShouldExit();
}

void Move::fail(const juce::String& error)
{
    const std::scoped_lock lock(mutex_);
    progress_.error = error;
    progress_.finished = true;
}

void Move::run()
{
    const auto items = contentOf(location_, from_);
    if (!to_.createDirectory()) {
        fail("cannot make " + to_.getFullPathName());
        return;
    }
    const auto serial = from_.getVolumeSerialNumber();
    const auto sameDrive = serial != 0 && serial == to_.getVolumeSerialNumber();

    // On the same drive an item is renamed at once; the others are copied
    // file by file.
    struct Copy {
        juce::File source;
        juce::File target;
        juce::int64 size {};
    };
    std::vector<Copy> copies;
    juce::Array<juce::File> sources;
    for (const auto& item : items) {
        const auto target = to_.getChildFile(item.getFileName());
        if (sameDrive && !target.exists() && item.moveFileTo(target))
            continue;
        sources.add(item);
        if (item.isDirectory()) {
            for (const auto& entry : juce::RangedDirectoryIterator(item, true, "*", juce::File::findFiles))
                copies.push_back({ entry.getFile(), target.getChildFile(entry.getFile().getRelativePathFrom(item)),
                                   entry.getFileSize() });
        } else {
            copies.push_back({ item, target, item.getSize() });
        }
    }
    {
        const std::scoped_lock lock(mutex_);
        for (const auto& copy : copies)
            progress_.total += copy.size;
    }

    // Copies first: the originals go only once every one is there.
    juce::Array<juce::File> made;
    const auto undo = [&made] {
        for (const auto& file : made)
            (void) file.deleteFile();
    };
    for (const auto& copy : copies) {
        if (threadShouldExit()) {
            undo();
            const std::scoped_lock lock(mutex_);
            progress_.cancelled = progress_.finished = true;
            return;
        }
        {
            const std::scoped_lock lock(mutex_);
            progress_.current = copy.source.getFileName();
        }
        // Already there (a merge): kept as it is.
        const auto there = copy.target.existsAsFile() && copy.target.getSize() == copy.size;
        if (!there) {
            const auto isNew = !copy.target.exists();
            const auto ok = copy.target.getParentDirectory().createDirectory().wasOk() && copy.source.copyFileTo(copy.target);
            if (!ok) {
                undo();
                fail("cannot copy " + copy.source.getFullPathName());
                return;
            }
            if (isNew)
                made.add(copy.target);
        }
        const std::scoped_lock lock(mutex_);
        progress_.done += copy.size;
    }
    for (const auto& source : sources)
        (void) (source.isDirectory() ? source.deleteRecursively() : source.deleteFile());
    const std::scoped_lock lock(mutex_);
    progress_.current.clear();
    progress_.finished = true;
}

} // namespace cydaw::v2::locations
