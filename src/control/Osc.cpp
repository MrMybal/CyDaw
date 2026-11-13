#include <bit>
#include <cmath>
#include <cstring>
#include <cydaw/control/Osc.h>
namespace cydaw::control::osc {
namespace {
struct Reader {
  std::span<const std::uint8_t> bytes;
  std::size_t at{};
  bool skip(std::size_t n) {
    if (n > bytes.size() - at)
      return false;
    at += n;
    return true;
  }
  bool word(std::uint32_t &value) {
    if (bytes.size() - at < 4)
      return false;
    value = (std::uint32_t(bytes[at]) << 24) |
            (std::uint32_t(bytes[at + 1]) << 16) |
            (std::uint32_t(bytes[at + 2]) << 8) | bytes[at + 3];
    at += 4;
    return true;
  }
  bool string(std::string &text) {
    const auto begin = at;
    while (at < bytes.size() && bytes[at])
      ++at;
    if (at == bytes.size())
      return false;
    text.assign(reinterpret_cast<const char *>(bytes.data() + begin),
                at - begin);
    const auto padded = (at + 4) & ~std::size_t(3);
    if (padded > bytes.size())
      return false;
    for (; at < padded; ++at)
      if (bytes[at] != 0)
        return false;
    return true;
  }
};
bool packet(std::span<const std::uint8_t> bytes, std::vector<Message> &messages,
            unsigned depth) {
  if (depth > 16 || bytes.empty() || bytes.size() > 65507 || bytes.size() % 4 ||
      messages.size() >= 1024)
    return false;
  if (bytes.size() >= 8 && std::memcmp(bytes.data(), "#bundle\0", 8) == 0) {
    Reader reader{bytes, 8};
    if (!reader.skip(8))
      return false;
    while (reader.at < bytes.size()) {
      std::uint32_t n{};
      if (!reader.word(n) || !n || n > bytes.size() - reader.at)
        return false;
      if (!packet(bytes.subspan(reader.at, n), messages, depth + 1) ||
          !reader.skip(n))
        return false;
    }
    return true;
  }
  Reader reader{bytes};
  Message message;
  std::string types;
  if (!reader.string(message.address) || message.address.empty() ||
      message.address.front() != '/' || !reader.string(types) ||
      types.empty() || types.front() != ',')
    return false;
  for (std::size_t i = 1; i < types.size(); ++i) {
    std::optional<double> value;
    std::uint32_t word{};
    switch (types[i]) {
    case 'i':
      if (!reader.word(word))
        return false;
      value = std::bit_cast<std::int32_t>(word);
      break;
    case 'f':
      if (!reader.word(word))
        return false;
      {
        const auto f = std::bit_cast<float>(word);
        if (std::isfinite(f))
          value = f;
      }
      break;
    case 's': {
      std::string ignored;
      if (!reader.string(ignored))
        return false;
      break;
    }
    case 'b':
      if (!reader.word(word) || word > bytes.size() - reader.at ||
          !reader.skip((std::size_t(word) + 3) & ~std::size_t(3)))
        return false;
      break;
    case 'h':
    case 't':
    case 'd':
      if (!reader.skip(8))
        return false;
      break;
    case 'c':
    case 'r':
    case 'm':
      if (!reader.skip(4))
        return false;
      break;
    case 'T':
    case 'F':
    case 'N':
    case 'I':
      break;
    default:
      return false;
    }
    message.arguments.push_back(value);
  }
  if (reader.at != bytes.size())
    return false;
  messages.push_back(std::move(message));
  return true;
}
} // namespace
bool decode(std::span<const std::uint8_t> bytes,
            std::vector<Message> &messages) {
  std::vector<Message> decoded;
  if (!packet(bytes, decoded, 0))
    return false;
  messages = std::move(decoded);
  return true;
}
std::vector<std::uint8_t> encodeFloat(std::string_view address, float value) {
  if (address.empty() || address.front() != '/' || address.size() > 65000 ||
      address.find('\0') != std::string_view::npos || !std::isfinite(value))
    return {};
  std::vector<std::uint8_t> bytes(address.begin(), address.end());
  bytes.push_back(0);
  while (bytes.size() % 4)
    bytes.push_back(0);
  bytes.insert(bytes.end(), {',', 'f', 0, 0});
  const auto word = std::bit_cast<std::uint32_t>(value);
  for (int shift = 24; shift >= 0; shift -= 8)
    bytes.push_back(std::uint8_t(word >> shift));
  return bytes;
}
bool Receiver::connect(int port) {
  disconnect();
  if (!socket_.bind(port))
    return false;
  if (!startThread()) {
    socket_.close();
    return false;
  }
  return true;
}
void Receiver::disconnect() {
  signalThreadShouldExit();
  notify();
  stopThread(-1);
  socket_.close();
}
void Receiver::run() {
  std::array<std::uint8_t, 65536> bytes;
  while (!threadShouldExit()) {
    const int count = socket_.read(bytes.data(), int(bytes.size()), 50);
    if (count <= 0)
      continue;
    std::vector<Message> messages;
    if (decode({bytes.data(), std::size_t(count)}, messages))
      for (const auto &message : messages) {
        if (threadShouldExit())
          break;
        callback_(message);
      }
  }
}
} // namespace cydaw::control::osc
