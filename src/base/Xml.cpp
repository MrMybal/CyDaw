#include <cydaw/base/Xml.h>
#include <pugixml/pugixml.hpp>
namespace cydaw::base {
XmlElement::XmlElement(const XmlElement &other)
    : tag_(other.tag_), text_(other.text_), attributes_(other.attributes_) {
  for (auto &c : other.children_)
    children_.push_back(std::make_unique<XmlElement>(*c));
}
bool XmlElement::hasAttribute(const Text &key) const {
  for (auto &p : attributes_)
    if (p.first == key)
      return true;
  return false;
}
Text XmlElement::getStringAttribute(const Text &key,
                                    const Text &fallback) const {
  for (auto &p : attributes_)
    if (p.first == key)
      return p.second;
  return fallback;
}
void XmlElement::setAttribute(const Text &key, Text value) {
  for (auto &p : attributes_)
    if (p.first == key) {
      p.second = std::move(value);
      return;
    }
  attributes_.emplace_back(key, std::move(value));
}
XmlElement *XmlElement::getChildByName(const Text &name) const {
  for (auto &c : children_)
    if (c->hasTagName(name))
      return c.get();
  return nullptr;
}
XmlElement *XmlElement::getChildElement(int index) const {
  return index >= 0 && index < int(children_.size()) ? children_[index].get()
                                                     : nullptr;
}
std::vector<XmlElement *> XmlElement::getChildIterator() const {
  std::vector<XmlElement *> out;
  for (auto &c : children_)
    out.push_back(c.get());
  return out;
}
std::vector<XmlElement *>
XmlElement::getChildWithTagNameIterator(const Text &tag) const {
  std::vector<XmlElement *> out;
  for (auto &c : children_)
    if (c->hasTagName(tag))
      out.push_back(c.get());
  return out;
}
Text XmlElement::getAllSubText() const {
  Text out = text_;
  for (auto &c : children_)
    out += c->getAllSubText();
  return out;
}
namespace {
std::unique_ptr<XmlElement> readNode(pugi::xml_node node, unsigned depth) {
  if (depth > 256)
    return nullptr;
  auto out = std::make_unique<XmlElement>(node.name());
  for (auto a : node.attributes())
    out->setAttribute(a.name(), Text(a.value()));
  for (auto c : node.children()) {
    if (c.type() == pugi::node_element) {
      auto child = readNode(c, depth + 1);
      if (!child)
        return nullptr;
      out->addChildElement(child.release());
    } else if (c.type() == pugi::node_pcdata || c.type() == pugi::node_cdata)
      out->addTextElement(c.value());
  }
  return out;
}
void writeNode(const XmlElement &from, pugi::xml_node node) {
  for (int i = 0; i < from.getNumAttributes(); ++i)
    node.append_attribute(from.getAttributeName(i).toRawUTF8())
        .set_value(from.getAttributeValue(i).toRawUTF8());
  if (from.ownText().isNotEmpty())
    node.append_child(pugi::node_pcdata).set_value(from.ownText().toRawUTF8());
  for (auto c : from.getChildIterator())
    writeNode(*c, node.append_child(c->getTagName().toRawUTF8()));
}
struct Writer : pugi::xml_writer {
  std::string text;
  void write(const void *data, size_t size) override {
    text.append(static_cast<const char *>(data), size);
  }
};
} // namespace
std::unique_ptr<XmlElement> XmlDocument::parse(const Text &text) {
  if (text.getNumBytesAsUTF8() > 128 * 1024 * 1024)
    return nullptr;
  pugi::xml_document doc;
  if (!doc.load_buffer(text.toRawUTF8(), text.getNumBytesAsUTF8(),
                       pugi::parse_default, pugi::encoding_utf8))
    return nullptr;
  auto root = doc.document_element();
  return root ? readNode(root, 0) : nullptr;
}
Text XmlElement::toString() const {
  pugi::xml_document doc;
  writeNode(*this, doc.append_child(tag_.toRawUTF8()));
  Writer writer;
  doc.save(writer, "  ", pugi::format_default, pugi::encoding_utf8);
  return writer.text;
}
} // namespace cydaw::base
