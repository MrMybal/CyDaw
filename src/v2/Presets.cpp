#include <cydaw/base/File.h>
#include "Presets.h"

#include "Locations.h"
#include "SamplerMapFile.h"

#include <algorithm>

namespace cydaw::v2::presets {

namespace {

constexpr auto format = "cydaw-preset";

cydaw::base::Text fileNameOf(const cydaw::base::Text& name)
{
    return cydaw::base::File::createLegalFileName(name.trim()).substring(0, 120);
}

void addFrom(const cydaw::base::File& folder, const bool factory, std::vector<Entry>& out)
{
    if (!folder.isDirectory())
        return;
    auto files = folder.findChildFiles(cydaw::base::File::findFiles, false, cydaw::base::Text("*") + fileExtension);
    files.sort();
    for (const auto& file : files)
        out.push_back({ file.getFileNameWithoutExtension(), file, factory });
}

} // namespace

cydaw::base::Text folderName(const cydaw::base::Text& target)
{
    const cydaw::base::Text name = cydaw::base::File::createLegalFileName(target.trim()).substring(0, 96);
    return name.isNotEmpty() ? name : cydaw::base::Text("other");
}

cydaw::base::File userFolder(const cydaw::base::Text& kind, const cydaw::base::Text& target)
{
    return locations::folder(Location::presets).getChildFile(kind).getChildFile(folderName(target));
}

cydaw::base::File factoryFolder(const cydaw::base::Text& kind, const cydaw::base::Text& target)
{
    return app::factoryContentFolder().getChildFile("Presets").getChildFile(kind).getChildFile(folderName(target));
}

std::vector<Entry> list(const cydaw::base::Text& kind, const cydaw::base::Text& target)
{
    std::vector<Entry> entries;
    addFrom(factoryFolder(kind, target), true, entries);
    addFrom(userFolder(kind, target), false, entries);
    return entries;
}

const Entry* find(const std::vector<Entry>& entries, const cydaw::base::Text& name)
{
    // The person's before CyDAW's of the same name.
    const Entry* found = nullptr;
    for (const auto& entry : entries)
        if (entry.name == name && (found == nullptr || found->factory))
            found = &entry;
    return found;
}

bool save(const cydaw::base::Text& kind, const cydaw::base::Text& target, const cydaw::base::Text& name, const cydaw::base::Value& state,
          cydaw::base::Text& error)
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
    auto* document = new cydaw::base::Object();
    document->setProperty("format", format);
    document->setProperty("version", 1);
    document->setProperty("kind", kind);
    document->setProperty("target", target);
    document->setProperty("name", name.trim());
    document->setProperty("created", cydaw::base::Time::getCurrentTime().toISO8601(true));
    document->setProperty("state", state);
    const auto file = folder.getChildFile(fileName + fileExtension);
    if (!file.replaceWithText(cydaw::base::JSON::toString(cydaw::base::Value(document), true))) {
        error = "cannot write " + file.getFullPathName();
        return false;
    }
    return true;
}

cydaw::base::Value read(const Entry& entry, const cydaw::base::Text& kind, cydaw::base::Text& error)
{
    if (entry.file.getSize() > 256 * 1024 * 1024) {
        error = "the preset is too large";
        return {};
    }
    const auto document = cydaw::base::JSON::parse(entry.file.loadFileAsString());
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

bool remove(const Entry& entry, cydaw::base::Text& error)
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

bool rename(const Entry& entry, const cydaw::base::Text& name, cydaw::base::Text& error)
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
    auto document = cydaw::base::JSON::parse(entry.file.loadFileAsString());
    if (auto* object = document.getDynamicObject())
        object->setProperty("name", name.trim());
    if (!target.replaceWithText(cydaw::base::JSON::toString(document, true))) {
        error = "cannot write " + target.getFullPathName();
        return false;
    }
    if (target != entry.file)
        (void) entry.file.deleteFile();
    return true;
}

} // namespace cydaw::v2::presets
