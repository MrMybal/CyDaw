#include <cydaw/control/Control.h>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace cydaw::control {
namespace {

constexpr double half = 0.5;

// "cc/1/74" into its parts.
std::vector<std::string_view> split(std::string_view text)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto slash = text.find('/', start);
        const auto end = slash == std::string_view::npos ? text.size() : slash;
        parts.push_back(text.substr(start, end - start));
        if (slash == std::string_view::npos)
            break;
        start = slash + 1;
    }
    return parts;
}

std::optional<int> number(const std::string_view text, const int low, const int high)
{
    int value {};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc {} || end != text.data() + text.size() || value < low || value > high)
        return std::nullopt;
    return value;
}

double clamp01(const double value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

// An encoder's tick count from its 7-bit value.
int ticks(const int raw, const int encoding) noexcept
{
    switch (encoding) {
    case 1: return raw - 64;                                   // offset from 64
    case 2: return (raw & 64) != 0 ? -(raw & 63) : raw & 63;   // sign bit
    default: return raw < 64 ? raw : raw - 128;                // two's complement
    }
}

double curveExponent(const double amount) noexcept
{
    return std::pow(4.0, -std::clamp(amount, -1.0, 1.0));
}

} // namespace

// ---- Raw messages ---------------------------------------------------------------------------

int midi7(const double value) noexcept
{
    return static_cast<int>(std::lround(clamp01(value) * 127.0));
}

bool isNoteAddress(const std::string_view address) noexcept
{
    return address.starts_with("note/");
}

bool isPressAddress(const std::string_view address) noexcept
{
    return isNoteAddress(address) || address.starts_with("key/") || address.starts_with("button/");
}

