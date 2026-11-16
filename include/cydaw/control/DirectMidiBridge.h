#pragma once
#include "Control.h"
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <span>

namespace cydaw::control {
// Input/service threads only, never the audio callback. A private copy of
// the programmable router converts raw controls to MIDI immediately. GUI
// command targets continue through their normal application dispatcher.
class DirectMidiBridge final {
public:
    using Sink = std::function<void(std::span<const std::uint8_t>)>;
    explicit DirectMidiBridge(Sink sink) : sink_(std::move(sink)) {}
    ~DirectMidiBridge() { panic(); }
    void configure(const Router&, std::set<std::string> disabledSources = {});
    void receive(std::string_view source, const RawMessage&);
    void releaseSource(std::string_view source);
    void panic();
    [[nodiscard]] bool claims(std::string_view source, std::string_view address) const;
    [[nodiscard]] std::uint64_t sent() const noexcept { return sent_.load(); }
    [[nodiscard]] static bool isTarget(std::string_view target);
    [[nodiscard]] static std::string midiAddress(std::string_view target);
private:
    void releaseLocked(std::string_view source);
    void emit(const std::vector<std::uint8_t>&);
    mutable std::mutex lock_;
    Router router_;
    std::set<std::string> disabled_;
    // Origin includes the raw address, so two keys mapped to one note keep
    // it held until both let go. No stuck notes on remapping/disconnection.
    std::map<std::pair<std::string, std::string>, std::vector<std::uint8_t>> held_;
    std::map<std::string, unsigned> holds_;
    std::map<std::string, double> values_;
    Sink sink_;
    std::atomic<std::uint64_t> sent_ {};
};
} // namespace cydaw::control
