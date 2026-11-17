#pragma once
#include <cydaw/base/Socket.h>
#include <cydaw/base/Thread.h>
#include <functional>
#include <optional>
#include <span>
namespace cydaw::control::osc {
struct Message {
  std::string address;
  std::vector<std::optional<double>> arguments;
};
// Bounded OSC 1.0 decoding, including nested bundles. Bundle timestamps retain
// the controller tool's immediate-dispatch behavior. Invalid packets are
// atomic.
bool decode(std::span<const std::uint8_t>, std::vector<Message> &);
std::vector<std::uint8_t> encodeFloat(std::string_view, float);
class Receiver final : private base::Thread {
  base::DatagramSocket socket_;
  std::function<void(const Message &)> callback_;
  void run() override;

public:
  explicit Receiver(std::function<void(const Message &)> callback)
      : Thread("CyDaw OSC input"), callback_(std::move(callback)) {}
  ~Receiver() override { disconnect(); }
  bool connect(int port);
  void disconnect();
  int port() const { return socket_.getBoundPort(); }
};
class Sender {
  base::DatagramSocket socket_;

public:
  bool connect(const base::Text &host, int port) {
    return socket_.connect(host, port);
  }
  void disconnect() { socket_.close(); }
  bool send(std::string_view address, float value) {
    auto packet = encodeFloat(address, value);
    return !packet.empty() &&
           socket_.write(packet.data(), int(packet.size())) ==
               int(packet.size());
  }
};
} // namespace cydaw::control::osc
