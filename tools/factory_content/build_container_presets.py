"""The factory presets of the containers (studio_presets/containers.py), written by
CyDAW itself into content/factory/Presets/device/<container>.

For each preset: a fresh container on a track that takes it (an effect
container on a bus track, an instrument container as an instrument track's
instrument), its branches filled with their devices (each set from one of its
own factory presets), named, its settings set through the control port, saved
as a device preset of the container's card (which keeps all it holds). A
silent session (the speakers silenced) in a folder of its own; nothing is
played.

    python tools/factory_content/build_container_presets.py [--exe ...] [--only fx-layer,drum-rack,...]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from studio_presets import containers

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
TARGET = ROOT / "content" / "factory" / "Presets" / "device"
port = 47865
failures = 0


def request(command: dict, timeout: float = 120.0) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(timeout)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(1 << 20)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode("utf-8"))


def need(command: dict, what: str) -> dict:
    global failures
    answer = request(command)
    if not answer.get("ok"):
        failures += 1
        raise RuntimeError(f"{what}: {answer.get('error')}")
    return answer.get("result", {})


def effect_preset(kind: str, name: str, spec: dict) -> None:
    """An effect container on the bus track "Fx", filled, saved, removed."""
    made = need({"cmd": "container", "track": "Fx", "action": "insert", "type": kind}, f"{kind} insert")
    head = made["head"]
    try:
        branches = spec.get("branches", [])
        count = len(made["branches"])
        while kind in containers.VARIABLE and count < len(branches):
            count = len(need({"cmd": "container", "track": "Fx", "action": "add_branch", "head": head}, "add branch")["branches"])
        for index, (label, devices) in enumerate(branches):
            if label:
                need({"cmd": "container", "track": "Fx", "action": "rename", "head": head, "branch": index, "name": label}, "rename")
            for key, preset, values in devices:
                added = need({"cmd": "container", "track": "Fx", "action": "add_device", "head": head, "branch": index, "key": key},
                             f"{key} into branch {index}")
                card = added["branches"][index][-1]["id"]
                if preset:
                    need({"cmd": "presets", "track": "Fx", "nodeId": card, "action": "load", "name": preset}, f"{key} / {preset}")
                for suffix, value in values.items():
                    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": card, "parameter": suffix, "value": value}, suffix)
        for setting, value in spec.get("head", {}).items():
            need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": head, "parameter": f"{containers.PREFIX[kind]}.{setting}",
                  "value": value}, setting)
        need({"cmd": "presets", "track": "Fx", "nodeId": head, "action": "save", "name": name}, "save")
        print(f"ok   {kind} / {name}", flush=True)
    finally:
        request({"cmd": "container", "track": "Fx", "action": "remove", "head": head})


def instrument_preset(card_type: str, name: str, spec: dict) -> None:
    """An instrument container on a new instrument track, its pads or layers made, saved, the track removed."""
    track = need({"cmd": "add_midi_track", "name": "Holder", "instrument": containers.INSTRUMENT_OF[card_type]}, "track")["name"]
    try:
        # Its first pads or layers go: the preset's take their place.
        for _ in range(len(need({"cmd": "rack", "track": track}, "list")["pads"])):
            need({"cmd": "rack", "track": track, "action": "remove", "pad": 0}, "remove pad")
        for item in spec["pads"]:
            mix = dict(item["mix"])
            note = int(mix.pop("note", 0))
            added = need({"cmd": "rack", "track": track, "action": "add", "note": note, "instrument": item["instrument"]},
                         f"{item['instrument']}")
            pad = len(added["pads"]) - 1
            if item["preset"]:
                cards = added["pads"][pad]["cards"]
                card = next((c["id"] for c in cards if c["type"].endswith("-synth")), None)
                if card is None:
                    raise RuntimeError(f"no instrument card on {item['instrument']}")
                need({"cmd": "presets", "track": track, "pad": pad, "nodeId": card, "action": "load", "name": item["preset"]},
                     f"{item['instrument']} / {item['preset']}")
            need({"cmd": "rack", "track": track, "action": "mix", "pad": pad, "name": item["name"], **mix}, "mix")
        graph = need({"cmd": "get_track_graph", "track": track}, "graph")["nodes"]
        head = next(n["id"] for n in graph if n["type"] == card_type)
        for setting, value in spec.get("selector", {}).items():
            need({"cmd": "set_node_parameter", "track": track, "nodeId": head, "parameter": f"selector.{setting}", "value": value},
                 setting)
        need({"cmd": "presets", "track": track, "nodeId": head, "action": "save", "name": name}, "save")
        print(f"ok   {card_type} / {name}", flush=True)
    finally:
        request({"cmd": "remove_track", "track": track})


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(DEFAULT_EXE))
    parser.add_argument("--port", type=int, default=port)
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    port = args.port
    only = {key for key in args.only.split(",") if key}
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-container-presets-"))
    presets = scratch / "Presets"
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {"presets": str(presets),
                                                                                          "cache": str(scratch / "Cache")}}),
                         encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([args.exe, f"--control-port={port}", "--silent", "--window-size=1400x900"], env=environment,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    kinds = [kind for kind in [*containers.EFFECTS, *containers.INSTRUMENTS] if not only or kind in only]
    try:
        for _ in range(300):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        if not request({"cmd": "status"})["result"].get("speakersSilenced"):
            print("the speakers are not silenced: stopping")
            return 1
        request({"cmd": "set_master_gain", "gainDb": -60, "muted": True})
        request({"cmd": "new_project"})
        request({"cmd": "add_bus_track", "name": "Fx"})
        for kind in kinds:
            for name, spec in containers.EFFECTS.get(kind, {}).items():
                try:
                    effect_preset(kind, name, spec)
                except RuntimeError as error:
                    print(f"FAIL {kind} / {name}: {error}", flush=True)
            for name, spec in containers.INSTRUMENTS.get(kind, {}).items():
                try:
                    instrument_preset(kind, name, spec)
                except RuntimeError as error:
                    print(f"FAIL {kind} / {name}: {error}", flush=True)
        for kind in kinds:
            target = TARGET / kind
            target.mkdir(parents=True, exist_ok=True)
            for old in target.glob("*.cypreset"):
                old.unlink()
            written = sorted((presets / "device" / kind).glob("*.cypreset"))
            for file in written:
                # Without the date: the same settings make the same files;
                # compact (a kit holds eight instruments' graphs).
                document = json.loads(file.read_text(encoding="utf-8"))
                document.pop("created", None)
                (target / file.name).write_text(json.dumps(document, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
            print(f"{len(written)} presets in {target}")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=20)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
