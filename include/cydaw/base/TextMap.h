#pragma once
#include "Text.h"
namespace cydaw::base {
class TextMap {
  std::vector<std::pair<Text, Text>> values_;
  bool ignoreCase_ = true;

public:
  TextMap(bool ignoreCase = true) : ignoreCase_(ignoreCase) {}
  void set(const Text &key, const Text &value) {
    for (auto &p : values_)
      if (ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key) {
        p.second = value;
        return;
      }
    values_.emplace_back(key, value);
  }
  Text getValue(const Text &key, const Text &fallback = {}) const {
    for (auto &p : values_)
      if (ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key)
        return p.second;
    return fallback;
  }
  Text operator[](const Text &key) const { return getValue(key); }
  bool containsKey(const Text &key) const {
    for (auto &p : values_)
      if (ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key)
        return true;
    return false;
  }
  TextList getAllKeys() const {
    TextList out;
    for (auto &p : values_)
      out.add(p.first);
    return out;
  }
  TextList getAllValues() const {
    TextList out;
    for (auto &p : values_)
      out.add(p.second);
    return out;
  }
  int size() const { return int(values_.size()); }
  bool isEmpty() const { return values_.empty(); }
  void clear() { values_.clear(); }
  void addArray(const TextMap &other) {
    for (auto &p : other.values_)
      set(p.first, p.second);
  }
  void remove(const Text &key) {
    std::erase_if(values_, [&](auto &p) {
      return ignoreCase_ ? p.first.equalsIgnoreCase(key) : p.first == key;
    });
  }
  void setIgnoresCase(bool value) { ignoreCase_ = value; }
  bool ignoresCase() const { return ignoreCase_; }
  Text getDescription() const {
    Text result;
    for (auto &p : values_)
      result << p.first << " = " << p.second << "\n";
    return result;
  }
  auto begin() const { return values_.begin(); }
  auto end() const { return values_.end(); }
};
} // namespace cydaw::base
