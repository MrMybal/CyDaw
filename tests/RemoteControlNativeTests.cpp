#include "RemoteControlServer.h"
#include <cydaw/base/EventLoop.h>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace cydaw;
void require(bool value, const char *text) {
  if (!value)
    throw std::runtime_error(text);
}
int main() {
  try {
    base::StreamingSocket reserve;
    require(reserve.createListener(0, "127.0.0.1"), "native loopback listener");
    const auto port = reserve.getBoundPort();
    require(port > 0, "native ephemeral port");
    reserve.close();
    int calls = 0;
    const auto uiThread = std::this_thread::get_id();
    app::RemoteControlServer server(
        port,
        [&](const base::Value &request) {
          require(std::this_thread::get_id() == uiThread,
                  "remote handler executes on UI owner");
          ++calls;
          auto result = base::Object::Ptr(new base::Object());
          result->setProperty("ok", true);
          result->setProperty("echo", request["text"]);
          return base::Value(result.get());
        },
        "native-test");
    require(server.isListening(), "actual server listens without JUCE");
    const auto request = [&](base::Text text) {
      auto work = std::async(std::launch::async, [&, text] {
        base::StreamingSocket socket;
        require(socket.connect("127.0.0.1", port, 1000),
                "native client connects");
        require(socket.write(text.toRawUTF8(), int(text.getNumBytesAsUTF8())) ==
                    int(text.getNumBytesAsUTF8()),
                "native client sends request");
        require(socket.waitUntilReady(true, 3000) > 0,
                "native client receives response");
        char bytes[4096];
        const int count = socket.read(bytes, sizeof(bytes), false);
        return count > 0 ? base::Text::fromUTF8(bytes, count) : base::Text{};
      });
      const auto end =
          std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (work.wait_for(std::chrono::milliseconds(0)) !=
                 std::future_status::ready &&
             std::chrono::steady_clock::now() < end) {
        base::EventLoop::dispatchPending();
        base::Thread::sleep(1);
      }
      return work.get();
    };
    auto answer = base::JSON::parse(
        request("{\"token\":\"native-test\",\"id\":42,\"text\":\"hello\"}\n"));
    require(bool(answer["ok"]) && int(answer["id"]) == 42 &&
                answer["echo"].toString() == "hello" && calls == 1,
            "actual server dispatch, id and payload");
    answer = base::JSON::parse(request("{\"token\":\"wrong\"}\n"));
    require(!bool(answer["ok"]) && calls == 1,
            "invalid token is rejected before dispatch");
    require(request("POST / HTTP/1.1\r\n\r\n").isEmpty() && calls == 1,
            "browser HTTP requests are rejected");
    std::cout << "Native loopback socket and actual authenticated control "
                 "dispatch passed without JUCE\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
