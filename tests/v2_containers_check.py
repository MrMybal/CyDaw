"""Containers in the software. Silent (the master muted at -60 dB).

    python tests/v2_containers_check.py --exe "<CyDAW 2.exe>" [--port 47859]

The chain's sound is measured as an exported plug-in gets it (effect_chain_check:
a sine through it offline).

* An FX Layer of two empty layers doubles the sound; a layer muted, as it was;
  a utility at -12 dB in the other layer, -12 dB.
* A card moved from one layer to the other, by the control port and by a drag
  of its name onto the other layer's end.
* A container inside a container, listed in its branch.
* A Multiband FX-3 with the high band at -36 dB: lows pass, highs are gone.
* An XY FX's Post FX follows the corners' mix, before Mix; saved and opened
  again, it is there.
* A Replacer plays an instrument (a kick, then a synth) at each click; its
  instrument's chain opens as a pad's; saved and opened again, it plays.
* The strip with a container open (a snapshot, for the eye).
* Saved and opened again, the containers and their cards are there.
* Removed, the sound is the dry sound again.

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
port = 47859


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


def level(hz: float = 220.0) -> float:
    return need({"cmd": "effect_chain_check", "track": "Fx", "hz": hz, "amplitude": 0.3, "seconds": 0.5}, "chain")["outputDb"]


def containers() -> list[dict]:
    return need({"cmd": "container", "track": "Fx", "action": "list"}, "list")["containers"]


def events(*items: dict) -> None:
    need({"cmd": "ui_input", "events": list(items) + [{"type": "wait", "frames": 3}]}, "input")


def scenario(scratch: Path) -> None:
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "add_bus_track", "name": "Fx"}, "track")
    dry = level()

    # An FX Layer: two empty layers, the sound twice.
    layer = need({"cmd": "container", "track": "Fx", "action": "insert", "type": "fx-layer"}, "insert")
    head = layer["head"]
    check(layer["type"] == "fx-layer" and len(layer["branches"]) == 2 and layer["end"] != 0,
          "an FX Layer goes into the chain with its two layers")
    check(abs(level() - dry - 6.02) < 0.3, f"two empty layers double the sound ({level() - dry:+.2f} dB)")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": head, "parameter": "fxLayer.mute2", "value": 1}, "mute")
    check(abs(level() - dry) < 0.3, "a layer muted: the sound as it was")
    added = need({"cmd": "container", "track": "Fx", "action": "add_device", "head": head, "branch": 0, "key": "utilitaire"},
                 "add device")
    utility = added["branches"][0][0]["id"]
    check(added["branches"][0][0]["type"] == "utilitaire", "a utility goes into the first layer")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": utility, "parameter": "utilitaire.gain", "value": -12}, "gain")
    check(abs(level() - dry + 12.0) < 0.3, f"the layer's utility at -12 dB ({level() - dry:+.2f} dB)")

    # Moved to the other layer.
    moved = need({"cmd": "container", "track": "Fx", "action": "move", "nodeId": utility, "head": head, "branch": 1}, "move")
    check(moved["branches"][0] == [] and [c["id"] for c in moved["branches"][1]] == [utility],
          "moved by the control port into the other layer")
    check(abs(level() - dry) < 0.3, "the first layer clean again (the second, muted, holds it)")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": head, "parameter": "fxLayer.mute2", "value": 0}, "unmute")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": head, "parameter": "fxLayer.mute1", "value": 1}, "mute 1")
    check(abs(level() - dry + 12.0) < 0.3, "the second layer plays it")

    # By hand: a utility of the main chain dragged by its name onto the end
    # of the first layer.
    need({"cmd": "insert_device", "track": "Fx", "key": "utilitaire"}, "main utility")
    main = next(n["id"] for n in need({"cmd": "get_track_graph", "track": "Fx"}, "graph")["nodes"]
                if n["type"] == "utilitaire" and n["id"] != utility)
    need({"cmd": "set_view", "mode": 3}, "devices")
    need({"cmd": "container", "track": "Fx", "action": "show", "head": head, "branch": 0}, "show")
    need({"cmd": "ui_spots", "record": True}, "record")
    time.sleep(0.4)
    spots = {s["key"]: s for s in need({"cmd": "ui_spots"}, "spots")["spots"]}
    grip = spots.get(f"card:{main}")
    end = spots.get(f"branch-end:{head}:0")
    check(grip is not None and end is not None, "the card's name and the layer's end are drawn")
    if grip and end:
        events({"type": "move", "x": grip["x"], "y": grip["y"]}, {"type": "down"},
               {"type": "move", "x": grip["x"] + 14, "y": grip["y"]}, {"type": "wait", "frames": 2},
               {"type": "move", "x": end["x"], "y": end["y"]}, {"type": "wait", "frames": 3}, {"type": "up"})
        inside = next(c for c in containers() if c["head"] == head)
        check([c["id"] for c in inside["branches"][0]] == [main], "dragged by its name onto the layer's end, it is in the layer")
    need({"cmd": "ui_spots", "record": False}, "stop recording")

    # A container in a container.
    nested = need({"cmd": "container", "track": "Fx", "action": "add_device", "head": head, "branch": 0, "key": "mid-side-split"},
                  "nested")
    types = [c["type"] for c in nested["branches"][0]]
    check("mid-side-split" in types and len(containers()) == 2, f"a container inside a layer ({types})")

    # Snapshot, for the eye.
    time.sleep(0.3)
    shot = scratch / "containers.png"
    need({"cmd": "snapshot", "path": str(shot).replace(os.sep, "/")}, "snapshot")
    check(shot.is_file() and shot.stat().st_size > 10_000, f"the strip with its container is drawn ({shot})")

    # Saved and opened again.
    project = scratch / "Contained.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    before = sorted((c["type"], tuple(tuple(card["type"] for card in b) for b in c["branches"])) for c in containers())
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    after = sorted((c["type"], tuple(tuple(card["type"] for card in b) for b in c["branches"])) for c in containers())
    check(before == after and len(after) == 2, f"saved and opened again, the containers are there ({after})")

    # Removed: the dry sound.
    head = next(c["head"] for c in containers() if c["type"] == "fx-layer")
    need({"cmd": "container", "track": "Fx", "action": "remove", "head": head}, "remove")
    remaining = [n["type"] for n in need({"cmd": "get_track_graph", "track": "Fx"}, "graph")["nodes"]]
    check(containers() == [] and "utilitaire" not in remaining and "container-end" not in remaining,
          "removed, the container goes with all it held")
    check(abs(level() - dry) < 0.3, "and the sound is the dry sound")

    # A Multiband FX-3: the high band at -60 dB.
    bands = need({"cmd": "container", "track": "Fx", "action": "insert", "type": "multiband-3"}, "multiband")
    high = need({"cmd": "container", "track": "Fx", "action": "add_device", "head": bands["head"], "branch": 2, "key": "utilitaire"},
                "high utility")["branches"][2][0]["id"]
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": high, "parameter": "utilitaire.gain", "value": -36}, "gain")
    check(abs(level(120.0) - dry) < 0.5, f"the lows pass ({level(120.0) - dry:+.2f} dB)")
    check(level(9_000.0) < dry - 25.0, f"the highs are turned down ({level(9_000.0) - dry:+.2f} dB)")
    need({"cmd": "container", "track": "Fx", "action": "remove", "head": bands["head"]}, "remove bands")

    # XY FX and its Post FX: the corners' mix, then the post chain, then Mix.
    xy = need({"cmd": "container", "track": "Fx", "action": "insert", "type": "xy-fx"}, "xy")
    check(len(xy["branches"]) == 5 and xy["postBranch"] == 4 and xy["blend"] != 0,
          "an XY FX comes with its four corners and its Post FX")
    check(abs(level() - dry) < 0.3, f"its corners, empty, give the sound back ({level() - dry:+.2f} dB)")
    post = need({"cmd": "container", "track": "Fx", "action": "add_device", "head": xy["head"], "branch": 4, "key": "utilitaire"},
                "post utility")
    post_utility = post["branches"][4][0]["id"]
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": post_utility, "parameter": "utilitaire.gain", "value": -12}, "gain")
    check(abs(level() - dry + 12.0) < 0.3, f"a utility in its Post FX at -12 dB ({level() - dry:+.2f} dB)")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": xy["head"], "parameter": "xyFx.mix", "value": 0}, "mix")
    check(abs(level() - dry) < 0.3, "Mix 0: the dry sound, the Post FX left out with the corners")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": xy["head"], "parameter": "xyFx.mix", "value": 1}, "mix back")
    need({"cmd": "container", "track": "Fx", "action": "show", "head": xy["head"], "branch": 4}, "show post")
    time.sleep(0.3)
    shot = scratch / "xy-post.png"
    need({"cmd": "snapshot", "path": str(shot).replace(os.sep, "/")}, "snapshot")
    check(shot.is_file() and shot.stat().st_size > 10_000, f"the XY FX with its Post FX shown is drawn ({shot})")
    again = scratch / "Xy.cydaw"
    need({"cmd": "save_project", "path": str(again).replace(os.sep, "/")}, "save xy")
    need({"cmd": "open_project", "path": str(again).replace(os.sep, "/")}, "open xy")
    reopened = next(c for c in containers() if c["type"] == "xy-fx")
    check(reopened["postBranch"] == 4 and [c["type"] for c in reopened["branches"][4]] == ["utilitaire"]
          and abs(level() - dry + 12.0) < 0.3, "saved and opened again, its Post FX and its sound are there")

    # A Replacer playing an instrument at each transient (the graph as the
    # track plays it: the instrument goes with it, not with an export).
    need({"cmd": "add_bus_track", "name": "Drums"}, "drums track")

    def clicks() -> float:
        return need({"cmd": "effect_chain_check", "track": "Drums", "live": True, "signal": "clicks", "amplitude": 0.8,
                     "seconds": 1.5}, "clicks")["outputDb"]

    bare = clicks()
    replacer = need({"cmd": "container", "track": "Drums", "action": "insert", "type": "replacer"}, "replacer")["head"]
    kick = need({"cmd": "container", "track": "Drums", "action": "instrument", "head": replacer, "key": "kick-instrument"},
                "kick")
    check(kick["instrument"] != "", f"a Replacer takes an instrument ({kick['instrument']})")
    kicked = clicks()
    check(kicked > bare + 20.0, f"the instrument plays at each click ({bare:.1f} dB of clicks, {kicked:.1f} dB replaced)")
    need({"cmd": "set_node_parameter", "track": "Drums", "nodeId": replacer, "parameter": "replacer.mix", "value": 0}, "mix")
    check(abs(clicks() - bare) < 1.0, "Mix 0: the clicks alone")
    need({"cmd": "set_node_parameter", "track": "Drums", "nodeId": replacer, "parameter": "replacer.mix", "value": 1}, "mix back")
    opened = need({"cmd": "container", "track": "Drums", "action": "open_instrument", "head": replacer}, "open")
    check(opened["pad"] >= 1000, "its instrument's chain opens in the strip, as a pad's")
    time.sleep(0.3)
    shot = scratch / "replacer-instrument.png"
    need({"cmd": "snapshot", "path": str(shot).replace(os.sep, "/")}, "snapshot")
    check(shot.is_file() and shot.stat().st_size > 10_000, f"the Replacer's instrument is drawn ({shot})")
    need({"cmd": "container", "track": "Drums", "action": "open_instrument", "head": replacer, "open": False}, "close")
    synth = need({"cmd": "container", "track": "Drums", "action": "instrument", "head": replacer, "key": "opale-instrument"},
                 "synth")
    switched = clicks()
    check(synth["instrument"] != kick["instrument"] and switched > bare + 10.0,
          f"another instrument ({synth['instrument']}) plays in its place ({switched:.1f} dB)")
    saved = scratch / "Replaced.cydaw"
    need({"cmd": "save_project", "path": str(saved).replace(os.sep, "/")}, "save replacer")
    need({"cmd": "open_project", "path": str(saved).replace(os.sep, "/")}, "open replacer")
    after = next(c for c in need({"cmd": "container", "track": "Drums", "action": "list"}, "list")["containers"]
                 if c["type"] == "replacer")
    reloaded = clicks()
    check(after["instrument"] == synth["instrument"] and reloaded > bare + 10.0,
          f"saved and opened again, the Replacer keeps its instrument and plays it ({reloaded:.1f} dB)")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-containers-check-"))
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
