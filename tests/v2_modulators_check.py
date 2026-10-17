"""Modulators on the cards, in the software. Silent (the master muted at -60 dB).

    python tests/v2_modulators_check.py --exe "<CyDAW 2.exe>" [--port 47858]

* A macro added to a utility card belongs to it; modulating its gain by a
  quarter of its range lifts what the card gives out by as much (the chain
  as an exported plug-in gets it, a sine through it offline); a half, by as
  much again: the amount is the ring.
* An LFO's value moves while it runs; its choices are set by name.
* An envelope modulates a knob of an instrument (Opale's filter): an
  instrument's settings have sockets.
* Armed, its column open: a snapshot of the strip (for the eye).
* Saved and opened again, the modulators and their modulations are there.
* Taken off a knob, then removed, they are gone.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

failures: list[str] = []
port = 47858


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


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def events(*items: dict) -> None:
    need({"cmd": "ui_input", "events": list(items) + [{"type": "wait", "frames": 3}]}, "input")


def spots() -> dict:
    return {spot["key"]: spot for spot in need({"cmd": "ui_spots"}, "spots")["spots"]}


def card_of(track: str, word: str) -> dict:
    graph = need({"cmd": "get_track_graph", "track": track}, "graph")
    return next(node for node in graph["nodes"] if word in node["type"])


def output_db(track: str) -> float:
    return need({"cmd": "effect_chain_check", "track": track, "hz": 220, "amplitude": 0.3, "seconds": 0.5}, "chain")["outputDb"]


def scenario(scratch: Path) -> None:
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new")
    # A track without clips (a bus): its chain is what is measured.
    need({"cmd": "add_bus_track", "name": "Fx"}, "audio track")
    need({"cmd": "insert_device", "track": "Fx", "key": "utilitaire"}, "utility")
    utility = card_of("Fx", "utilitaire")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": utility["id"], "parameter": "utilitaire.gain", "value": -24},
         "gain")
    base = output_db("Fx")

    # A macro on the utility's gain.
    macro = need({"cmd": "modulator", "track": "Fx", "action": "add", "nodeId": utility["id"], "type": "macro"}, "add macro")
    check(macro["owner"] == utility["id"] and macro["type"] == "mod-macro",
          f"a macro added to a card belongs to it ({macro['owner']} for {utility['id']})")
    need({"cmd": "modulator", "track": "Fx", "action": "set", "id": macro["id"], "settings": {"value": 1}}, "macro value")
    moved = need({"cmd": "modulator", "track": "Fx", "action": "modulate", "id": macro["id"], "nodeId": utility["id"],
                  "parameter": "utilitaire.gain", "amount": 0.25}, "modulate")
    check(len(moved["modulations"]) == 1 and moved["modulations"][0]["mode"] == "modulate"
          and abs(moved["modulations"][0]["amount"] - 0.25) < 1e-6,
          "the macro modulates the gain by a quarter of its range")
    quarter = output_db("Fx")
    check(abs((quarter - base) - 18.0) < 1.5, f"a quarter of 72 dB lifts the sound by 18 dB ({quarter - base:+.1f} dB)")
    need({"cmd": "modulator", "track": "Fx", "action": "modulate", "id": macro["id"], "nodeId": utility["id"],
          "parameter": "utilitaire.gain", "amount": 0.5}, "amount")
    half = output_db("Fx")
    listed = need({"cmd": "modulator", "track": "Fx", "action": "list", "nodeId": utility["id"]}, "list")["modulators"]
    check(len(listed) == 1 and len(listed[0]["modulations"]) == 1 and abs(listed[0]["modulations"][0]["amount"] - 0.5) < 1e-6,
          "modulated again, the same cable takes the new amount")
    check(abs((half - base) - 36.0) < 1.5, f"half its range: 36 dB ({half - base:+.1f} dB)")

    # An LFO: it moves; its choices by name.
    lfo = need({"cmd": "modulator", "track": "Fx", "action": "add", "nodeId": utility["id"], "type": "lfo"}, "add lfo")
    set_lfo = need({"cmd": "modulator", "track": "Fx", "action": "set", "id": lfo["id"],
                    "settings": {"rate": 3.0, "sync": "Free", "shape": "Triangle"}}, "lfo settings")
    check(set_lfo["settings"]["shape"] == 1 and set_lfo["settings"]["sync"] == 0 and abs(set_lfo["settings"]["rate"] - 3) < 1e-6,
          "an LFO's choices are set by their names")
    need({"cmd": "modulator", "track": "Fx", "action": "modulate", "id": lfo["id"], "nodeId": utility["id"],
          "parameter": "utilitaire.pan", "amount": 0.3}, "lfo on pan")
    values = []
    for _ in range(8):
        time.sleep(0.07)
        found = [m for m in need({"cmd": "modulator", "track": "Fx", "action": "list"}, "list")["modulators"] if m["id"] == lfo["id"]]
        if found and "value" in found[0]:
            values.append(found[0]["value"])
    check(len(values) >= 4 and max(values) - min(values) > 0.05, f"an LFO's value moves while it runs ({values[:4]})")

    # By hand: the LFO's arrow dragged onto the width knob; then armed (a
    # click on it), a drag up on the knob sets how much, not the knob.
    need({"cmd": "set_view", "mode": 3}, "devices")
    need({"cmd": "modulator", "track": "Fx", "action": "column", "nodeId": utility["id"], "open": True}, "column")
    need({"cmd": "ui_spots", "record": True}, "record")
    time.sleep(0.4)
    seen = spots()
    arrow = seen.get(f"modulator-arrow:{lfo['id']}")
    width_binding = f"node.{utility['id']}.utilitaire.width"
    knob = seen.get(f"knob:{width_binding}")
    check(arrow is not None and knob is not None, "the LFO's arrow and the width knob are drawn")
    if arrow and knob:
        events({"type": "move", "x": arrow["x"], "y": arrow["y"]}, {"type": "down"},
               {"type": "move", "x": arrow["x"] + 12, "y": arrow["y"]}, {"type": "wait", "frames": 2},
               {"type": "move", "x": knob["x"], "y": knob["y"] - 4}, {"type": "wait", "frames": 3}, {"type": "up"})
        lfo_now = next(m for m in need({"cmd": "modulator", "track": "Fx", "action": "list"}, "list")["modulators"]
                       if m["id"] == lfo["id"])
        dropped = [move for move in lfo_now["modulations"] if move["binding"] == width_binding]
        check(len(dropped) == 1 and abs(dropped[0]["amount"] - 0.25) < 1e-6,
              "dragged onto a knob, its arrow modulates it by a quarter")
        events({"type": "move", "x": arrow["x"], "y": arrow["y"]}, {"type": "down"}, {"type": "up"})
        events({"type": "move", "x": knob["x"], "y": knob["y"] - 4}, {"type": "down"},
               *[{"type": "move", "x": knob["x"], "y": knob["y"] - 4 - step * 8} for step in range(1, 6)],
               {"type": "wait", "frames": 2}, {"type": "up"})
        lfo_now = next(m for m in need({"cmd": "modulator", "track": "Fx", "action": "list"}, "list")["modulators"]
                       if m["id"] == lfo["id"])
        amount = next((move["amount"] for move in lfo_now["modulations"] if move["binding"] == width_binding), 0.0)
        width = next(v["value"] for v in need({"cmd": "value_list", "track": "Fx", "nodeId": utility["id"], "filter": "width",
                                               "open": False}, "values")["values"] if v["binding"] == width_binding)
        check(amount > 0.32 and abs(width - 1.0) < 1e-6,
              f"armed, a drag on the knob sets the amount ({amount:.3f}), the knob stays ({width})")
        events({"type": "move", "x": arrow["x"], "y": arrow["y"]}, {"type": "down"}, {"type": "up"})
    need({"cmd": "ui_spots", "record": False}, "stop recording")

    # An envelope on an instrument's knob.
    need({"cmd": "add_midi_track", "name": "Synth", "instrument": "opale-instrument"}, "synth track")
    synth = card_of("Synth", "opale")
    cutoff = next(v for v in need({"cmd": "value_list", "track": "Synth", "nodeId": synth["id"], "filter": "cutoff", "open": False},
                                  "values")["values"])
    adsr = need({"cmd": "modulator", "track": "Synth", "action": "add", "nodeId": synth["id"], "type": "adsr"}, "add adsr")
    enveloped = need({"cmd": "modulator", "track": "Synth", "action": "modulate", "id": adsr["id"], "binding": cutoff["binding"],
                      "amount": 0.4}, "adsr on cutoff")
    check(len(enveloped["modulations"]) == 1 and enveloped["modulations"][0]["binding"] == cutoff["binding"],
          f"an envelope modulates an instrument's knob ({cutoff['binding']})")

    # Armed, its column open, for the eye.
    need({"cmd": "modulator", "track": "Fx", "action": "column", "nodeId": utility["id"], "open": True}, "column")
    need({"cmd": "modulator", "track": "Fx", "action": "arm", "id": lfo["id"]}, "arm")
    need({"cmd": "set_view", "mode": 3}, "devices")
    time.sleep(0.3)
    shot = scratch / "modulators.png"
    need({"cmd": "snapshot", "path": str(shot).replace(os.sep, "/")}, "snapshot")
    check(shot.is_file() and shot.stat().st_size > 10_000, f"the strip with its modulators is drawn ({shot})")
    need({"cmd": "modulator", "track": "Fx", "action": "arm", "id": 0}, "disarm")

    # Saved, opened again.
    project = scratch / "Modulated.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    for _ in range(50):
        if need({"cmd": "status"}, "status").get("projectOpening") is not True:
            break
        time.sleep(0.2)
    utility = card_of("Fx", "utilitaire")
    again = need({"cmd": "modulator", "track": "Fx", "action": "list", "nodeId": utility["id"]}, "list again")["modulators"]
    types = sorted(m["type"] for m in again)
    amounts = sorted(round(move["amount"], 3) for m in again for move in m["modulations"])
    check(types == ["mod-lfo", "mod-macro"] and 0.3 in amounts and 0.5 in amounts and len(amounts) == 3,
          f"saved and opened again, its modulators and their amounts are there ({types}, {amounts})")
    reopened = abs(output_db("Fx") - half) < 1.5
    check(reopened, "and the sound is modulated as before")

    # Off a knob, then removed.
    macro_id = next(m["id"] for m in again if m["type"] == "mod-macro")
    off = need({"cmd": "modulator", "track": "Fx", "action": "unmodulate", "id": macro_id, "nodeId": utility["id"],
                "parameter": "utilitaire.gain"}, "unmodulate")
    check(off["modulations"] == [] and abs(output_db("Fx") - base) < 1.5, "taken off the knob, the gain is its own again")
    need({"cmd": "modulator", "track": "Fx", "action": "remove", "id": macro_id}, "remove")
    left = need({"cmd": "modulator", "track": "Fx", "action": "list", "nodeId": utility["id"]}, "list after")["modulators"]
    check([m["type"] for m in left] == ["mod-lfo"], "removed, it is gone; the other stays")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-modulators-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {"cache": str(scratch / "Cache")}}),
                         encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent", "--window-size=1800x1100"],
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
