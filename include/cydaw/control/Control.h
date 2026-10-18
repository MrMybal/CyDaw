#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Controllers. What hardware and the network say is RAW: a MIDI port, an OSC
// socket, a device an application plug-in brings (a gamepad), each with its
// own addresses ("cc/1/74", "/fader1", "button/a"). Virtual controllers are
// made from them: each virtual control is fed by one raw address through a
// chain of modifiers (a range, a curve, an encoder's ticks…) and says its
// value back the other way (LED rings, motorised faders, a tablet). What
// CyDAW uses is the virtual side: assignments tie a virtual control to a
// target (a card's knob, a track's fader, the transport, a deck).
//
// The application owns the devices; this is the model they share, without
// JUCE, so it is tested as the engine is.

namespace cydaw::control {

// ---- Raw messages ---------------------------------------------------------------------------

// A raw device's message: an address in its own words and a value. MIDI
// values are 0..1: "cc/<channel 1..16>/<number>", "note/<ch>/<n>" (its
// velocity; 0 when let go), "pb/<ch>" (the bend, 0.5 in the middle),
// "at/<ch>" (pressure), "pat/<ch>/<n>" (a key's pressure), "pc/<ch>/<n>"
// (a program, 1). OSC: the address as sent, its first number as it is (a
// second number at "<address>#1"…).
struct RawMessage {
    std::string address;
    double value {};
    bool operator==(const RawMessage&) const = default;
};

// A MIDI message read (none for what has no address: clock, SysEx…).
[[nodiscard]] std::optional<RawMessage> fromMidi(const std::uint8_t* bytes, std::size_t size);
// The bytes that say a message to a MIDI device; empty when the address is
// not a MIDI one.
[[nodiscard]] std::vector<std::uint8_t> toMidi(const RawMessage& message);
// A 0..1 value as MIDI's 0..127.
[[nodiscard]] int midi7(double value) noexcept;
// Whether an address names a key (its note-ons and note-offs are a press).
[[nodiscard]] bool isNoteAddress(std::string_view address) noexcept;
// A press and a let go: a key (a MIDI one, "note/…", or the computer's,
// "key/…"), a gamepad's "button/…". Its let go (0) teaches nothing.
[[nodiscard]] bool isPressAddress(std::string_view address) noexcept;

// ---- Modifiers ------------------------------------------------------------------------------

enum class ModifierKind : std::uint8_t {
    range,     // the raw part [a, b] spread over 0..1
    invert,    // 1 - x
    curve,     // a in -1..1: below 0 slow at first, above fast at first
    relative,  // an encoder's ticks: a = 0 two's complement, 1 offset from 64, 2 sign bit; b = the step a tick moves
    toggle,    // a press (over half) turns it on, the next one off; letting go does nothing
    threshold, // a button from anything: 1 at or over a, else 0
    deadzone,  // a centred axis (a stick): within a of the middle, the middle
    steps,     // a = how many values it lands on (2 and more)
    scale,     // 0..1 onto [a, b]
};
inline constexpr int modifierKindCount = 9;

struct Modifier {
    ModifierKind kind { ModifierKind::range };
    double a {};
    double b { 1.0 };
    bool operator==(const Modifier&) const = default;
};

// Its name in files and commands ("range", "invert"…), and back.
[[nodiscard]] const char* modifierName(ModifierKind kind) noexcept;
[[nodiscard]] std::optional<ModifierKind> modifierKindNamed(std::string_view name) noexcept;
// A modifier with its usual settings.
[[nodiscard]] Modifier defaultModifier(ModifierKind kind) noexcept;

// ---- Virtual controllers --------------------------------------------------------------------

enum class ControlKind : std::uint8_t {
    knob,    // turned to a place
    fader,   // the same, upright
    encoder, // turned without end: ticks
    button,  // pressed while held
    toggle,  // pressed on, pressed off
    pad,     // hit, with its velocity
    axis,    // a stick or a pedal
};
inline constexpr int controlKindCount = 7;

[[nodiscard]] const char* controlKindName(ControlKind kind) noexcept;
[[nodiscard]] std::optional<ControlKind> controlKindNamed(std::string_view name) noexcept;
// What a control of that kind starts with.
[[nodiscard]] std::vector<Modifier> defaultChain(ControlKind kind);
// A raw address's likely kind (a key: a pad; a bend: an axis; else a knob).
[[nodiscard]] ControlKind kindForAddress(std::string_view address) noexcept;

struct VirtualControl {
    std::string id;
    std::string name;
    ControlKind kind { ControlKind::knob };
    // What feeds it: a raw device and one of its addresses.
    std::string sourceDevice;
    std::string sourceAddress;
    // Where its value goes back (LEDs, motorised faders, a tablet); an
    // empty device: nowhere.
    std::string feedbackDevice;
    std::string feedbackAddress;
    std::vector<Modifier> chain;
    // Where it is now (0..1), whether a message has said so yet, and the
    // last raw value (a toggle looks for a press).
    double value {};
    bool known {};
    double lastRaw {};

