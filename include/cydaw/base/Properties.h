#pragma once
#include "Xml.h"
#include <mutex>
namespace cydaw::base {
class PropertiesFile {
public:
  struct Options {
    Text applicationName, filenameSuffix, folderName, osxLibrarySubFolder;
    bool ignoreCaseOfKeyNames = false;
  };
  explicit PropertiesFile(const Options &);
  PropertiesFile(const File &, const Options &);
  ~PropertiesFile();
  Text getValue(const Text &, const Text &fallback = {}) const;
  int getIntValue(const Text &key, int fallback = 0) const {
    return getValue(key, Text(fallback)).getIntValue();
  }
  double getDoubleValue(const Text &key, double fallback = 0) const {
    return getValue(key, Text(fallback)).getDoubleValue();
  }
  bool getBoolValue(const Text &key, bool fallback = false) const {
    return getIntValue(key, fallback ? 1 : 0) != 0;
  }
  void setValue(const Text &, const Value &);
  bool containsKey(const Text &) const;
  void removeValue(const Text &);
  bool saveIfNeeded();
  bool save();
  bool reload();
  const File &getFile() const { return file_; }

private:
  File file_;
  mutable std::recursive_mutex mutex_;
  std::vector<std::pair<Text, Text>> values_;
  bool dirty_ = false, readable_ = true, ignoreCase_ = false;
};
class ApplicationProperties {
  PropertiesFile::Options options_;
  std::unique_ptr<PropertiesFile> user_;

public:
  void setStorageParameters(const PropertiesFile::Options &options) {
    closeFiles();
    options_ = options;
  }
  PropertiesFile *getUserSettings() {
    if (!user_)
      user_ = std::make_unique<PropertiesFile>(options_);
    return user_.get();
  }
  bool saveIfNeeded() { return !user_ || user_->saveIfNeeded(); }
  void closeFiles() {
    if (user_)
      user_->saveIfNeeded();
    user_.reset();
  }
};
} // namespace cydaw::base
