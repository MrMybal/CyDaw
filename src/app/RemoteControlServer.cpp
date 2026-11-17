#include <iostream>
#include <cydaw/base/Socket.h>
#include <cydaw/base/EventLoop.h>
#include <cydaw/base/Thread.h>
#include <cydaw/base/File.h>
#include "RemoteControlServer.h"



#include <atomic>
#include <memory>
#include <utility>

namespace cydaw::app {

namespace {
cydaw::base::Text failure(const cydaw::base::Text& message)
{
    auto* object = new cydaw::base::Object();
    object->setProperty("ok", false);
    object->setProperty("error", message);
    return cydaw::base::JSON::toString(cydaw::base::Value(object), true);
}
} // namespace

RemoteControlServer::RemoteControlServer(const int port, Handler handler,
                                         cydaw::base::Text token)
    : cydaw::base::Thread("CyDAW remote control"),
      port_(port),
      handler_(std::move(handler)),
      token_(std::move(token))
{
    if (port_ <= 0)
        return;
    listening_ = listener_.createListener(port_, "127.0.0.1");
    if (listening_)
        startThread();
    std::clog << (
        listening_ ? "remote control: listening on 127.0.0.1:"
                         + cydaw::base::Text(port_)
                   : "remote control: port " + cydaw::base::Text(port_)
                         + " is not available") << "\n";
}

RemoteControlServer::~RemoteControlServer()
{
    signalThreadShouldExit();
    listener_.close();
    stopThread(3'000);
}

void RemoteControlServer::run()
{
    while (!threadShouldExit()) {
        std::unique_ptr<cydaw::base::StreamingSocket> client(
            listener_.waitForNextConnection());
        if (client == nullptr) {
            if (threadShouldExit())
                break;
            wait(50);
            continue;
        }
        serve(*client);
    }
}

void RemoteControlServer::serve(cydaw::base::StreamingSocket& client)
{
    std::string buffer;
    // A connection starts with a JSON object. One that starts otherwise (a web
    // page's request: "POST / HTTP/1.1"…) is closed before anything it carries
    // is read, so a page in a browser cannot drive CyDAW through its body.
    auto first = true;
    while (!threadShouldExit() && client.isConnected()) {
        const auto ready = client.waitUntilReady(true, 200);
        if (ready < 0)
            break;
        if (ready == 0)
            continue;
        char chunk[4'096];
        const auto count = client.read(chunk, static_cast<int>(sizeof(chunk)),
                                       false);
        if (count <= 0)
            break;
        buffer.append(chunk, static_cast<std::size_t>(count));
        for (;;) {
            const auto newline = buffer.find('\n');
            if (newline == std::string::npos)
                break;
            const auto line = cydaw::base::Text::fromUTF8(
                buffer.data(), static_cast<int>(newline));
            buffer.erase(0, newline + 1);
            if (line.trim().isEmpty())
                continue;
            if (std::exchange(first, false) && !line.trim().startsWithChar('{'))
                return;
            const auto reply = respond(line.trim()) + "\n";
            const auto bytes = reply.toRawUTF8();
            const auto length = static_cast<int>(std::char_traits<char>::length(bytes));
            if (client.write(bytes, length) != length)
                return;
        }
    }
}

cydaw::base::Text RemoteControlServer::respond(const cydaw::base::Text& line) const
{
    const auto request = cydaw::base::JSON::parse(line);
    if (request.getDynamicObject() == nullptr)
        return failure("the request is not a JSON object");
    if (!handler_)
        return failure("no handler");
    if (token_.isNotEmpty()
        && request.getProperty("token", cydaw::base::Value()).toString() != token_)
        return failure("the request carries no valid token");
    // Answered on the message thread; the socket thread waits, and gives
    // up rather than hang on a modal dialog.
    struct Pending {
        cydaw::base::Value response;
        cydaw::base::WaitableEvent done;
        // Given up on (too late, or CyDAW closing): not run when its turn
        // comes, so a client trying again does not have it done twice.
        std::atomic<bool> abandoned { false };
    };
    auto pending = std::make_shared<Pending>();
    cydaw::base::EventLoop::post([pending, request, handler = handler_] {
        if (pending->abandoned.load())
            return;
        pending->response = handler(request);
        pending->done.signal();
    });
    // Waited for in slices: a CyDAW closing does not wait for it.
    auto answered = false;
    for (auto waited = 0; waited < 30'000 && !answered && !threadShouldExit(); waited += 100)
        answered = pending->done.wait(100);
    if (!answered) {
        pending->abandoned.store(true);
        return failure(threadShouldExit() ? "the application is closing" : "the application did not answer within 30 s");
    }
    if (auto* object = pending->response.getDynamicObject();
        object != nullptr && request.hasProperty("id"))
        object->setProperty("id", request["id"]);
    const auto encoded = cydaw::base::JSON::toString(pending->response, true);
    if (encoded.isNotEmpty()) return encoded;
    cydaw::base::MemoryOutputStream diagnostic;
    const auto result = cydaw::base::JSON::writeToStream(diagnostic, pending->response, true);
    return failure("The response could not be encoded: " + result.getErrorMessage());
}

} // namespace cydaw::app
