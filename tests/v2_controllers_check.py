"""The controllers tool in the software. Silent (the master muted at -60 dB).

    python tests/v2_controllers_check.py --exe "<CyDAW 2.exe>" [--port 47856]

Real OSC on this machine's UDP sockets (127.0.0.1): the test sends to CyDAW's
OSC endpoint and listens for what CyDAW says back. MIDI-shaped addresses are
injected through the control port (no MIDI port is needed).

* An OSC endpoint listens; a virtual control fed by one of its addresses,
  through a range, moves a track's volume; the target moved otherwise is said
  back to the endpoint through the chain the other way.
* An encoder (relative ticks) moves the volume from where it is; a button
  toggles the mute at each press; takeover waits for the control to reach
  the target.
* /cydaw/<target> moves a target directly and is answered when it moves.
* Learning: a target picked, a raw control moved: a virtual control is made
  in the device whose input it is, and assigned.
* Devices: a new device finds its hardware when something on it moves
  (named after it), learns a control per new address moved on its input
  (a key's let go teaches nothing, another input is not heard), follows its
  input when it changes, and copies wait for hardware of their own. The
  computer's keyboard is a raw device whose keys drive rules.
* A card's knob and a deck's EQ are targets too.
* The track's assignments go with the project (saved, a new project, opened).

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

failures: list[str] = []
port = 47856
exe_path = Path()


def request(command: dict) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(120)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(1 << 20)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode("utf-8"))


def need(command: dict, what: str) -> dict:
    answer = request(command)
    if not answer.get("ok"):
        raise RuntimeError(f"{what}: {answer.get('error')}")
    return answer.get("result", {})


def ctl(what: str, **fields) -> dict:
    return need(dict({"cmd": "controllers"}, **fields), what)


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def osc_string(text: str) -> bytes:
    data = text.encode("utf-8") + b"\x00"
    return data + b"\x00" * ((4 - len(data) % 4) % 4)


def osc_message(address: str, *values: float) -> bytes:
    return osc_string(address) + osc_string("," + "f" * len(values)) + b"".join(struct.pack(">f", v) for v in values)


def osc_read(packet: bytes) -> tuple[str, list[float]]:
    end = packet.index(b"\x00")
    address = packet[:end].decode("utf-8")
    at = (end + 4) & ~3
    tags_end = packet.index(b"\x00", at)
    tags = packet[at + 1:tags_end].decode("ascii")
    at = (tags_end + 4) & ~3
    values = []
    for tag in tags:
        if tag == "f":
            values.append(struct.unpack(">f", packet[at:at + 4])[0])
            at += 4
        elif tag == "i":
            values.append(float(struct.unpack(">i", packet[at:at + 4])[0]))
            at += 4
    return address, values


def value_of(target: str) -> float:
    return float(ctl("value " + target, action="value", target=target).get("value", -1.0))


def wait_until(what: str, condition, seconds: float = 5.0) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        if condition():
            return True
        time.sleep(0.05)
    return False


def received(listener: socket.socket, address: str, seconds: float = 3.0) -> list[float] | None:
    """The last value CyDAW said at `address` within the time given."""
    end = time.time() + seconds
    found = None
    while time.time() < end:
        listener.settimeout(max(0.01, end - time.time()))
        try:
            packet, _ = listener.recvfrom(4096)
        except socket.timeout:
            break
        try:
            said, values = osc_read(packet)
        except (ValueError, struct.error):
            continue
        if said == address:
            found = values
            # Keep reading a moment: the last word counts.
            end = min(end, time.time() + 0.2)
    return found


def scenario(scratch: Path) -> None:
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "add_midi_track", "name": "Synth", "instrument": "opale-instrument"}, "synth track")
    targets = ctl("targets", action="targets", filter="/volume")["targets"]
    tracks = [t["target"] for t in targets if t["target"].startswith("track/") and t["label"].startswith("Synth")]
    check(len(tracks) == 1, f"the project's track is a target ({tracks})")
    volume = tracks[0]
    key = volume.split("/")[1]
    # An application plug-in, when built beside CyDAW: loaded and started.
    if (exe_path.parent / "ControlPlugins" / "Gamepads.cyctl").is_file():
        loaded = ctl("status", action="status")["plugins"]
        check(any(plugin["name"] == "Gamepads (XInput)" and plugin["started"] for plugin in loaded),
              f"the gamepads' application plug-in is loaded and started ({loaded})")
    mute = f"track/{key}/mute"
    pan = f"track/{key}/pan"

    # ---- An OSC endpoint: in and back ----
    cydaw_port = free_udp_port()
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind(("127.0.0.1", 0))
    answer_port = listener.getsockname()[1]
    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    made = ctl("osc", action="add_osc", name="Test tablet", port=cydaw_port, host="127.0.0.1", sendPort=answer_port)
    osc = made["device"]
    check(made.get("listening") and made.get("sending"), f"the OSC endpoint listens on {cydaw_port} and answers on {answer_port}")
    box = ctl("virtual", action="add_virtual", name="Test box")["device"]
    knob = ctl("knob", action="add_control", device=box, name="Volume knob", kind="knob", sourceDevice=osc,
               sourceAddress="/knob1", chain=[{"kind": "range", "a": 0.2, "b": 0.8}])["control"]
    ctl("assign", action="assign", device=box, control=knob, target=volume)
    sender.sendto(osc_message("/knob1", 0.5), ("127.0.0.1", cydaw_port))
    moved = wait_until("volume", lambda: abs(value_of(volume) - 0.5) < 0.01)
    check(moved, f"an OSC knob, through its range, moves the track's volume to the middle ({value_of(volume):.3f})")
    status = ctl("status", action="status")
    control = next(c for d in status["virtual"] if d["id"] == box for c in d["controls"] if c["id"] == knob)
    check(abs(control["value"] - 0.5) < 0.01 and control["feedbackDevice"] == osc and control["feedbackAddress"] == "/knob1",
          "the virtual control holds its value and answers on the same address")
    # Moved otherwise: said back through the range, the other way.
    while True:
        try:
            listener.settimeout(0.05)
            listener.recvfrom(4096)
        except socket.timeout:
            break
    ctl("apply", action="apply", target=volume, value=0.75)
    said = received(listener, "/knob1")
    check(said is not None and abs(said[0] - (0.2 + 0.75 * 0.6)) < 0.01,
          f"the volume moved otherwise is said back on /knob1 through the range ({said})")

    # ---- An encoder, a button, takeover ----
    encoder = ctl("encoder", action="add_control", device=box, name="Encoder", kind="encoder", sourceDevice=osc,
                  sourceAddress="cc/1/20")["control"]
    ctl("assign", action="assign", device=box, control=encoder, target=volume)
    ctl("apply", action="apply", target=volume, value=0.5)
    time.sleep(0.15)
    ctl("inject", action="inject", device=osc, address="cc/1/20", value=3 / 127)
    check(abs(value_of(volume) - 0.53) < 0.006, f"an encoder's three ticks move the volume from where it is ({value_of(volume):.3f})")
    ctl("inject", action="inject", device=osc, address="cc/1/20", value=126 / 127)
    check(abs(value_of(volume) - 0.51) < 0.006, f"and two ticks back ({value_of(volume):.3f})")
    button = ctl("button", action="add_control", device=box, name="Mute button", kind="button", sourceDevice=osc,
                 sourceAddress="note/1/36")["control"]
    ctl("assign", action="assign", device=box, control=button, target=mute)
    ctl("inject", action="inject", device=osc, address="note/1/36", value=1.0)
    ctl("inject", action="inject", device=osc, address="note/1/36", value=0.0)
    first = value_of(mute)
    ctl("inject", action="inject", device=osc, address="note/1/36", value=1.0)
    ctl("inject", action="inject", device=osc, address="note/1/36", value=0.0)
    check(first == 1.0 and value_of(mute) == 0.0, "a button toggles the mute at each press, not at its let go")
    fader = ctl("fader", action="add_control", device=box, name="Pan fader", kind="fader", sourceDevice=osc, sourceAddress="/fader2")["control"]
    ctl("assign", action="assign", device=box, control=fader, target=pan, takeover=True)
    ctl("apply", action="apply", target=pan, value=0.8)
    time.sleep(0.2)
    ctl("inject", action="inject", device=osc, address="/fader2", value=0.3)
    held = value_of(pan)
    ctl("inject", action="inject", device=osc, address="/fader2", value=0.6)
    still = value_of(pan)
    ctl("inject", action="inject", device=osc, address="/fader2", value=0.85)
    check(abs(held - 0.8) < 0.01 and abs(still - 0.8) < 0.01 and abs(value_of(pan) - 0.85) < 0.01,
          f"takeover: the fader moves the pan only once it crosses it ({held:.2f}, {still:.2f}, {value_of(pan):.2f})")

    # ---- /cydaw/<target> ----
    sender.sendto(osc_message("/cydaw/transport/tempo", 0.5), ("127.0.0.1", cydaw_port))
    check(wait_until("tempo", lambda: abs(value_of("transport/tempo") - 0.5) < 0.002),
          f"/cydaw/transport/tempo moves the tempo itself ({value_of('transport/tempo'):.3f})")
    status = need({"cmd": "status"}, "status")
    check(abs(status.get("tempo", 0) - 160.0) < 0.5, f"to 160 BPM, half its course ({status.get('tempo')})")
    need({"cmd": "set_tempo", "bpm": 90}, "tempo")
    said = received(listener, "/cydaw/transport/tempo")
    check(said is not None and abs(said[0] - (90 - 20) / 280) < 0.01, f"the tempo moved otherwise is said back at /cydaw/transport/tempo ({said})")

    # ---- Learning ----
    ctl("learn", action="learn", on=True, target=pan)
    ctl("inject", action="inject", device=osc, address="/learnt", value=0.25)
    status = ctl("status", action="status")
    learnt = [a for a in status["assignments"] if a["target"] == pan]
    check(len(learnt) == 1 and learnt[0]["device"] == box and not status["learnTarget"],
          f"learning: the moved control is made a control of the device the endpoint feeds, and takes the target ({learnt})")
    boxed = next(d for d in status["virtual"] if d["id"] == box)
    check(boxed["input"] == osc and boxed["output"] == osc,
          f"the device took its first control's endpoint as its input and feedback ({boxed['input']}, {boxed['output']})")
    ctl("learn", action="learn", on=False)
    ctl("inject", action="inject", device=osc, address="/learnt", value=0.25)
    check(abs(value_of(pan) - 0.25) < 0.01, f"the learnt control moves the pan ({value_of(pan):.3f})")

    devices_scenario(osc, mute)

    # ---- A card's knob, a deck ----
    parameters = [t for t in ctl("params", action="targets", filter="param/")["targets"] if t["target"].startswith(f"param/{key}/")]
    check(len(parameters) > 0, f"the track's cards' knobs are targets ({len(parameters)})")
    if parameters:
        # A continuous one when there is: a level, a cutoff, a mix.
        preferred = [t["target"] for t in parameters if any(w in t["target"].lower() for w in ("cutoff", "level", "gain", "mix"))]
        parameter = preferred[0] if preferred else parameters[0]["target"]
        card = ctl("card knob", action="add_control", device=box, name="Card knob", kind="knob", sourceDevice=osc,
                   sourceAddress="/card")["control"]
        ctl("assign", action="assign", device=box, control=card, target=parameter)
        ctl("inject", action="inject", device=osc, address="/card", value=0.3)
        check(abs(value_of(parameter) - 0.3) < 0.02, f"a control turns a card's knob ({parameter}: {value_of(parameter):.3f})")
    eq = ctl("eq", action="add_control", device=box, name="Deck A high", kind="knob", sourceDevice=osc, sourceAddress="/high")["control"]
    ctl("assign", action="assign", device=box, control=eq, target="dj/A/high")
    ctl("inject", action="inject", device=osc, address="/high", value=0.2)
    check(abs(value_of("dj/A/high") - 0.2) < 0.01, "a control turns deck A's high EQ")

    # ---- The project keeps its tracks' assignments ----
    status = ctl("status", action="status")
    scopes = {a["target"]: a["scope"] for a in status["assignments"]}
    check(scopes.get(volume) == "project" and scopes.get("dj/A/high") == "global",
          f"a track's assignment is the project's, a deck's every project's ({scopes.get(volume)}, {scopes.get('dj/A/high')})")
    project = scratch / "controllers-check.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    need({"cmd": "new_project"}, "new")
    time.sleep(0.3)
    after_new = [a["target"] for a in ctl("status", action="status")["assignments"]]
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    time.sleep(0.3)
    # The opened project's track has a key of its own now: found by its name.
    reopened_targets = ctl("targets", action="targets", filter="/volume")["targets"]
    volume = next((t["target"] for t in reopened_targets if t["target"].startswith("track/") and t["label"].startswith("Synth")), "")
    pan = volume.replace("/volume", "/pan")
    reopened = [a["target"] for a in ctl("status", action="status")["assignments"]]
    check(not any(t.startswith("track/") for t in after_new) and "dj/A/high" in after_new and volume in reopened and pan in reopened,
          f"a new project drops the track's assignments, keeps the deck's; opening the project brings them back, on its track ({volume})")
    ctl("inject", action="inject", device=osc, address="/knob1", value=0.8)
    check(abs(value_of(volume) - 1.0) < 0.01, f"and the reopened assignment moves the volume ({value_of(volume):.3f})")
    monitor = ctl("monitor", action="monitor", count=400)["messages"]
    check(any(m["address"] == "/knob1" and not m["out"] for m in monitor) and any(m["out"] for m in monitor),
          "the monitor shows what came in and what went back")
    listener.close()
    sender.close()


def device_of(device_id: str) -> dict:
    return next(d for d in ctl("status", action="status")["virtual"] if d["id"] == device_id)


def devices_scenario(osc: str, mute: str) -> None:
    """A device made around its hardware: found, its controls learnt, moved."""
    pad_port = free_udp_port()
    pads = ctl("pads", action="add_osc", name="Test pads", port=pad_port)["device"]
    made = ctl("new device", action="add_virtual")["device"]
    fresh = device_of(made)
    check(fresh["input"] == "" and fresh["name"].startswith(("New device", "Nouveau périphérique")),
          f"a new device has no hardware yet and a name to change ({fresh['name']})")
    # Its hardware: what moves next.
    ctl("learn hardware", action="learn_hardware", device=made)
    ctl("let go", action="inject", device=pads, address="note/1/40", value=0.0)
    check(device_of(made)["input"] == "", "a key let go does not say what the device is")
    ctl("pad", action="inject", device=pads, address="/pad/1", value=1.0)
    found = device_of(made)
    status = ctl("status", action="status")
    check(found["input"] == pads and found["name"] == "Test pads" and not status["learnHardware"],
          f"something moved on the hardware makes it the device's input, and names it ({found['input']}, {found['name']})")
    # Its controls: one per new address moved on its input.
    ctl("learn controls", action="learn_controls", device=made)
    ctl("pad 1", action="inject", device=pads, address="/pad/1", value=1.0)
    ctl("pad 1 again", action="inject", device=pads, address="/pad/1", value=0.4)
    ctl("let go", action="inject", device=pads, address="note/1/41", value=0.0)
    ctl("pressed", action="inject", device=pads, address="note/1/41", value=0.8)
    ctl("elsewhere", action="inject", device=osc, address="/elsewhere", value=0.5)
    controls = device_of(made)["controls"]
    addresses = sorted(c["sourceAddress"] for c in controls)
    check(addresses == ["/pad/1", "note/1/41"] and all(c["sourceDevice"] == pads for c in controls),
          f"learning its controls: one per new address on its input, none for a let go or another input ({addresses})")
    pad = next((c for c in controls if c["sourceAddress"] == "note/1/41"), {})
    check(pad.get("kind") == "pad", f"a key learnt is a pad ({pad.get('kind')})")
    ctl("learn off", action="learn_controls", on=False)
    ctl("after", action="inject", device=pads, address="/pad/2", value=1.0)
    check(len(device_of(made)["controls"]) == 2, "learning stopped: nothing more is made")
    # Another box: its controls follow the input.
    other_port = free_udp_port()
    other = ctl("other pads", action="add_osc", name="Other pads", port=other_port)["device"]
    ctl("other input", action="set_virtual", device=made, input=other)
    moved = device_of(made)
    check(moved["input"] == other and all(c["sourceDevice"] == other for c in moved["controls"]),
          f"the device's input changed: its controls listen to the new one ({moved['input']})")
    # A copy waits for its own hardware, and keeps what the rules move.
    first = moved["controls"][0]["id"]
    ctl("assign pad", action="assign", device=made, control=first, target="transport/play")
    copy = ctl("copy", action="duplicate_virtual", device=made)["device"]
    copied = device_of(copy)
    assigned = [a for a in ctl("status", action="status")["assignments"] if a["device"] == copy]
    check(copied["input"] == "" and len(copied["controls"]) == 2 and all(c["sourceDevice"] == "" for c in copied["controls"])
          and [a["target"] for a in assigned] == ["transport/play"],
          f"a copy has the rules and what they move, and waits for hardware ({copied['input']!r}, {len(assigned)})")
    ctl("unassign", action="unassign", target="transport/play")
    ctl("remove copy", action="remove_virtual", device=copy)
    ctl("remove made", action="remove_virtual", device=made)
    ctl("remove pads", action="remove_raw", device=pads)
    ctl("remove other", action="remove_raw", device=other)
    # The computer's keyboard: a raw device; its keys drive rules.
    raws = ctl("status", action="status")["raw"]
    keyboard = next((r for r in raws if r["kind"] == "keyboard"), None)
    check(keyboard is not None and keyboard["id"] == "keyboard:computer" and keyboard["present"],
          f"the computer's keyboard is a raw device ({keyboard})")
    keys = ctl("keys", action="add_virtual", input="keyboard:computer")["device"]
    check(device_of(keys)["output"] == "", "the computer's keyboard answers nothing")
    key = ctl("key", action="add_control", device=keys, kind="button", sourceAddress="key/F2")["control"]
    check(next(c for c in device_of(keys)["controls"] if c["id"] == key)["sourceDevice"] == "keyboard:computer",
          "a rule added to the device listens to its input")
    ctl("assign key", action="assign", device=keys, control=key, target=mute)
    before = value_of(mute)
    ctl("press", action="inject", device="keyboard:computer", address="key/F2", value=1.0)
    ctl("let go", action="inject", device="keyboard:computer", address="key/F2", value=0.0)
    check(value_of(mute) != before, f"a key of the computer's keyboard toggles the mute ({before} -> {value_of(mute)})")
    ctl("press", action="inject", device="keyboard:computer", address="key/F2", value=1.0)
    ctl("let go", action="inject", device="keyboard:computer", address="key/F2", value=0.0)
    ctl("remove keys", action="remove_virtual", device=keys)


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    global exe_path
    exe_path = Path(args.exe).resolve()
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-controllers-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {"cache": str(scratch / "Cache")}}),
                         encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent", "--window-size=1500x900"],
                               env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(300):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        if not need({"cmd": "status"}, "status").get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        scenario(scratch)
    except Exception as error:  # noqa: BLE001
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        if process.poll() is None:
            try:
                request({"cmd": "quit"})
                process.wait(timeout=30)
            except Exception:  # noqa: BLE001
                process.kill()
    print(f"{len(failures)} failure(s)" if failures else "all checks hold", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
