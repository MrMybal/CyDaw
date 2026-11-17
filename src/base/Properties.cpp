#include <cydaw/base/Properties.h>
namespace cydaw::base {
namespace {
File settingsPath(const PropertiesFile::Options &o) {
  auto root = File::getSpecialLocation(File::userApplicationDataDirectory);
#if defined(__APPLE__)
  root = File::getSpecialLocation(File::userHomeDirectory)
             .getChildFile("Library")
             .getChildFile(o.osxLibrarySubFolder);
#endif
  return root
      .getChildFile(o.folderName.isEmpty() ? o.applicationName : o.folderName)
      .getChildFile(o.applicationName + o.filenameSuffix);
}
} // namespace
PropertiesFile::PropertiesFile(const Options &options)
    : PropertiesFile(settingsPath(options), options) {}
PropertiesFile::PropertiesFile(const File &file, const Options &options)
    : file_(file), ignoreCase_(options.ignoreCaseOfKeyNames) {
  reload();
}
PropertiesFile::~PropertiesFile() { saveIfNeeded(); }
bool PropertiesFile::reload() {
  std::lock_guard lock(mutex_);
  if (!file_.existsAsFile()) {
    values_.clear();
    dirty_ = false;
    readable_ = true;
    return true;
  }
  auto doc = XmlDocument::parse(file_);
  if (!doc || !doc->hasTagName("PROPERTIES")) {
    readable_ = false;
    return false;
  }
  std::vector<std::pair<Text, Text>> next;
  for (auto *item : doc->getChildWithTagNameIterator("VALUE")) {
    auto key = item->getStringAttribute("name");
    auto value = item->getStringAttribute("val");
    if (auto *child = item->getChildElement(0))
      value = child->toString();
    next.emplace_back(key, value);
  }
  values_ = std::move(next);
  dirty_ = false;
  readable_ = true;
  return true;
}
Text PropertiesFile::getValue(const Text &key, const Text &fallback) const {
  std::lock_guard lock(mutex_);
  for (auto &p : values_)
    if ((ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key))
      return p.second;
  return fallback;
}
bool PropertiesFile::containsKey(const Text &key) const {
  std::lock_guard lock(mutex_);
  for (auto &p : values_)
    if ((ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key))
      return true;
  return false;
}
void PropertiesFile::setValue(const Text &key, const Value &value) {
  std::lock_guard lock(mutex_);
  auto text = value.isBool() ? Text(static_cast<bool>(value) ? 1 : 0)
                             : value.toString();
  for (auto &p : values_)
    if ((ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key)) {
      if (p.second != text) {
        p.second = std::move(text);
        dirty_ = true;
      }
      return;
    }
  values_.emplace_back(key, std::move(text));
  dirty_ = true;
}
void PropertiesFile::removeValue(const Text &key) {
  std::lock_guard lock(mutex_);
  auto size = values_.size();
  std::erase_if(values_, [&](auto &p) {
    return (ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key);
  });
  dirty_ |= size != values_.size();
}
bool PropertiesFile::saveIfNeeded() {
  std::lock_guard lock(mutex_);
  return !dirty_ || save();
}
bool PropertiesFile::save() {
  std::lock_guard lock(mutex_);
  if (!readable_)
    return false;
  XmlElement doc("PROPERTIES");
  for (auto &p : values_) {
    auto *item = doc.createNewChildElement("VALUE");
    item->setAttribute("name", p.first);
    item->setAttribute("val", p.second);
  }
  if (file_.getParentDirectory().createDirectory().failed())
    return false;
  TemporaryFile tmp(file_);
  if (!tmp.getFile().replaceWithText(doc.toString()) ||
      !tmp.overwriteTargetFileWithTemporary())
    return false;
  dirty_ = false;
  return true;
}
} // namespace cydaw::base
