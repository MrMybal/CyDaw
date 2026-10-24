#include "RemoteControlServer.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <utility>

namespace cydaw::app {

namespace {
juce::String failure(const juce::String& message)
{
    auto* object = new juce::DynamicObject();
    object->setProperty("ok", false);
    object->setProperty("error", message);
    return juce::JSON::toString(juce::var(object), true);
}
} // namespace

RemoteControlServer::RemoteControlServer(const int port, Handler handler,
                                         juce::String token)
    : juce::Thread("CyDAW remote control"),
      port_(port),
      handler_(std::move(handler)),
      token_(std::move(token))
{
    if (port_ <= 0)
        return;
    listening_ = listener_.createListener(port_, "127.0.0.1");
    if (listening_)
        startThread();
    juce::Logger::writeToLog(
        listening_ ? "remote control: listening on 127.0.0.1:"
                         + juce::String(port_)
                   : "remote control: port " + juce::String(port_)
                         + " is not available");
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
        std::unique_ptr<juce::StreamingSocket> client(
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

void RemoteControlServer::serve(juce::StreamingSocket& client)
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
            const auto line = juce::String::fromUTF8(
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

juce::String RemoteControlServer::respond(const juce::String& line) const
{
    const auto request = juce::JSON::parse(line);
    if (request.getDynamicObject() == nullptr)
        return failure("the request is not a JSON object");
    if (!handler_)
        return failure("no handler");
    if (token_.isNotEmpty()
        && request.getProperty("token", juce::var()).toString() != token_)
        return failure("the request carries no valid token");
    // Answered on the message thread; the socket thread waits, and gives
    // up rather than hang on a modal dialog.
    struct Pending {
        juce::var response;
        juce::WaitableEvent done;
        // Given up on (too late, or CyDAW closing): not run when its turn
        // comes, so a client trying again does not have it done twice.
        std::atomic<bool> abandoned { false };
    };
    auto pending = std::make_shared<Pending>();
    juce::MessageManager::callAsync([pending, request, handler = handler_] {
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
    return juce::JSON::toString(pending->response, true);
}

} // namespace cydaw::app
