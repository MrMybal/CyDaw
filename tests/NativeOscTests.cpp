#include <cydaw/control/Osc.h>
#include <iostream>
#include <stdexcept>
using namespace cydaw;
void require(bool okay, const char *reason) {
  if (!okay)
    throw std::runtime_error(reason);
}
struct Clock final : base::HighResolutionTimer {
  std::atomic<int> calls{};
  base::WaitableEvent ready;
  void hiResTimerCallback() override {
    if (++calls >= 4)
      ready.signal();
  }
  ~Clock() override { stopTimer(); }
};
int main() {
  try {
    auto bytes = control::osc::encodeFloat("/gain", .25f);
    std::vector<control::osc::Message> decoded;
    require(control::osc::decode(bytes, decoded) && decoded.size() == 1 &&
                decoded[0].address == "/gain" && decoded[0].arguments[0] == .25,
            "OSC address and float wire roundtrip");
    std::vector<std::uint8_t> bundle{'#', 'b', 'u', 'n', 'd', 'l', 'e', 0,
                                     0,   0,   0,   0,   0,   0,   0,   1};
    auto append = [&](const auto &data) {
      const auto n = std::uint32_t(data.size());
      for (int shift = 24; shift >= 0; shift -= 8)
        bundle.push_back(std::uint8_t(n >> shift));
      bundle.insert(bundle.end(), data.begin(), data.end());
    };
    append(bytes);
    append(bytes);
    require(control::osc::decode(bundle, decoded) && decoded.size() == 2,
            "OSC bundle contains both complete messages");
    bundle.back() = 0xff;
    bundle.pop_back();
    require(!control::osc::decode(bundle, decoded) && decoded.size() == 2,
            "malformed bundle does not partially publish messages");
    std::atomic<unsigned> calls{};
    base::WaitableEvent arrived;
    control::osc::Receiver receiver([&](const auto &message) {
      if (message.address == "/gain" && message.arguments[0] == .25) {
        ++calls;
        arrived.signal();
      }
    });
    require(receiver.connect(0), "native UDP OSC listener binds");
    control::osc::Sender sender;
    require(sender.connect("127.0.0.1", receiver.port()),
            "native UDP OSC sender connects");
    require(sender.send("/gain", .25f) && arrived.wait(2000),
            "real UDP packet arrives on receiver worker");
    receiver.disconnect();
    const auto before = calls.load();
    (void)sender.send("/gain", .25f);
    base::Thread::sleep(60);
    require(calls == before,
            "disconnect joins callbacks before releasing owners");
    Clock clock;
    clock.startTimer(2);
    require(clock.ready.wait(1000),
            "native high-resolution clock runs without UI dispatch");
    clock.stopTimer();
    const auto stopped = clock.calls.load();
    base::Thread::sleep(10);
    require(clock.calls == stopped,
            "clock stops before callback owner is destroyed");
    std::cout << "Native OSC codec, UDP callback lifecycle and control clock "
                 "passed without JUCE\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