    // A momentary control: a press, then a let go.
    [[nodiscard]] bool momentary() const noexcept { return kind == ControlKind::button || kind == ControlKind::pad; }
    [[nodiscard]] bool relative() const noexcept;
};

struct VirtualDevice {
    std::string id;
    std::string name;
    // The hardware it is: the raw device its controls listen to, and the
    // one their values go back to (empty: not chosen yet).
    std::string input;
    std::string output;
    std::vector<VirtualControl> controls;
};

// The value a raw value gives the control, through its chain (an encoder
// and a toggle start from where the control is, and keep it).
[[nodiscard]] double applyChain(VirtualControl& control, double raw) noexcept;
// The raw value that says `value` back (the chain the other way).
[[nodiscard]] double feedbackValue(const VirtualControl& control, double value) noexcept;

// ---- Assignments ----------------------------------------------------------------------------

struct Assignment {
    std::string device;  // the virtual device's id
    std::string control; // its control's id
    std::string target;  // what it moves in CyDAW
    // The part of the target's course the control covers.
    double minimum {};
    double maximum { 1.0 };
    // Moves the target only once the control has reached where it is: no
    // jump after the target moved otherwise.
    bool takeover {};
    // Kept with the project; else with the controllers, for every project.
    bool project {};
    // Takeover: whether the control holds the target now.
    bool caught { true };
};

// ---- The router -----------------------------------------------------------------------------

class Router {
public:
    std::vector<VirtualDevice> devices;
    std::vector<Assignment> assignments;

    struct Moved {
        std::size_t device {};
        std::size_t control {};
        double previous {};
        double value {};
    };
    // A raw message: the virtual controls it moves (their values updated).
    std::vector<Moved> receive(std::string_view rawDevice, const RawMessage& message);

    struct Order {
        std::string target;
        double value {};        // where the target goes, 0..1
        bool pressed {};        // the control went over half
        bool released {};       // and back under
        bool momentary {};      // a button or a pad (a toggle target flips on its press)
    };
    // What moved controls ask of their targets. `current` says where a
    // target is now (for takeover); none when unknown.
    std::vector<Order> orders(const std::vector<Moved>& moved,
                              const std::function<std::optional<double>(const std::string&)>& current);

    struct Feedback {
        std::string device;
        RawMessage message;
    };
    // A target now at `value`: what the controls assigned to it say back,
    // and where they now are (an encoder goes on from there; a takeover is
    // lost when the target went elsewhere).
    std::vector<Feedback> targetMoved(std::string_view target, double value, bool byThisRouter);

    [[nodiscard]] VirtualDevice* findDevice(std::string_view id) noexcept;
    [[nodiscard]] VirtualControl* findControl(std::string_view device, std::string_view control) noexcept;
    // The virtual control a raw address feeds, if any.
    [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> controlFedBy(std::string_view rawDevice,
                                                                                  std::string_view address) const noexcept;
    // An id not yet used among `taken` ("knob-3").
    [[nodiscard]] static std::string freshId(std::string_view stem, const std::vector<std::string>& taken);
};

} // namespace cydaw::control
