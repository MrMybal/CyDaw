"""Instrument containers, branch names, container presets and plug-in value lists in the software.
Silent (the speakers silenced, the master muted at -60 dB between exports).

    python tests/v2_instrument_containers_check.py --exe "<CyDAW 2.exe>" [--port 47863]

The presets folder is the test's own (CYDAW_LOCATIONS_FILE). An instrument track's sound is the mix
exported to a file (offline, the speakers silenced); an effect chain's, a sine through it as an
exported plug-in gets it (effect_chain_check).

* An Instrument Layer starts with two layers; both play a note, a muted one is quiet, a layer
  whose keys or velocities leave the note out does not play it.
* An Instrument Selector starts with three layers; by hand it plays the one chosen, in turn one
  layer a note, by key switch the one a key chose (the switch itself silent); it says which.
* A Drum Machine takes a pad on any key, more than sixteen.
* What a pad, a layer or a Replacer plays: CyDAW's instruments and the factory's, named in
  English (a Replacer's instrument too).
* A branch named by hand keeps its name when the branches before it go, and in a saved project.
* A container's preset brings all it held back (its cards, their settings, its branches' names);
  an instrument container's, its layers; the factory's do too (a layer of effects, a kit of eight pads).
* A plug-in card opens its list of values.
* Saved and opened again: the layers, their keys and velocities, the selector's settings.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47863
ROOT = Path(__file__).resolve().parents[1]


def request(command: dict) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(180)
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


def read_wave(path: Path) -> tuple[list[float], int]:
    """The left channel of a WAV file, -1..1."""
    with wave.open(str(path), "rb") as file:
        rate, width, channels = file.getframerate(), file.getsampwidth(), file.getnchannels()
        raw = file.readframes(file.getnframes())
    step = width * channels
    samples = []
    for at in range(0, len(raw) - step + 1, step):
        frame = raw[at:at + width]
        if width == 3:
            samples.append(int.from_bytes(frame + (b"\xff" if frame[2] & 0x80 else b"\x00"), "little", signed=True) / 8_388_608.0)
        elif width == 4:
            samples.append(struct.unpack("<f", frame)[0])
        else:
            samples.append(int.from_bytes(frame, "little", signed=True) / 32_768.0)
    return samples, rate


def rms(samples: list[float], rate: int, start: float, end: float) -> float:
    part = samples[int(start * rate):int(end * rate)]
    return math.sqrt(sum(value * value for value in part) / max(1, len(part)))


def hits(samples: list[float], rate: int, starts: list[float], window: float = 0.2, floor: float = 0.003) -> list[bool]:
    """Whether something sounds right after each start (seconds)."""
    return [rms(samples, rate, start, start + window) > floor for start in starts]


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-instrument-containers-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1500x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(300):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        if not need({"cmd": "status"}, "status").get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
        need({"cmd": "set_tempo", "bpm": 120}, "tempo")
        scenario(scratch)
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


def scenario(scratch: Path) -> None:
    def export(name: str) -> tuple[list[float], int]:
        """The mix rendered to a file (the master at 0 dB for it: the speakers stay silenced)."""
        need({"cmd": "set_master_gain", "gainDb": 0.0, "muted": False}, "master for the export")
        path = scratch / f"{name}.wav"
        need({"cmd": "export_mix", "path": str(path)}, f"export {name}")
        for _ in range(1_200):
            time.sleep(0.1)
            if not need({"cmd": "status"}, "status").get("exporting"):
                break
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master")
        return read_wave(path)

    def rack(track: str, **fields) -> dict:
        return need({"cmd": "rack", "track": track, **fields}, f"rack {fields.get('action', 'list')}")

    def notes(track: str, played: list[dict]) -> None:
        need({"cmd": "clear_notes", "track": track, "clip": "notes"}, "clear")
        need({"cmd": "add_notes", "track": track, "clip": "notes", "notes": played}, "notes")

    def selector(track: str, key: str, value: float) -> None:
        card = next(n["id"] for n in need({"cmd": "get_track_graph", "track": track}, "graph")["nodes"]
                    if n["type"] == "instrument-selector")
        need({"cmd": "set_node_parameter", "track": track, "nodeId": card, "parameter": f"selector.{key}", "value": value},
             f"selector {key}")

    # ---- What a pad, a layer or a Replacer plays ----
    choices = need({"cmd": "instrument_choices"}, "instrument choices")["instruments"]
    own = [c for c in choices if not c["category"]]
    factory = [c for c in choices if c["category"]]
    check(any(c["key"] == "opale-instrument" for c in own) and not any(c["key"] == "rack-instrument" for c in own),
          f"CyDAW's instruments are offered, no container among them ({len(own)})")
    check(len(factory) > 20 and all(c["key"].startswith("factory:") for c in factory),
          f"the factory's sampled instruments too, by category ({len(factory)})")
    names = {c["key"]: c["name"] for c in own}
    check(names.get("simple-sampler-instrument") == "Simple sampler" and names.get("kick-instrument") == "Kick (kick drum synth)",
          f"their names are English ({names.get('simple-sampler-instrument')}, {names.get('kick-instrument')})")

    # ---- An Instrument Layer ----
    need({"cmd": "new_project"}, "new")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    layer = need({"cmd": "add_midi_track", "name": "Layer", "instrument": "layer-instrument"}, "layer track")["name"]
    listed = rack(layer)
    check(listed.get("kind") == "Instrument Layer" and len(listed["pads"]) == 2,
          f"an Instrument Layer starts with two layers ({[p['name'] for p in listed.get('pads', [])]})")
    for pad in (0, 1):
        rack(layer, action="instrument", pad=pad, instrument="kick-instrument")
    need({"cmd": "add_midi_clip", "track": layer, "name": "notes", "startBeats": 0, "lengthBeats": 4}, "clip")
    starts = [0.0, 0.5, 1.0, 1.5]
    notes(layer, [{"note": 36, "start": b, "length": 0.25, "velocity": 0.9} for b in range(4)])
    samples, rate = export("layer_both")
    both = rms(samples, rate, 0.0, 2.0)
    check(both > 0.02, f"both layers play the note (rms {both:.3f})")
    rack(layer, action="mix", pad=1, mute=True)
    samples, rate = export("layer_one")
    one = rms(samples, rate, 0.0, 2.0)
    check(0.003 < one < both * 0.8, f"a muted layer is quiet, the other plays ({both:.3f} -> {one:.3f})")
    rack(layer, action="mix", pad=0, keyLow=48)
    samples, rate = export("layer_keys")
    check(rms(samples, rate, 0.0, 2.0) < 0.002, "a layer whose keys leave the note out does not play it")
    rack(layer, action="mix", pad=0, keyLow=0, velocityLow=120)
    samples, rate = export("layer_velocity")
    check(rms(samples, rate, 0.0, 2.0) < 0.002, "nor one whose velocities leave it out")
    rack(layer, action="mix", pad=0, velocityLow=1, velocityHigh=127, keyHigh=100)
    check([(p["keyLow"], p["keyHigh"], p["velocityLow"]) for p in rack(layer)["pads"]][0] == (0, 100, 1),
          "a layer's keys and velocities are kept")

    # ---- An Instrument Selector ----
    chooser = need({"cmd": "add_midi_track", "name": "Selector", "instrument": "selector-instrument"}, "selector track")["name"]
    listed = rack(chooser)
    check(listed.get("kind") == "Instrument Selector" and len(listed["pads"]) == 3,
          f"an Instrument Selector starts with three layers ({[p['name'] for p in listed.get('pads', [])]})")
    for pad in (0, 1, 2):
        rack(chooser, action="instrument", pad=pad, instrument="kick-instrument")
    # Only the second layer is heard.
    rack(chooser, action="mix", pad=0, mute=True)
    rack(chooser, action="mix", pad=2, mute=True)
    need({"cmd": "set_track", "track": layer, "muted": True}, "layer muted")
    need({"cmd": "add_midi_clip", "track": chooser, "name": "notes", "startBeats": 0, "lengthBeats": 4}, "clip")
    notes(chooser, [{"note": 36, "start": b, "length": 0.25, "velocity": 0.9} for b in range(4)])
    selector(chooser, "mode", 0)
    selector(chooser, "layer", 1)
    samples, rate = export("selector_first")
    check(rms(samples, rate, 0.0, 2.0) < 0.002, "by hand on the first layer (muted): nothing")
    selector(chooser, "layer", 2)
    samples, rate = export("selector_second")
    check(rms(samples, rate, 0.0, 2.0) > 0.01, "by hand on the second: it plays")
    check(rack(chooser).get("selected") == 1, "it says it plays the second")
    selector(chooser, "mode", 1)
    samples, rate = export("selector_turns")
    heard = hits(samples, rate, starts)
    check(heard.count(True) in (1, 2), f"in turn, one layer a note: the second plays one note in three ({heard})")
    selector(chooser, "mode", 3)
    selector(chooser, "keyswitch", 24)
    notes(chooser, [{"note": 25, "start": 0, "length": 0.1, "velocity": 0.9},
                    {"note": 36, "start": 0.5, "length": 0.25, "velocity": 0.9},
                    {"note": 24, "start": 2, "length": 0.1, "velocity": 0.9},
                    {"note": 36, "start": 2.5, "length": 0.25, "velocity": 0.9}])
    samples, rate = export("selector_switch")
    heard = hits(samples, rate, [0.0, 0.25, 1.0, 1.25], window=0.2)
    check(heard == [False, True, False, False], f"by key switch: the second plays after its key, not after the first's ({heard})")

    # ---- A Drum Machine ----
    drums = need({"cmd": "add_midi_track", "name": "Drums", "instrument": "rack-instrument"}, "drums track")["name"]
    need({"cmd": "set_track", "track": chooser, "muted": True}, "selector muted")
    listed = rack(drums)
    check(listed.get("kind") == "Drum Machine" and [p["note"] for p in listed["pads"]] == [36, 38, 39, 42, 46],
          "a Drum Machine starts with its kit")
    rack(drums, action="add", note=100, instrument="kick-instrument")
    for note in range(60, 72):
        rack(drums, action="add", note=note, instrument="silex-instrument")
    listed = rack(drums)
    check(len(listed["pads"]) == 18 and any(p["note"] == 100 for p in listed["pads"]),
          f"it takes a pad on any key, more than sixteen ({len(listed['pads'])})")
    need({"cmd": "add_midi_clip", "track": drums, "name": "notes", "startBeats": 0, "lengthBeats": 4}, "clip")
    notes(drums, [{"note": 100, "start": b, "length": 0.25, "velocity": 0.9} for b in range(4)])
    samples, rate = export("drums_high")
    check(rms(samples, rate, 0.0, 2.0) > 0.01, "a pad on key 100 plays")

    # A factory instrument on a layer (one whose samples are here).
    ready = [c for c in factory if not c["missing"]]
    if ready:
        chosen = ready[0]
        rack(layer, action="instrument", pad=1, instrument=chosen["key"])
        pad = rack(layer)["pads"][1]
        check(pad["name"] == chosen["name"] and "sampler" in pad["devices"],
              f"a factory instrument on a layer, named in English ({pad['name']})")
    else:
        print("skip a factory instrument on a layer: none downloaded here")

    # ---- Saved and opened again ----
    project = scratch / "Containers.cydaw"
    need({"cmd": "save_project", "path": str(project)}, "save")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "open_project", "path": str(project)}, "open")
    kept = {}
    for _ in range(150):
        answer = request({"cmd": "rack", "track": "Selector"})
        if answer.get("ok") and answer["result"].get("pads"):
            kept = answer["result"]
            break
        time.sleep(0.2)
    check(kept.get("kind") == "Instrument Selector" and kept.get("padsKind") == "selector" and len(kept.get("pads", [])) == 3,
          "a saved project keeps the selector's layers")
    card = next(n for n in need({"cmd": "get_track_graph", "track": "Selector"}, "graph")["nodes"] if n["type"] == "instrument-selector")
    values = {p["key"].split(".")[-1]: p["value"] for p in card.get("parameters", [])}
    check(values.get("mode") == 3 and values.get("keyswitch") == 24, f"and its way of choosing ({values})")
    kept_layer = rack("Layer")
    check(kept_layer.get("padsKind") == "layer" and kept_layer["pads"][0]["keyHigh"] == 100, "and the layers' keys")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")

    # ---- An instrument container's preset: its layers ----
    layer_card = next(n["id"] for n in need({"cmd": "get_track_graph", "track": "Layer"}, "graph")["nodes"]
                      if n["type"] == "instrument-layer")
    need({"cmd": "presets", "track": "Layer", "nodeId": layer_card, "action": "save", "name": "Two Kicks"}, "save layer preset")
    rack("Layer", action="remove", pad=1)
    check(len(rack("Layer")["pads"]) == 1, "a layer removed")
    need({"cmd": "presets", "track": "Layer", "nodeId": layer_card, "action": "load", "name": "Two Kicks"}, "load layer preset")
    check(len(rack("Layer")["pads"]) == 2, "its preset brings its layers back")

    # ---- Branch names, container presets: on a bus track ----
    need({"cmd": "add_bus_track", "name": "Fx"}, "fx track")
    dry = need({"cmd": "effect_chain_check", "track": "Fx", "hz": 220, "amplitude": 0.3, "seconds": 0.5}, "chain")["outputDb"]
    made = need({"cmd": "container", "track": "Fx", "action": "insert", "type": "fx-layer"}, "fx layer")
    head = made["head"]
    need({"cmd": "container", "track": "Fx", "action": "add_branch", "head": head}, "third layer")
    need({"cmd": "container", "track": "Fx", "action": "rename", "head": head, "branch": 2, "name": "Air"}, "rename")
    named = need({"cmd": "container", "track": "Fx", "action": "rename", "head": head, "branch": 1, "name": "Body"}, "rename")
    check(named["names"] == ["", "Body", "Air"], f"branches named by hand ({named['names']})")
    gone = need({"cmd": "container", "track": "Fx", "action": "remove_branch", "head": head, "branch": 0}, "remove branch")
    check(gone["names"] == ["Body", "Air"], f"the names follow their branches when one before goes ({gone['names']})")
    added = need({"cmd": "container", "track": "Fx", "action": "add_device", "head": head, "branch": 0, "key": "utilitaire"}, "utility")
    utility = added["branches"][0][0]["id"]
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": utility, "parameter": "utilitaire.gain", "value": -12}, "gain")
    need({"cmd": "set_node_parameter", "track": "Fx", "nodeId": head, "parameter": "fxLayer.mute2", "value": 1}, "mute air")
    level = need({"cmd": "effect_chain_check", "track": "Fx", "hz": 220, "amplitude": 0.3, "seconds": 0.5}, "chain")["outputDb"]
    check(abs(level - dry + 12.0) < 0.4, f"the Body layer at -12 dB ({level - dry:+.2f} dB)")
    need({"cmd": "presets", "track": "Fx", "nodeId": head, "action": "save", "name": "Body Minus 12"}, "save container preset")
    need({"cmd": "container", "track": "Fx", "action": "remove_branch", "head": head, "branch": 0}, "remove body")
    need({"cmd": "container", "track": "Fx", "action": "rename", "head": head, "branch": 0, "name": "Other"}, "rename other")
    need({"cmd": "presets", "track": "Fx", "nodeId": head, "action": "load", "name": "Body Minus 12"}, "load container preset")
    back = next(c for c in need({"cmd": "container", "track": "Fx", "action": "list"}, "list")["containers"] if c["head"] == head)
    check(back["names"][:2] == ["Body", "Air"] and back["branches"][0] and back["branches"][0][0]["type"] == "utilitaire",
          f"its preset brings back all it held ({back['names']}, {[[c['type'] for c in b] for b in back['branches']]})")
    level = need({"cmd": "effect_chain_check", "track": "Fx", "hz": 220, "amplitude": 0.3, "seconds": 0.5}, "chain")["outputDb"]
    check(abs(level - dry + 12.0) < 0.4, f"and its sound ({level - dry:+.2f} dB)")
    need({"cmd": "container", "track": "Fx", "action": "list"}, "select the track")
    need({"cmd": "undo", "scope": "graph"}, "undo")
    undone = next(c for c in need({"cmd": "container", "track": "Fx", "action": "list"}, "list")["containers"] if c["head"] == head)
    check(undone["names"][:1] == ["Other"], f"one undo takes the preset back ({undone['names']})")

    # A factory preset of a container: all it holds.
    other = need({"cmd": "container", "track": "Fx", "action": "insert", "type": "fx-layer"}, "another fx layer")["head"]
    need({"cmd": "presets", "track": "Fx", "nodeId": other, "action": "load", "name": "Parallel Saturation"}, "factory container preset")
    loaded = next(c for c in need({"cmd": "container", "track": "Fx", "action": "list"}, "list")["containers"] if c["head"] == other)
    check(loaded["names"] == ["Dry", "Heat"] and [c["type"] for c in loaded["branches"][1]] == ["fournaise"],
          f"a factory container preset brings its layers and devices ({loaded['names']})")
    drum_card = next(n["id"] for n in need({"cmd": "get_track_graph", "track": "Drums"}, "graph")["nodes"] if n["type"] == "drum-rack")
    need({"cmd": "presets", "track": "Drums", "nodeId": drum_card, "action": "load", "name": "Techno Kit"}, "factory kit")
    kit = rack("Drums")["pads"]
    check(len(kit) == 8 and any(p["name"] == "Crash" and p["note"] == 49 for p in kit),
          f"a factory kit brings its eight pads ({[p['name'] for p in kit]})")

    # A Replacer's instrument, named in English.
    replacer = need({"cmd": "container", "track": "Fx", "action": "insert", "type": "replacer"}, "replacer")["head"]
    played = need({"cmd": "container", "track": "Fx", "action": "instrument", "head": replacer, "key": "simple-sampler-instrument"},
                  "replacer instrument")
    check(played.get("instrument") == "Simple sampler", f"a Replacer's instrument is named in English ({played.get('instrument')})")

    # ---- A plug-in card's list of values ----
    plugin = ROOT / "build" / "agent-graph" / "CyDAWTestGain_artefacts" / "Release" / "VST3" / "CyDAW Test Gain.vst3"
    if plugin.exists():
        need({"cmd": "add_plugin", "track": "Fx", "file": str(plugin).replace(os.sep, "/")}, "plug-in")
        need({"cmd": "set_view", "mode": 3}, "devices")
        opened = need({"cmd": "value_list", "track": "Fx", "plugin": 0, "open": True}, "plug-in value list")
        check(opened.get("open") is True, "a plug-in card opens its list of values")
        time.sleep(1.0)
        shot = scratch / "plugin_values.png"
        need({"cmd": "snapshot", "path": str(shot).replace(os.sep, "/")}, "snapshot")
        check(shot.is_file() and shot.stat().st_size > 10_000, "the strip with the plug-in's list is drawn")
    else:
        print("skip the plug-in's list: the test plug-in is not built")


if __name__ == "__main__":
    sys.exit(main())
