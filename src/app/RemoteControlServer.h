#include <cydaw/base/Socket.h>
#include <cydaw/base/Thread.h>
#include <cydaw/base/File.h>
#pragma once



#include <functional>
#include <string>

namespace cydaw::app {

// The local control port an agent talks to (see tools/mcp): one JSON
// object per line in, one per line out, every request answered on the
// message thread. Listens on the loopback address only.
class RemoteControlServer final : private cydaw::base::Thread {
public:
    using Handler = std::function<cydaw::base::Value(const cydaw::base::Value&)>;

    // With a token, a request must carry {"token": …} to be answered.
    RemoteControlServer(int port, Handler handler, cydaw::base::Text token = {});
    ~RemoteControlServer() override;

    [[nodiscard]] bool isListening() const noexcept { return listening_; }
    [[nodiscard]] int port() const noexcept { return port_; }

private:
    void run() override;
    void serve(cydaw::base::StreamingSocket& client);
    // The reply to one line: the handler's answer, or why there is none.
    [[nodiscard]] cydaw::base::Text respond(const cydaw::base::Text& line) const;

    int port_ {};
    Handler handler_;
    cydaw::base::Text token_;
    cydaw::base::StreamingSocket listener_;
    bool listening_ {};
};

} // namespace cydaw::app
