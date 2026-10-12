#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <string>

namespace cydaw::app {

// The local control port an agent talks to (see tools/mcp): one JSON
// object per line in, one per line out, every request answered on the
// message thread. Listens on the loopback address only.
class RemoteControlServer final : private juce::Thread {
public:
    using Handler = std::function<juce::var(const juce::var&)>;

    // With a token, a request must carry {"token": …} to be answered.
    RemoteControlServer(int port, Handler handler, juce::String token = {});
    ~RemoteControlServer() override;

    [[nodiscard]] bool isListening() const noexcept { return listening_; }
    [[nodiscard]] int port() const noexcept { return port_; }

private:
    void run() override;
    void serve(juce::StreamingSocket& client);
    // The reply to one line: the handler's answer, or why there is none.
    [[nodiscard]] juce::String respond(const juce::String& line) const;

    int port_ {};
    Handler handler_;
    juce::String token_;
    juce::StreamingSocket listener_;
    bool listening_ {};
};

} // namespace cydaw::app
