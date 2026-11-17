#include <cydaw/control/DirectMidiBridge.h>
#include <algorithm>
#include <cmath>

namespace cydaw::control {
std::string DirectMidiBridge::midiAddress(std::string_view target) {
    constexpr std::string_view prefix = "midi/live/";
    return target.starts_with(prefix) ? std::string(target.substr(prefix.size())) : std::string();
}
bool DirectMidiBridge::isTarget(std::string_view target) {
    const auto address = midiAddress(target);
    return !address.empty() && !toMidi({address, 0.5}).empty();
}
void DirectMidiBridge::emit(const std::vector<std::uint8_t>& bytes) {
    if (!bytes.empty() && sink_) { sink_(bytes); sent_.fetch_add(1, std::memory_order_relaxed); }
}
void DirectMidiBridge::configure(const Router& router, std::set<std::string> disabled) {
    const std::scoped_lock guard(lock_);
    releaseLocked({}); router_ = router; disabled_ = std::move(disabled); values_.clear();
    std::erase_if(router_.assignments, [](const auto& a) { return !isTarget(a.target); });
}
bool DirectMidiBridge::claims(std::string_view source, std::string_view address) const {
    const std::scoped_lock guard(lock_);
    return !disabled_.contains(std::string(source)) && router_.controlFedBy(source, address).has_value();
}
void DirectMidiBridge::receive(std::string_view source, const RawMessage& message) {
    if (!std::isfinite(message.value)) return;
    const std::scoped_lock guard(lock_);
    if (disabled_.contains(std::string(source))) return;
    const auto moved = router_.receive(source, message);
    const auto orders = router_.orders(moved, [this](const std::string& target) -> std::optional<double> {
        const auto found = values_.find(target); return found == values_.end() ? std::nullopt : std::optional(found->second);
    });
    const auto origin = std::string(source) + "|" + message.address;
    for (const auto& order : orders) {
        const auto bytes = toMidi({midiAddress(order.target), order.value});
        if (bytes.empty()) continue;
        if ((bytes[0] & 0xf0) == 0x90 || (bytes[0] & 0xf0) == 0x80) {
            const auto key = std::make_pair(origin, order.target);
            const bool on = order.momentary ? message.value > 0 && bytes.size() > 2 && bytes[2] > 0 : order.value >= 0.5;
            const auto held = held_.find(key);
            if (on && held == held_.end()) {
                auto off = bytes; off[0] = (off[0] & 0x0f) | 0x80; off[2] = 0;
                held_[key] = off;
                if (holds_[order.target]++ == 0) emit(bytes);
            } else if (!on && held != held_.end()) {
                if (--holds_[order.target] == 0) emit(held->second);
                held_.erase(held);
            }
        } else if (!values_.contains(order.target) || toMidi({midiAddress(order.target), values_[order.target]}) != bytes) {
            emit(bytes);
        }
        values_[order.target] = order.value;
    }
}
void DirectMidiBridge::releaseLocked(std::string_view source) {
    for (auto it = held_.begin(); it != held_.end();) {
        if (source.empty() || it->first.first.starts_with(std::string(source) + "|")) {
            if (--holds_[it->first.second] == 0) emit(it->second);
            it = held_.erase(it);
        } else ++it;
    }
    std::erase_if(holds_, [](const auto& item) { return item.second == 0; });
}
void DirectMidiBridge::releaseSource(std::string_view source) {
    const std::scoped_lock guard(lock_);
    releaseLocked(source);
    for(auto& device:router_.devices)for(auto& control:device.controls)if(control.sourceDevice==source) {
        control.lastRaw=0;
        if(control.momentary() || control.kind==ControlKind::toggle){control.value=0;control.known=false;}
    }
}
void DirectMidiBridge::panic() { const std::scoped_lock guard(lock_); releaseLocked({}); }
} // namespace cydaw::control