std::optional<RawMessage> fromMidi(const std::uint8_t* bytes, const std::size_t size)
{
    if (bytes == nullptr || size == 0 || bytes[0] < 0x80 || bytes[0] >= 0xf0)
        return std::nullopt;
    const auto type = bytes[0] & 0xf0;
    const auto channel = std::to_string((bytes[0] & 0x0f) + 1);
    const auto data1 = size > 1 ? bytes[1] & 0x7f : 0;
    const auto data2 = size > 2 ? bytes[2] & 0x7f : 0;
    switch (type) {
    case 0x80: return RawMessage { "note/" + channel + "/" + std::to_string(data1), 0.0 };
    case 0x90: return RawMessage { "note/" + channel + "/" + std::to_string(data1), data2 / 127.0 };
    case 0xa0: return RawMessage { "pat/" + channel + "/" + std::to_string(data1), data2 / 127.0 };
    case 0xb0: return RawMessage { "cc/" + channel + "/" + std::to_string(data1), data2 / 127.0 };
    case 0xc0: return RawMessage { "pc/" + channel + "/" + std::to_string(data1), 1.0 };
    case 0xd0: return RawMessage { "at/" + channel, data1 / 127.0 };
    case 0xe0: return RawMessage { "pb/" + channel, static_cast<double>(data1 | (data2 << 7)) / 16'383.0 };
    default: return std::nullopt;
    }
}

std::vector<std::uint8_t> toMidi(const RawMessage& message)
{
    const auto parts = split(message.address);
    if (parts.size() < 2)
        return {};
    const auto channel = number(parts[1], 1, 16);
    if (!channel)
        return {};
    const auto status = [&channel](const int type) { return static_cast<std::uint8_t>(type | (*channel - 1)); };
    const auto data = parts.size() > 2 ? number(parts[2], 0, 127) : std::nullopt;
    const auto value = static_cast<std::uint8_t>(midi7(message.value));
    if (parts[0] == "cc" && data)
        return { status(0xb0), static_cast<std::uint8_t>(*data), value };
    if (parts[0] == "note" && data)
        return { status(0x90), static_cast<std::uint8_t>(*data), value };
    if (parts[0] == "pat" && data)
        return { status(0xa0), static_cast<std::uint8_t>(*data), value };
    if (parts[0] == "pc" && data)
        return { status(0xc0), static_cast<std::uint8_t>(*data) };
    if (parts[0] == "at")
        return { status(0xd0), value };
    if (parts[0] == "pb") {
        const auto bend = static_cast<int>(std::lround(clamp01(message.value) * 16'383.0));
        return { status(0xe0), static_cast<std::uint8_t>(bend & 0x7f), static_cast<std::uint8_t>(bend >> 7) };
    }
    return {};
}

// ---- Modifiers ------------------------------------------------------------------------------

const char* modifierName(const ModifierKind kind) noexcept
{
    switch (kind) {
    case ModifierKind::range: return "range";
    case ModifierKind::invert: return "invert";
    case ModifierKind::curve: return "curve";
    case ModifierKind::relative: return "relative";
    case ModifierKind::toggle: return "toggle";
    case ModifierKind::threshold: return "threshold";
    case ModifierKind::deadzone: return "deadzone";
    case ModifierKind::steps: return "steps";
    case ModifierKind::scale: return "scale";
    }
    return "range";
}

std::optional<ModifierKind> modifierKindNamed(const std::string_view name) noexcept
{
    for (int index = 0; index < modifierKindCount; ++index)
        if (name == modifierName(static_cast<ModifierKind>(index)))
            return static_cast<ModifierKind>(index);
    return std::nullopt;
}

Modifier defaultModifier(const ModifierKind kind) noexcept
{
    switch (kind) {
    case ModifierKind::range: return { kind, 0.0, 1.0 };
    case ModifierKind::invert: return { kind, 0.0, 0.0 };
    case ModifierKind::curve: return { kind, 0.5, 0.0 };
    case ModifierKind::relative: return { kind, 0.0, 0.01 };
    case ModifierKind::toggle: return { kind, 0.0, 0.0 };
    case ModifierKind::threshold: return { kind, 0.5, 0.0 };
    case ModifierKind::deadzone: return { kind, 0.08, 0.0 };
    case ModifierKind::steps: return { kind, 4.0, 0.0 };
    case ModifierKind::scale: return { kind, 0.0, 1.0 };
    }
    return { kind, 0.0, 1.0 };
}

// ---- Virtual controllers --------------------------------------------------------------------

const char* controlKindName(const ControlKind kind) noexcept
{
    switch (kind) {
    case ControlKind::knob: return "knob";
    case ControlKind::fader: return "fader";
    case ControlKind::encoder: return "encoder";
    case ControlKind::button: return "button";
    case ControlKind::toggle: return "toggle";
    case ControlKind::pad: return "pad";
    case ControlKind::axis: return "axis";
    }
    return "knob";
}

std::optional<ControlKind> controlKindNamed(const std::string_view name) noexcept
{
    for (int index = 0; index < controlKindCount; ++index)
        if (name == controlKindName(static_cast<ControlKind>(index)))
            return static_cast<ControlKind>(index);
    return std::nullopt;
}

std::vector<Modifier> defaultChain(const ControlKind kind)
{
    switch (kind) {
    case ControlKind::encoder: return { defaultModifier(ModifierKind::relative) };
    case ControlKind::button: return { defaultModifier(ModifierKind::threshold) };
    case ControlKind::toggle: return { defaultModifier(ModifierKind::toggle) };
    case ControlKind::axis: return { defaultModifier(ModifierKind::deadzone) };
    default: return {};
    }
}

ControlKind kindForAddress(const std::string_view address) noexcept
{
    if (isNoteAddress(address) || address.starts_with("pc/"))
        return ControlKind::pad;
    if (address.starts_with("pb/") || address.starts_with("axis/"))
        return ControlKind::axis;
    if (address.starts_with("button/") || address.starts_with("key/"))
        return ControlKind::button;
    return ControlKind::knob;
}

bool VirtualControl::relative() const noexcept
{
    return std::any_of(chain.begin(), chain.end(), [](const Modifier& m) { return m.kind == ModifierKind::relative; });
}

double applyChain(VirtualControl& control, const double raw) noexcept
{
    auto x = std::isfinite(raw) ? raw : 0.0;
    const auto previousRaw = control.lastRaw;
    control.lastRaw = x;
    for (const auto& m : control.chain) {
        switch (m.kind) {
        case ModifierKind::range:
            x = m.b != m.a ? clamp01((x - m.a) / (m.b - m.a)) : clamp01(x);
            break;
        case ModifierKind::invert:
            x = 1.0 - clamp01(x);
            break;
        case ModifierKind::curve:
            x = std::pow(clamp01(x), curveExponent(m.a));
            break;
        case ModifierKind::relative: {
            const auto tick = ticks(midi7(x), static_cast<int>(std::lround(m.a)));
            x = clamp01((control.known ? control.value : half) + tick * m.b);
            break;
        }
        case ModifierKind::toggle: {
            const auto now = control.known ? control.value : 0.0;
            x = x >= half && previousRaw < half ? (now >= half ? 0.0 : 1.0) : now;
            break;
        }
        case ModifierKind::threshold:
            x = x >= m.a ? 1.0 : 0.0;
            break;
        case ModifierKind::deadzone: {
            const auto zone = std::clamp(m.a, 0.0, 0.49);
            const auto offset = clamp01(x) - half;
            x = std::abs(offset) <= zone ? half
                                         : half + std::copysign((std::abs(offset) - zone) / (half - zone) * half, offset);
            break;
        }
        case ModifierKind::steps: {
            const auto count = std::max(2.0, std::round(m.a));
            x = std::round(clamp01(x) * (count - 1.0)) / (count - 1.0);
            break;
        }
        case ModifierKind::scale:
            x = clamp01(m.a + clamp01(x) * (m.b - m.a));
            break;
        }
    }
    control.value = clamp01(x);
    control.known = true;
    return control.value;
}

double feedbackValue(const VirtualControl& control, const double value) noexcept
{
    auto x = clamp01(value);
    for (auto m = control.chain.rbegin(); m != control.chain.rend(); ++m) {
        switch (m->kind) {
        case ModifierKind::range:
            x = clamp01(m->a + x * (m->b - m->a));
            break;
        case ModifierKind::invert:
            x = 1.0 - x;
            break;
        case ModifierKind::curve:
            x = std::pow(x, 1.0 / curveExponent(m->a));
            break;
        case ModifierKind::scale:
            x = m->b != m->a ? clamp01((x - m->a) / (m->b - m->a)) : x;
            break;
        case ModifierKind::steps: {
            const auto count = std::max(2.0, std::round(m->a));
            x = std::round(x * (count - 1.0)) / (count - 1.0);
            break;
        }
        // An encoder's ring, a button's light, a stick: the value as it is.
        case ModifierKind::relative:
        case ModifierKind::toggle:
        case ModifierKind::threshold:
        case ModifierKind::deadzone:
            break;
        }
    }
    return x;
}

// ---- The router -----------------------------------------------------------------------------

VirtualDevice* Router::findDevice(const std::string_view id) noexcept
{
    for (auto& device : devices)
        if (device.id == id)
            return &device;
    return nullptr;
}

VirtualControl* Router::findControl(const std::string_view device, const std::string_view control) noexcept
{
    if (auto* found = findDevice(device))
        for (auto& item : found->controls)
            if (item.id == control)
                return &item;
    return nullptr;
}

std::optional<std::pair<std::size_t, std::size_t>> Router::controlFedBy(const std::string_view rawDevice,
                                                                        const std::string_view address) const noexcept
{
    for (std::size_t d = 0; d < devices.size(); ++d)
        for (std::size_t c = 0; c < devices[d].controls.size(); ++c)
            if (devices[d].controls[c].sourceDevice == rawDevice && devices[d].controls[c].sourceAddress == address)
                return std::pair { d, c };
    return std::nullopt;
}

std::vector<Router::Moved> Router::receive(const std::string_view rawDevice, const RawMessage& message)
{
    std::vector<Moved> moved;
    for (std::size_t d = 0; d < devices.size(); ++d)
        for (std::size_t c = 0; c < devices[d].controls.size(); ++c) {
            auto& control = devices[d].controls[c];
            if (control.sourceDevice != rawDevice || control.sourceAddress != message.address)
                continue;
            const auto previous = control.known ? control.value : 0.0;
            const auto value = applyChain(control, message.value);
            moved.push_back({ d, c, previous, value });
        }
    return moved;
}

std::vector<Router::Order> Router::orders(const std::vector<Moved>& moved,
                                          const std::function<std::optional<double>(const std::string&)>& current)
{
    std::vector<Order> result;
    for (const auto& item : moved) {
        if (item.device >= devices.size() || item.control >= devices[item.device].controls.size())
            continue;
        const auto& device = devices[item.device];
        const auto& control = device.controls[item.control];
        for (auto& assignment : assignments) {
            if (assignment.device != device.id || assignment.control != control.id)
                continue;
            const auto mapped = assignment.minimum + item.value * (assignment.maximum - assignment.minimum);
            if (assignment.takeover && !assignment.caught && !control.momentary() && !control.relative()) {
                // Caught when it reaches the target, or crosses it.
                const auto there = current ? current(assignment.target) : std::nullopt;
                const auto before = assignment.minimum + item.previous * (assignment.maximum - assignment.minimum);
                if (!there || std::abs(mapped - *there) < 0.03 || (before - *there) * (mapped - *there) <= 0.0)
                    assignment.caught = true;
                else
                    continue;
            }
            Order order;
            order.target = assignment.target;
            order.value = std::clamp(mapped, std::min(assignment.minimum, assignment.maximum),
                                     std::max(assignment.minimum, assignment.maximum));
            order.pressed = item.previous < half && item.value >= half;
            order.released = item.previous >= half && item.value < half;
            order.momentary = control.momentary();
            result.push_back(std::move(order));
        }
    }
    return result;
}

std::vector<Router::Feedback> Router::targetMoved(const std::string_view target, const double value, const bool byThisRouter)
{
    std::vector<Feedback> result;
    for (auto& assignment : assignments) {
        if (assignment.target != target)
            continue;
        auto* control = findControl(assignment.device, assignment.control);
        if (control == nullptr)
            continue;
        const auto span = assignment.maximum - assignment.minimum;
        const auto position = span != 0.0 ? clamp01((value - assignment.minimum) / span) : 0.0;
        // Moved otherwise: an encoder goes on from there, a takeover waits
        // to be caught again.
        if (!byThisRouter) {
            if (control->relative() || control->kind == ControlKind::toggle) {
                control->value = position;
                control->known = true;
            } else if (assignment.takeover && control->known) {
                assignment.caught = std::abs(control->value - position) < 0.03;
            }
        }
        if (!control->feedbackDevice.empty() && !control->feedbackAddress.empty())
            result.push_back({ control->feedbackDevice, { control->feedbackAddress, feedbackValue(*control, position) } });
    }
    return result;
}

std::string Router::freshId(const std::string_view stem, const std::vector<std::string>& taken)
{
    for (int index = 1;; ++index) {
        auto id = std::string(stem) + "-" + std::to_string(index);
        if (std::find(taken.begin(), taken.end(), id) == taken.end())
            return id;
    }
}

} // namespace cydaw::control
