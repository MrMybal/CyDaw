#include <algorithm>
#include <cydaw/base/Socket.h>
#include <memory>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace cydaw::base {
namespace {
#if defined(_WIN32)
using Descriptor = SOCKET;
using Length = int;
bool initialize() {
  struct Winsock {
    bool okay = false;
    Winsock() {
      WSADATA data{};
      okay = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~Winsock() {
      if (okay)
        WSACleanup();
    }
  };
  static Winsock state;
  return state.okay;
}
void release(Descriptor value) { closesocket(value); }
void nonblocking(Descriptor value, bool enabled) {
  u_long flag = enabled ? 1 : 0;
  ioctlsocket(value, FIONBIO, &flag);
}
#else
using Descriptor = int;
using Length = socklen_t;
bool initialize() { return true; }
void release(Descriptor value) { ::close(value); }
void nonblocking(Descriptor value, bool enabled) {
  const int flags = fcntl(value, F_GETFL);
  fcntl(value, F_SETFL, enabled ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
}
#endif
int wait(Descriptor value, bool reading, int timeout) {
  fd_set reads, writes, errors;
  FD_ZERO(&reads);
  FD_ZERO(&writes);
  FD_ZERO(&errors);
  FD_SET(value, reading ? &reads : &writes);
  FD_SET(value, &errors);
  timeval delay{timeout / 1000, (timeout % 1000) * 1000};
  const auto result = ::select(int(value) + 1, &reads, &writes, &errors,
                               timeout < 0 ? nullptr : &delay);
  return result <= 0 ? result : FD_ISSET(value, &errors) ? -1 : 1;
}
} // namespace
void StreamingSocket::close() {
  const auto value = socket_.exchange(-1);
  if (value != -1) {
    ::shutdown(Descriptor(value), 2);
    release(Descriptor(value));
  }
}
bool StreamingSocket::createListener(int port, const Text &address) {
  close();
  if (!initialize() || port < 0 || port > 65535)
    return false;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo *found{};
  const auto service = Text(port).toStdString();
  if (getaddrinfo(address.isEmpty() ? nullptr : address.toRawUTF8(),
                  service.c_str(), &hints, &found) != 0)
    return false;
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(found, freeaddrinfo);
  for (auto *item = found; item; item = item->ai_next) {
    auto value =
        ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (value == Descriptor(-1))
      continue;
    int reuse = 1;
#if defined(_WIN32)
    setsockopt(value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#else
    setsockopt(value, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&reuse), sizeof(reuse));
#endif
    if (::bind(value, item->ai_addr, int(item->ai_addrlen)) == 0 &&
        ::listen(value, 16) == 0) {
      socket_ = std::intptr_t(value);
      return true;
    }
    release(value);
  }
  return false;
}
StreamingSocket *StreamingSocket::waitForNextConnection() {
  const auto current = socket_.load();
  if (current == -1)
    return nullptr;
  const auto accepted = ::accept(Descriptor(current), nullptr, nullptr);
  if (accepted == Descriptor(-1))
    return nullptr;
  return new StreamingSocket(std::intptr_t(accepted));
}
bool StreamingSocket::connect(const Text &host, int port, int timeout) {
  close();
  if (!initialize() || port <= 0 || port > 65535)
    return false;
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *found{};
  const auto service = Text(port).toStdString();
  if (getaddrinfo(host.toRawUTF8(), service.c_str(), &hints, &found) != 0)
    return false;
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> owner(found, freeaddrinfo);
  for (auto *item = found; item; item = item->ai_next) {
    const auto value =
        ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (value == Descriptor(-1))
      continue;
    nonblocking(value, true);
    const auto connected =
        ::connect(value, item->ai_addr, int(item->ai_addrlen));
    int error{};
    Length length = sizeof(error);
    if (connected == 0 ||
        (wait(value, false, std::max(0, timeout)) > 0 &&
         getsockopt(value, SOL_SOCKET, SO_ERROR,
                    reinterpret_cast<char *>(&error), &length) == 0 &&
         error == 0)) {
      nonblocking(value, false);
      int noDelay = 1;
      setsockopt(value, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
      socket_ = std::intptr_t(value);
      return true;
    }
    release(value);
  }
  return false;
}
int StreamingSocket::waitUntilReady(bool reading, int timeout) const {
  const auto value = socket_.load();
  return value == -1 ? -1 : wait(Descriptor(value), reading, timeout);
}
int StreamingSocket::read(void *destination, int maximum, bool all) {
  if (!destination || maximum <= 0)
    return 0;
  int received = 0;
  while (received < maximum) {
    const auto current = socket_.load();
    if (current == -1)
      return received ? received : -1;
    const auto count =
        ::recv(Descriptor(current), static_cast<char *>(destination) + received,
               maximum - received, 0);
    if (count <= 0) {
      close();
      return received ? received : int(count);
    }
    received += int(count);
    if (!all)
      break;
  }
  return received;
}
int StreamingSocket::write(const void *source, int size) {
  const auto current = socket_.load();
  if (current == -1 || !source || size <= 0)
    return -1;
#if defined(MSG_NOSIGNAL)
  constexpr int flags = MSG_NOSIGNAL;
#else
  constexpr int flags = 0;
#endif
  const auto count = ::send(Descriptor(current),
                            static_cast<const char *>(source), size, flags);
  if (count <= 0)
    close();
  return int(count);
}
int StreamingSocket::getBoundPort() const {
  const auto current = socket_.load();
  if (current == -1)
    return 0;
  sockaddr_storage address{};
  Length length = sizeof(address);
  if (getsockname(Descriptor(current), reinterpret_cast<sockaddr *>(&address),
                  &length) != 0)
    return 0;
  return address.ss_family == AF_INET
             ? ntohs(reinterpret_cast<const sockaddr_in *>(&address)->sin_port)
         : address.ss_family == AF_INET6
             ? ntohs(
                   reinterpret_cast<const sockaddr_in6 *>(&address)->sin6_port)
             : 0;
}
void DatagramSocket::close(){const auto value=socket_.exchange(-1);if(value!=-1)release(Descriptor(value));}
bool DatagramSocket::bind(int port,const Text& address){
 close();if(!initialize()||port<0||port>65535)return false;
 addrinfo hints{};hints.ai_socktype=SOCK_DGRAM;hints.ai_flags=AI_PASSIVE;hints.ai_family=AF_UNSPEC;
 addrinfo* found{};const auto service=Text(port).toStdString();if(getaddrinfo(address.isEmpty()?nullptr:address.toRawUTF8(),service.c_str(),&hints,&found)!=0)return false;
 std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> owner(found,freeaddrinfo);
 // Prefer a dual-stack listener where available, then fall back to IPv4.
 for(int pass=0;pass<2;++pass)for(auto* item=found;item;item=item->ai_next){if((pass==0)!=(item->ai_family==AF_INET6))continue;
  auto value=::socket(item->ai_family,item->ai_socktype,item->ai_protocol);if(value==Descriptor(-1))continue;
  int flag=1;
#if defined(_WIN32)
  setsockopt(value,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,reinterpret_cast<const char*>(&flag),sizeof(flag));
#endif
  if(item->ai_family==AF_INET6){flag=0;if(setsockopt(value,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&flag),sizeof(flag))!=0){release(value);continue;}}
  if(::bind(value,item->ai_addr,int(item->ai_addrlen))==0){socket_=std::intptr_t(value);return true;}release(value);
 }return false;
}
bool DatagramSocket::connect(const Text& host,int port){
 close();if(!initialize()||host.isEmpty()||port<=0||port>65535)return false;addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_DGRAM;addrinfo* found{};const auto service=Text(port).toStdString();if(getaddrinfo(host.toRawUTF8(),service.c_str(),&hints,&found)!=0)return false;
 std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> owner(found,freeaddrinfo);for(auto* item=found;item;item=item->ai_next){auto value=::socket(item->ai_family,item->ai_socktype,item->ai_protocol);if(value==Descriptor(-1))continue;if(::connect(value,item->ai_addr,int(item->ai_addrlen))==0){socket_=std::intptr_t(value);return true;}release(value);}return false;
}
int DatagramSocket::read(void* destination,int maximum,int timeout){const auto value=socket_.load();if(value==-1||!destination||maximum<=0)return -1;const auto ready=wait(Descriptor(value),true,timeout);if(ready<=0)return ready;return int(::recv(Descriptor(value),static_cast<char*>(destination),maximum,0));}
int DatagramSocket::write(const void* data,int size){const auto value=socket_.load();if(value==-1||!data||size<=0||size>65507)return -1;return int(::send(Descriptor(value),static_cast<const char*>(data),size,0));}
int DatagramSocket::getBoundPort()const{const auto value=socket_.load();if(value==-1)return 0;sockaddr_storage address{};Length length=sizeof(address);if(getsockname(Descriptor(value),reinterpret_cast<sockaddr*>(&address),&length)!=0)return 0;return address.ss_family==AF_INET?ntohs(reinterpret_cast<const sockaddr_in*>(&address)->sin_port):address.ss_family==AF_INET6?ntohs(reinterpret_cast<const sockaddr_in6*>(&address)->sin6_port):0;}
} // namespace cydaw::base
