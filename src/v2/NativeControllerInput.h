#pragma once
#include <functional>
#include <memory>
#include <string>
namespace cydaw::v2 {
// Own OS message thread. No ImGui, frame timer, or audio callback here.
class NativeControllerInput final {
public:
    struct Event { std::string source, address; double value {}; int scan {}; bool keyboard {}; bool down {}; };
    using Callback = std::function<void(const Event&)>;
    explicit NativeControllerInput(Callback);
    ~NativeControllerInput();
    [[nodiscard]] bool available() const noexcept;
    // Opt-in diagnostic: synthetic event on this service thread, never OS input.
    bool scheduleProbe(int delayMs, int scan, int virtualKey);
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};
}
