// Controllers: raw MIDI read and written, modifiers both ways, virtual
// controls moving their targets. Included by EngineTests.cpp inside its
// namespace.

void controlMidiAddressesGoBothWays()
{
    using cydaw::control::RawMessage;
    const std::uint8_t cc[] { 0xb1, 74, 127 };
    const auto read = cydaw::control::fromMidi(cc, 3);
    expect(read && read->address == "cc/2/74" && read->value == 1.0, "a controller change reads as cc/<channel>/<number>");
    const std::uint8_t off[] { 0x80, 60, 40 };
    const auto released = cydaw::control::fromMidi(off, 3);
    expect(released && released->address == "note/1/60" && released->value == 0.0, "a note-off is the key at 0");
    const std::uint8_t bend[] { 0xe0, 0x00, 0x40 };
    const auto bent = cydaw::control::fromMidi(bend, 3);
    expect(bent && bent->address == "pb/1" && std::abs(bent->value - 8'192.0 / 16'383.0) < 1e-9, "the bend's middle reads near half");
    const std::uint8_t clock[] { 0xf8 };
    expect(!cydaw::control::fromMidi(clock, 1), "the clock has no address");
    for (const auto& message : { RawMessage { "cc/16/7", 100 / 127.0 }, RawMessage { "note/3/36", 1.0 },
                                 RawMessage { "pb/1", 0.25 }, RawMessage { "at/5", 64 / 127.0 } }) {
        const auto bytes = cydaw::control::toMidi(message);
        const auto back = cydaw::control::fromMidi(bytes.data(), bytes.size());
        expect(back && back->address == message.address && std::abs(back->value - message.value) < 1.0 / 16'000.0,
               "a MIDI address written then read is the same (" + message.address + ")");
    }
    expect(cydaw::control::toMidi({ "/fader1", 0.5 }).empty() && cydaw::control::toMidi({ "cc/17/1", 0.5 }).empty(),
           "an OSC address, a 17th channel: no MIDI");
}

void controlModifiersShapeAndGiveBack()
{
    using namespace cydaw::control;
    VirtualControl control;
    control.chain = { { ModifierKind::range, 0.2, 0.8 }, { ModifierKind::invert } };
    expect(std::abs(applyChain(control, 0.35) - 0.75) < 1e-9, "a range then an inversion shape the value");
    expect(std::abs(feedbackValue(control, 0.75) - 0.35) < 1e-9, "the chain the other way gives the raw value back");
    control.chain = { { ModifierKind::curve, 0.5 } };
    const auto curved = applyChain(control, 0.25);
    expect(curved > 0.25 && std::abs(feedbackValue(control, curved) - 0.25) < 1e-9, "a curve bends, and unbends for the feedback");

    // An encoder: ticks move it from where it is, in every encoding.
    VirtualControl encoder;
    encoder.kind = ControlKind::encoder;
    encoder.chain = { { ModifierKind::relative, 0.0, 0.01 } };
    encoder.value = 0.5;
    encoder.known = true;
    (void) applyChain(encoder, 3 / 127.0);
    (void) applyChain(encoder, 126 / 127.0);
    expect(std::abs(encoder.value - 0.51) < 1e-9, "two's complement: +3 then -2 ticks");
    encoder.chain[0].a = 1.0;
    (void) applyChain(encoder, 60 / 127.0);
    expect(std::abs(encoder.value - 0.47) < 1e-9, "offset from 64: 60 is 4 ticks down");
    encoder.chain[0].a = 2.0;
    (void) applyChain(encoder, (64 + 5) / 127.0);
    expect(std::abs(encoder.value - 0.42) < 1e-9, "sign bit: 64 + 5 is 5 ticks down");

    // A toggle flips on a press only.
    VirtualControl toggle;
    toggle.kind = ControlKind::toggle;
    toggle.chain = defaultChain(ControlKind::toggle);
    const auto on = applyChain(toggle, 1.0);
    const auto held = applyChain(toggle, 0.0);
    const auto off = applyChain(toggle, 1.0);
    expect(on == 1.0 && held == 1.0 && off == 0.0, "a toggle turns on at a press, stays when let go, turns off at the next press");

    // A stick: its middle held still, the rest spread to the ends.
    VirtualControl stick;
    stick.chain = { { ModifierKind::deadzone, 0.1 } };
    expect(applyChain(stick, 0.55) == 0.5 && applyChain(stick, 1.0) == 1.0 && applyChain(stick, 0.0) == 0.0,
           "a dead zone holds the middle and keeps the ends");
    VirtualControl stepped;
    stepped.chain = { { ModifierKind::steps, 3.0 } };
    expect(applyChain(stepped, 0.3) == 0.5, "three steps: 0, half, 1");
}

void controlRouterMovesTargetsAndFeedsBack()
{
    using namespace cydaw::control;
    Router router;
    VirtualDevice box;
    box.id = "box";
    VirtualControl knob;
    knob.id = "knob-1";
    knob.sourceDevice = "midi-in:box";
    knob.sourceAddress = "cc/1/21";
    knob.feedbackDevice = "midi-out:box";
    knob.feedbackAddress = "cc/1/21";
    VirtualControl button = knob;
    button.id = "button-1";
    button.kind = ControlKind::button;
    button.sourceAddress = "note/1/36";
    button.feedbackAddress = "note/1/36";
    button.chain = defaultChain(ControlKind::button);
    box.controls = { knob, button };
    router.devices.push_back(box);
    router.assignments.push_back({ "box", "knob-1", "track/1/volume", 0.25, 0.75 });
    router.assignments.push_back({ "box", "button-1", "transport/play" });

    const auto none = [](const std::string&) -> std::optional<double> { return std::nullopt; };
    auto orders = router.orders(router.receive("midi-in:box", { "cc/1/21", 0.5 }), none);
    expect(orders.size() == 1 && orders[0].target == "track/1/volume" && std::abs(orders[0].value - 0.5) < 1e-9,
           "a knob moves its target over the part of its course it covers");
    expect(router.receive("midi-in:other", { "cc/1/21", 0.5 }).empty(), "another device's same address moves nothing");
    orders = router.orders(router.receive("midi-in:box", { "note/1/36", 1.0 }), none);
    expect(orders.size() == 1 && orders[0].pressed && orders[0].momentary, "a button's press is a press");
    orders = router.orders(router.receive("midi-in:box", { "note/1/36", 0.0 }), none);
    expect(orders.size() == 1 && orders[0].released, "and letting go, a release");

    // The target moved otherwise: the knob's light follows.
    const auto back = router.targetMoved("track/1/volume", 0.75, false);
    expect(back.size() == 1 && back[0].device == "midi-out:box" && back[0].message.address == "cc/1/21"
               && std::abs(back[0].message.value - 1.0) < 1e-9,
           "a target moved says its place back to the control's feedback");

    // Takeover: the knob moves the target only once it reaches it.
    router.assignments[0].takeover = true;
    (void) router.targetMoved("track/1/volume", 0.75, false);
    const auto at = [](const std::string&) -> std::optional<double> { return 0.75; };
    expect(router.orders(router.receive("midi-in:box", { "cc/1/21", 0.6 }), at).empty(), "taken over: short of the target, nothing");
    expect(router.orders(router.receive("midi-in:box", { "cc/1/21", 1.0 }), at).size() == 1, "crossing it, the knob holds it");
}
