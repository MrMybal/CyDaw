#pragma once
#include "Xml.h"
#include <cstring>
namespace cydaw::base {
// Bounded reader for the historical XML state envelope. New Core state is
// stored unchanged; this codec is only the old application's wire format.
inline std::unique_ptr<XmlElement> legacyXmlFromBinary(const void *data,
                                                       int size) {
  if (!data || size < 8)
    return {};
  const auto *b = static_cast<const unsigned char *>(data);
  if (std::memcmp(b, "VC2!", 4) != 0)
    return {};
  const auto n = std::uint32_t(b[4]) | (std::uint32_t(b[5]) << 8) |
                 (std::uint32_t(b[6]) << 16) | (std::uint32_t(b[7]) << 24);
  if (!n || n > 64u * 1024u * 1024u || n > std::uint32_t(size - 8))
    return {};
  return parseXML(
      Text::fromUTF8(reinterpret_cast<const char *>(b + 8), int(n)));
}
inline void copyLegacyXmlToBinary(const XmlElement &xml, MemoryBlock &result) {
  const auto text = xml.toString();
  const auto n = text.getNumBytesAsUTF8();
  result.reset();
  if (n > 64u * 1024u * 1024u)
    return;
  unsigned char header[8] = {'V', 'C', '2', '!', 0, 0, 0, 0};
  for (unsigned i = 0; i < 4; ++i)
    header[4 + i] = static_cast<unsigned char>(n >> (8 * i));
  result.append(header, 8);
  result.append(text.toRawUTF8(), n + 1);
}
inline bool decodeLegacyStateBytes(const Text &text, MemoryBlock &block) {
  const auto wire = text.toStdString();
  const auto dot = wire.find('.');
  if (dot == std::string::npos)
    return block.fromBase64Encoding(text);
  if (dot == 0 || dot > 9 ||
      !std::all_of(wire.begin(), wire.begin() + dot,
                   [](char c) { return c >= '0' && c <= '9'; }))
    return false;
  const auto count = text.substring(0, int(dot)).getLargeIntValue();
  if (count < 0 || count > 64 * 1024 * 1024)
    return false;
  const auto digits = std::string_view(
      ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+");
  const auto required = (std::size_t(count) * 8 + 5) / 6;
  if (wire.size() - dot - 1 != required)
    return false;
  MemoryBlock decoded(std::size_t(count), true);
  auto *bytes = static_cast<unsigned char *>(decoded.getData());
  for (std::size_t i = 0; i < required; ++i) {
    const auto value = digits.find(wire[dot + 1 + i]);
    if (value == std::string_view::npos)
      return false;
    for (unsigned bit = 0; bit < 6; ++bit) {
      const auto position = i * 6 + bit;
      if (position < std::size_t(count) * 8 && ((value >> bit) & 1))
        bytes[position / 8] |= static_cast<unsigned char>(1u << (position % 8));
    }
  }
  block.swapWith(decoded);
  return true;
}
} // namespace cydaw::base
