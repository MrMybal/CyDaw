#pragma once
#include "File.h"
namespace cydaw::base {
// Application XML model. Ownership and traversal are independent of the parser.
class XmlElement {
  Text tag_, text_;
  std::vector<std::pair<Text, Text>> attributes_;
  std::vector<std::unique_ptr<XmlElement>> children_;

public:
  explicit XmlElement(Text tag) : tag_(std::move(tag)) {}
  XmlElement(const XmlElement &);
  const Text &getTagName() const { return tag_; }
  bool hasTagName(const Text &tag) const { return tag_ == tag; }
  bool hasAttribute(const Text &) const;
  Text getStringAttribute(const Text &, const Text &fallback = {}) const;
  int getIntAttribute(const Text &key, int fallback = 0) const {
    return hasAttribute(key) ? getStringAttribute(key).getIntValue() : fallback;
  }
  double getDoubleAttribute(const Text &key, double fallback = 0) const {
    return hasAttribute(key) ? getStringAttribute(key).getDoubleValue()
                             : fallback;
  }
  bool getBoolAttribute(const Text &key, bool fallback = false) const {
    auto v = getStringAttribute(key);
    return v.isEmpty() ? fallback
                       : v.equalsIgnoreCase("true") ||
                             v.equalsIgnoreCase("yes") || v.getIntValue() != 0;
  }
  void setAttribute(const Text &, Text);
  template <class T> void setAttribute(const Text &key, T value) {
    setAttribute(key, Text(value));
  }
  int getNumAttributes() const { return int(attributes_.size()); }
  Text getAttributeName(int index) const { return attributes_.at(index).first; }
  Text getAttributeValue(int index) const {
    return attributes_.at(index).second;
  }
  XmlElement *getFirstChildElement() const { return getChildElement(0); }
  XmlElement *getChildByName(const Text &) const;
  XmlElement *getChildElement(int) const;
  int getNumChildElements() const { return int(children_.size()); }
  std::vector<XmlElement *> getChildIterator() const;
  std::vector<XmlElement *> getChildWithTagNameIterator(const Text &) const;
  void addChildElement(XmlElement *node) { children_.emplace_back(node); }
  XmlElement *createNewChildElement(const Text &tag) {
    auto *child = new XmlElement(tag);
    addChildElement(child);
    return child;
  }
  void addTextElement(const Text &t) { text_ += t; }
  Text getAllSubText() const;
  const Text &ownText() const { return text_; }
  Text toString() const;
};
struct XmlDocument {
  static std::unique_ptr<XmlElement> parse(const Text &);
  static std::unique_ptr<XmlElement> parse(const File &file) {
    return file.getSize() <= 128 * 1024 * 1024 ? parse(file.loadFileAsString())
                                               : nullptr;
  }
};
inline std::unique_ptr<XmlElement> parseXML(const Text &text) {
  return XmlDocument::parse(text);
}
} // namespace cydaw::base
