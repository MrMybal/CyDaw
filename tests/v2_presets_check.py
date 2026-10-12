"""Presets of a device card, a hosted plug-in and a track's whole chain. Silent, in a presets folder
of the test's own (CYDAW_LOCATIONS_FILE).

    python tests/v2_presets_check.py --exe "<CyDAW 2.exe>" [--plugin "<CyDAW Graph Runtime.vst3>"]
                                     [--programs-plugin "<a VST3 exported with programs>"] [--port 47830]

* A sound-model card's settings (and its model) saved under a name, the file where the folder
  says; a knob turned; the preset loaded puts it back, in one step undone; renamed, removed.
* A track's chain saved; a card taken out; the chain's preset brings it back.
* A plug-in's whole state (the CyDAW Graph Runtime VST3 on a bus): a parameter set, saved, changed,
  the preset loaded sets it again; its own programs come with the list.
* A device's programs in the workshop: kept under names, one loaded puts its settings back, renamed,
  removed; the exported plug-in gets them as its programs (host values in the published order).
* With --programs-plugin (a VST3 exported from CyDAW with programs): the host lists them, one chosen.

Exit code 0 when all hold.
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

failures: list[str] = []
port = 47830
ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests" / "fixtures" / "two-partials.cysound"


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


def nodes(track: str) -> list[dict]:
    return need({"cmd": "get_track_graph", "track": track}, "graph")["nodes"]


def knob(track: str, node: int, suffix: str) -> float:
    for item in nodes(track):
        if item["id"] == node:
            for parameter in item["parameters"]:
                if parameter["key"] == f"node.{node}.{suffix}":
                    return float(parameter["value"])
    return float("nan")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--plugin", default="")
    parser.add_argument("--programs-plugin", default="")
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-presets-"))
    presets = scratch / "Presets"
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1,
                                     "folders": {"presets": str(presets), "cache": str(scratch / "Cache")}}),
                         encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1400x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(200):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
        need({"cmd": "new_project"}, "new")
        track = need({"cmd": "load_sound_model", "file": str(FIXTURE)}, "model")["track"]
        card = next(item["id"] for item in nodes(track) if item["type"] == "sound-model")

        # A card's preset.
        need({"cmd": "set_node_parameter", "track": track, "nodeId": card, "parameter": "model.brightness", "value": 5.0},
             "brightness")
        saved = need({"cmd": "presets", "track": track, "nodeId": card, "action": "save", "name": "Clair"}, "save")
        file = presets / "device" / "sound-model" / "Clair.cypreset"
        check(saved["kind"] == "device" and saved["target"] == "sound-model" and saved["current"] == "Clair"
              and [p["name"] for p in saved["presets"]] == ["Clair"], f"a card's preset saved ({saved.get('presets')})")
        document = json.loads(file.read_text(encoding="utf-8")) if file.exists() else {}
        state = document.get("state", {})
        check(file.exists() and abs(state.get("parameters", {}).get("model.brightness", 0) - 5.0) < 1e-6
              and state.get("soundModel", {}).get("format") == "cydaw-sound-model",
              "it is a file in the presets folder, the knobs and the model in it")
        need({"cmd": "set_node_parameter", "track": track, "nodeId": card, "parameter": "model.brightness", "value": -7.0},
             "brightness")
        time.sleep(0.6)
        need({"cmd": "presets", "track": track, "nodeId": card, "action": "load", "name": "Clair"}, "load")
        check(abs(knob(track, card, "model.brightness") - 5.0) < 1e-6, "the preset puts the knob back")
        time.sleep(0.8)
        need({"cmd": "undo", "scope": "graph", "track": track}, "undo")
        check(abs(knob(track, card, "model.brightness") + 7.0) < 1e-6, "loading it is undone in one step")
        renamed = need({"cmd": "presets", "track": track, "nodeId": card, "action": "rename", "name": "Clair",
                        "newName": "Brillant"}, "rename")
        check([p["name"] for p in renamed["presets"]] == ["Brillant"], "renamed")
        removed = need({"cmd": "presets", "track": track, "nodeId": card, "action": "delete", "name": "Brillant"}, "delete")
        check(removed["presets"] == [], "removed")

        # The whole chain's preset.
        need({"cmd": "presets", "track": track, "action": "save", "name": "Chaine"}, "chain save")
        filter_card = next(item["id"] for item in nodes(track) if item["type"] == "voice-filter")
        need({"cmd": "remove_device", "track": track, "nodeId": filter_card}, "remove a card")
        check("voice-filter" not in [item["type"] for item in nodes(track)], "a card taken out of the chain")
        loaded = need({"cmd": "presets", "track": track, "action": "load", "name": "Chaine"}, "chain load")
        check(loaded["kind"] == "chain" and loaded["target"] == "instrument"
              and "voice-filter" in [item["type"] for item in nodes(track)], "the chain's preset brings it back")

        # The device's programs, in the workshop.
        need({"cmd": "workshop", "action": "open_track", "track": track}, "workshop")
        envelope = next(item["id"] for item in nodes(track) if item["type"] == "envelope")
        need({"cmd": "set_node_parameter", "track": track, "nodeId": envelope, "parameter": "envelope.attack",
              "value": 800.0}, "attack")
        need({"cmd": "workshop", "action": "programs", "op": "add", "name": "Lent"}, "program add")
        need({"cmd": "set_node_parameter", "track": track, "nodeId": envelope, "parameter": "envelope.attack",
              "value": 2.0}, "attack")
        listed = need({"cmd": "workshop", "action": "programs", "op": "add", "name": "Rapide", "runtime": True},
                      "program add")
        key = f"node.{envelope}.envelope.attack"
        check([p["name"] for p in listed["programs"]] == ["Lent", "Rapide"]
              and abs(listed["programs"][0]["values"].get(key, 0) - 800.0) < 1e-6,
              "the device keeps its settings under names")
        runtime = listed.get("runtime") or []
        check(len(runtime) == 2 and [p["name"] for p in runtime] == ["Lent", "Rapide"]
              and runtime[0]["hostValues"] != runtime[1]["hostValues"],
              f"the exported plug-in gets them as its programs ({runtime})")
        need({"cmd": "workshop", "action": "programs", "op": "load", "name": "Lent"}, "program load")
        check(abs(knob(track, envelope, "envelope.attack") - 800.0) < 1e-6, "a program puts its settings back")
        renamed = need({"cmd": "workshop", "action": "programs", "op": "rename", "name": "Lent", "newName": "Doux"},
                       "program rename")
        check([p["name"] for p in renamed["programs"]] == ["Doux", "Rapide"], "a program renamed")
        removed = need({"cmd": "workshop", "action": "programs", "op": "remove", "name": "Rapide"}, "program remove")
        check([p["name"] for p in removed["programs"]] == ["Doux"], "a program removed")
        status = need({"cmd": "workshop", "action": "status"}, "workshop status")
        check(status.get("programs") == ["Doux"], "the workshop shows them")

        # A plug-in's preset.
        if args.plugin and Path(args.plugin).exists():
            need({"cmd": "add_bus_track", "name": "Bus"}, "bus")
            need({"cmd": "add_plugin", "track": "Bus", "file": args.plugin}, "plug-in")
            need({"cmd": "set_plugin_parameter", "track": "Bus", "plugin": 0, "parameter": 0, "value": 0.7}, "set")
            time.sleep(0.5)
            need({"cmd": "presets", "track": "Bus", "plugin": 0, "action": "save", "name": "P1"}, "plug-in save")
            need({"cmd": "set_plugin_parameter", "track": "Bus", "plugin": 0, "parameter": 0, "value": 0.2}, "set")
            time.sleep(0.5)
            answer = need({"cmd": "presets", "track": "Bus", "plugin": 0, "action": "load", "name": "P1"}, "plug-in load")
            time.sleep(0.5)
            value = need({"cmd": "list_plugin_parameters", "track": "Bus", "plugin": 0}, "parameters")["parameters"][0]["value"]
            check(answer["kind"] == "plugin" and abs(value - 0.7) < 0.01,
                  f"a plug-in's preset sets its state again ({value})")
            check(isinstance(answer.get("programs"), list) and "program" in answer,
                  f"its own programs come with the list ({answer.get('programs')})")
        else:
            print("skip the plug-in: none given", flush=True)

        # A VST3 exported from CyDAW with programs: the host lists them.
        if args.programs_plugin and Path(args.programs_plugin).exists():
            need({"cmd": "add_bus_track", "name": "Programmes"}, "bus")
            need({"cmd": "add_plugin", "track": "Programmes", "file": args.programs_plugin}, "plug-in")
            time.sleep(0.5)
            listed = need({"cmd": "presets", "track": "Programmes", "plugin": 0}, "programs")
            names = listed.get("programs", [])
            check(len(names) >= 2, f"an exported plug-in's programs are listed ({names})")
            if len(names) >= 2:
                chosen = need({"cmd": "presets", "track": "Programmes", "plugin": 0, "action": "program", "index": 1},
                              "program")
                check(chosen.get("program") == 1 and chosen.get("current") == names[1],
                      f"one chosen ({chosen.get('program')}, {chosen.get('current')})")
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL " + str(error), flush=True)
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=20)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print("presets check: %d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
