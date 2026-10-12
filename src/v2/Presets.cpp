#include "Presets.h"

#include "Locations.h"
#include "SamplerMapFile.h"

#include <algorithm>

namespace cydaw::v2::presets {

namespace {

constexpr auto format = "cydaw-preset";

juce::String fileNameOf(const juce::String& name)
{
    return juce::File::createLegalFileName(name.trim()).substring(0, 120);
}

void addFrom(const juce::File& folder, const bool factory, std::vector<Entry>& out)
{
    if (!folder.isDirectory())
        return;
    auto files = folder.findChildFiles(juce::File::findFiles, false, juce::String("*") + fileExtension);
    files.sort();
    for (const auto& file : files)
        out.push_back({ file.getFileNameWithoutExtension(), file, factory });
}

} // namespace

juce::String folderName(const juce::String& target)
{
    const auto name = juce::File::createLegalFileName(target.trim()).substring(0, 96);
    return name.isNotEmpty() ? name : juce::String("other");
}

juce::File userFolder(const juce::String& kind, const juce::String& target)
{
    return locations::folder(Location::presets).getChildFile(kind).getChildFile(folderName(target));
}

juce::File factoryFolder(const juce::String& kind, const juce::String& target)
{
    return app::factoryContentFolder().getChildFile("Presets").getChildFile(kind).getChildFile(folderName(target));
}

std::vector<Entry> list(const juce::String& kind, const juce::String& target)
{
    std::vector<Entry> entries;
    addFrom(factoryFolder(kind, target), true, entries);
    addFrom(userFolder(kind, target), false, entries);
    return entries;
}

const Entry* find(const std::vector<Entry>& entries, const juce::String& name)
{
    // The person's before CyDAW's of the same name.
    const Entry* found = nullptr;
    for (const auto& entry : entries)
        if (entry.name == name && (found == nullptr || found->factory))
            found = &entry;
    return found;
}

bool save(const juce::String& kind, const juce::String& target, const juce::String& name, const juce::var& state,
          juce::String& error)
{
    const auto fileName = fileNameOf(name);
    if (fileName.isEmpty()) {
        error = "a preset needs a name";
        return false;
    }
    if (!state.isObject()) {
        error = "nothing to keep";
        return false;
    }
    const auto folder = userFolder(kind, target);
    if (folder.createDirectory().failed() && !folder.isDirectory()) {
        error = "cannot make " + folder.getFullPathName();
        return false;
    }
    auto* document = new juce::DynamicObject();
    document->setProperty("format", format);
    document->setProperty("version", 1);
    document->setProperty("kind", kind);
    document->setProperty("target", target);
    document->setProperty("name", name.trim());
    document->setProperty("created", juce::Time::getCurrentTime().toISO8601(true));
    document->setProperty("state", state);
    const auto file = folder.getChildFile(fileName + fileExtension);
    if (!file.replaceWithText(juce::JSON::toString(juce::var(document), true))) {
        error = "cannot write " + file.getFullPathName();
        return false;
    }
    return true;
}

juce::var read(const Entry& entry, const juce::String& kind, juce::String& error)
{
    if (entry.file.getSize() > 256 * 1024 * 1024) {
        error = "the preset is too large";
        return {};
    }
    const auto document = juce::JSON::parse(entry.file.loadFileAsString());
    if (document.getProperty("format", {}).toString() != format || document.getProperty("kind", {}).toString() != kind) {
        error = "not a " + kind + " preset: " + entry.file.getFileName();
        return {};
    }
    if (static_cast<int>(document.getProperty("version", 0)) > 1) {
        error = "a newer preset version";
        return {};
    }
    const auto state = document.getProperty("state", {});
    if (!state.isObject()) {
        error = "the preset holds no state";
        return {};
    }
    return state;
}

bool remove(const Entry& entry, juce::String& error)
{
    if (entry.factory) {
        error = "CyDAW's presets stay";
        return false;
    }
    if (!entry.file.moveToTrash() && !entry.file.deleteFile()) {
        error = "cannot remove " + entry.file.getFullPathName();
        return false;
    }
    return true;
}

bool rename(const Entry& entry, const juce::String& name, juce::String& error)
{
    if (entry.factory) {
        error = "CyDAW's presets stay";
        return false;
    }
    const auto fileName = fileNameOf(name);
    if (fileName.isEmpty()) {
        error = "a preset needs a name";
        return false;
    }
    const auto target = entry.file.getSiblingFile(fileName + fileExtension);
    if (target != entry.file && target.exists()) {
        error = "a preset is already called " + name.trim();
        return false;
    }
    auto document = juce::JSON::parse(entry.file.loadFileAsString());
    if (auto* object = document.getDynamicObject())
        object->setProperty("name", name.trim());
    if (!target.replaceWithText(juce::JSON::toString(document, true))) {
        error = "cannot write " + target.getFullPathName();
        return false;
    }
    if (target != entry.file)
        (void) entry.file.deleteFile();
    return true;
}

} // namespace cydaw::v2::presets
