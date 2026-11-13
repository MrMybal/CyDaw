#pragma once
#include "Text.h"
#include <atomic>
#include <cstdint>
namespace cydaw::base {
class StreamingSocket {
  std::atomic<std::intptr_t> socket_{-1};
  explicit StreamingSocket(std::intptr_t descriptor) : socket_(descriptor) {}

public:
  StreamingSocket() = default;
  ~StreamingSocket() { close(); }
  StreamingSocket(const StreamingSocket &) = delete;
  StreamingSocket &operator=(const StreamingSocket &) = delete;
  bool createListener(int port, const Text &localAddress = {});
  StreamingSocket *waitForNextConnection();
  bool connect(const Text &host, int port, int timeoutMs = 3000);
  bool isConnected() const noexcept { return socket_.load() != -1; }
  void close();
  int waitUntilReady(bool forReading, int timeoutMs) const;
  int read(void *destination, int maximum, bool waitForAll = true);
  int write(const void *source, int size);
  int getBoundPort() const;
};
// Datagram boundaries are retained; a read never joins separate packets.
class DatagramSocket {
 std::atomic<std::intptr_t> socket_{-1};
public:
 ~DatagramSocket(){close();}
 DatagramSocket()=default;DatagramSocket(const DatagramSocket&)=delete;
 bool bind(int port,const Text& address={});bool connect(const Text& host,int port);
 void close();int read(void*,int maximum,int timeoutMs=100);int write(const void*,int size);
 int getBoundPort()const;
};
} // namespace cydaw::base
