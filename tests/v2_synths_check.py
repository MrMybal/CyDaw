"""The synth family in the software (Ambre, an analog polysynth; Colosse, a big wavetable synth;
Mosaïque, a hybrid one; Nébuleuse, a synth of atmospheres). Silent, offline.

    python tests/v2_synths_check.py --exe "<CyDAW 2.exe>" [--port 47845]

The presets folder is the test's own (CYDAW_LOCATIONS_FILE). The synth command renders a voice in
memory; the mix is heard by an exported file and the plug-in check renders in memory: nothing is
played.

* An Ambre track: its card holds every setting of its table, by key; a choice is set by its name; an
  unknown key is reported.
* Its sound: a held note at a sensible level, ended after its release; its unison spreads it across
  the stereo; a chord stays under 0 dBFS.
* Its settings are knobs of the track (automatable, set by their keys).
* Its factory presets are there and load.
* The track plays it: the exported mix sounds at the note's pitch (A4: 440 Hz); an exported plug-in
  plays it the same (the family's waveform, a held note held).
* Its window opens; a saved project opened again keeps its settings.
* Colosse: its settings and choices by name, its sound, its unison, its filter, its presets, an
  exported plug-in, its window, a project keeping it.
* Mosaïque: the same, every kind of engine sounding and ending.
* Nébuleuse: the same, every texture sounding as grains, Halo after it in its instrument.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import socket
import subprocess
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47845


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


def read_wav(path: Path) -> tuple[list[float], int]:
    """The first channel."""
    with wave.open(str(path), "rb") as file:
        rate = file.getframerate()
        width = file.getsampwidth()
        channels = file.getnchannels()
        raw = file.readframes(file.getnframes())
    step = width * channels
    out = []
    for at in range(0, len(raw) - step + 1, step):
        frame = raw[at:at + width]
        if width == 3:
            value = int.from_bytes(frame + (b"\xff" if frame[2] & 0x80 else b"\x00"), "little", signed=True) / 8_388_608.0
        elif width == 4:
            value = int.from_bytes(frame, "little", signed=True) / 2_147_483_648.0
        else:
            value = int.from_bytes(frame, "little", signed=True) / 32_768.0
        out.append(value)
    return out, rate


def tone_db(samples: list[float], rate: int, hz: float) -> float:
    """The level of one frequency (Goertzel), Hann-windowed."""
    count = len(samples)
    coefficient = 2.0 * math.cos(2.0 * math.pi * hz / rate)
    s1 = s2 = 0.0
    for index, sample in enumerate(samples):
        window = 0.5 - 0.5 * math.cos(2.0 * math.pi * index / max(1, count - 1))
        s0 = sample * window + coefficient * s1 - s2
        s2, s1 = s1, s0
    power = s1 * s1 + s2 * s2 - coefficient * s1 * s2
    return 10.0 * math.log10(max(power, 1e-30)) - 20.0 * math.log10(max(1, count) / 4.0)


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-synths-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1400x900"], env=environment, stdin=subprocess.DEVNULL,
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
        need({"cmd": "new_project"}, "new project")
        track = need({"cmd": "add_midi_track", "name": "Ambre", "instrument": "ambre-instrument"}, "track")["name"]

        def synth(**fields) -> dict:
            return need(dict({"cmd": "synth", "track": track}, **fields), "synth")

        # ---- Its settings ----
        first = synth()
        check(first.get("model") == "ambre" and len(first.get("values", {})) == 58,
              f"Ambre's card holds its 58 settings ({first.get('model')}, {len(first.get('values', {}))})")
        node = first["nodeId"]
        changed = synth(set={"filter.cutoff": 500, "filter.model": "Cascade", "osc1.wave": "Pulse", "nothing.here": 1})
        values = changed["values"]
        check(values["filter.cutoff"] == 500 and values["filter.model"] == 1 and values["osc1.wave"] == 1,
              "settings are set by key, a choice by its name")
        check(any("nothing.here" in problem for problem in changed.get("problems", [])), "an unknown key is reported")
        clamped = synth(set={"filter.resonance": 7})
        check(clamped["values"]["filter.resonance"] == 1.0, "a setting stays in its range")

        # ---- Its sound, rendered in memory ----
        plain = synth(init=True, note=60, seconds=1.0, tail=2.0)
        check(-36.0 < plain["rmsDb"] < -6.0 and plain["peakDb"] < -1.0,
              f"a held note at a sensible level (rms {plain['rmsDb']:.1f} dB, peak {plain['peakDb']:.1f} dB)")
        check(plain["endDb"] < -90.0, f"the note ends after its release ({plain['endDb']:.0f} dB)")
        check(plain["sideDb"] < -60.0, f"one voice sits in the middle ({plain['sideDb']:.0f} dB on the sides)")
        wide = synth(set={"unison.voices": 4, "unison.spread": 1.0}, note=60, seconds=1.0, tail=2.0)
        check(wide["sideDb"] > -20.0, f"four unison voices spread across the stereo ({wide['sideDb']:.0f} dB)")
        chord = synth(init=True, notes=[48, 55, 60, 64, 67], seconds=1.0, tail=2.0)
        check(chord["peakDb"] < 0.0, f"a five-note chord stays under 0 dBFS ({chord['peakDb']:.1f} dB)")
        dark = synth(set={"filter.cutoff": 80, "filter.env": 0.0, "filter.key": 0.0}, note=60, seconds=1.0, tail=1.0)
        check(dark["rmsDb"] < plain["rmsDb"] - 10.0,
              f"the filter closed darkens it ({plain['rmsDb']:.1f} -> {dark['rmsDb']:.1f} dB)")
        synth(init=True)

        # ---- Knobs of the track ----
        parameters = need({"cmd": "list_parameters", "track": track}, "parameters")["parameters"]
        cutoff = [p["key"] for p in parameters if p["key"].endswith("ambre.filter.cutoff")]
        check(len(cutoff) == 1 and len([p for p in parameters if ".ambre." in p["key"]]) >= 58,
              f"its settings are knobs of the track ({len([p for p in parameters if '.ambre.' in p['key']])})")
        if cutoff:
            need({"cmd": "set_parameter", "track": track, "key": cutoff[0], "value": 3_000.0}, "set cutoff")
            check(abs(synth()["values"]["filter.cutoff"] - 3_000.0) < 1.0, "a knob set by its key reaches the card")

        # ---- Factory presets ----
        listed = need({"cmd": "presets", "track": track, "nodeId": node}, "presets")
        factory = [item["name"] for item in listed.get("presets", []) if item.get("factory")]
        check(len(factory) >= 20 and "Acid Bass" in factory, f"Ambre's factory presets ({len(factory)})")
        if "Acid Bass" in factory:
            need({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": "Acid Bass"}, "load")
            acid = synth()["values"]
            check(acid["voice.mode"] == 2 and acid["filter.resonance"] > 0.7, "a factory preset loads its settings")
        synth(init=True)

        # ---- The track plays it ----
        need({"cmd": "add_midi_clip", "track": track, "name": "A4", "startBeats": 0, "lengthBeats": 4}, "clip")
        need({"cmd": "add_notes", "track": track, "clip": "A4",
              "notes": [{"note": 69, "start": 0, "length": 3, "velocity": 1.0}]}, "notes")
        need({"cmd": "set_master_gain", "gainDb": 0.0, "muted": False}, "master up")
        mix_path = scratch / "ambre.wav"
        need({"cmd": "export_mix", "path": str(mix_path)}, "export")
        for _ in range(600):
            time.sleep(0.1)
            if not need({"cmd": "status"}, "status").get("exporting"):
                break
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master down")
        samples, rate = read_wav(mix_path)
        held = samples[int(0.4 * rate):int(1.2 * rate)]
        level = 20.0 * math.log10(max(1e-9, math.sqrt(sum(s * s for s in held) / max(1, len(held)))))
        at440 = tone_db(held, rate, 440.0)
        check(level > -40.0, f"the exported mix sounds ({level:.1f} dB)")
        check(at440 > tone_db(held, rate, 415.3) + 10.0 and at440 > tone_db(held, rate, 466.2) + 10.0,
              f"A4 sounds at 440 Hz ({at440:.1f} dB)")
        exported = need({"cmd": "instrument_export_check", "track": track, "note": 69, "velocity": 1.0, "seconds": 1.0},
                        "plug-in")
        windows = exported.get("windowsDb", [])
        check(exported.get("waveform") == 10 and exported.get("rms", 0.0) > 1e-3,
              f"an exported plug-in plays Ambre (waveform {exported.get('waveform')}, rms {exported.get('rms', 0.0):.3f})")
        check(len(windows) > 10 and windows[10] > -30.0, "the plug-in holds the held note")

        # ---- Colosse ----
        colosse = need({"cmd": "add_midi_track", "name": "Colosse", "instrument": "colosse-instrument"}, "Colosse")["name"]

        def colosse_synth(**fields) -> dict:
            return need(dict({"cmd": "synth", "track": colosse}, **fields), "Colosse synth")

        first = colosse_synth()
        check(first.get("model") == "colosse" and len(first.get("values", {})) == 101,
              f"Colosse's card holds its 101 settings ({first.get('model')}, {len(first.get('values', {}))})")
        named = colosse_synth(set={"osc1.table": "Voices", "f1.type": "Comb", "mod2.dest": "Pitch", "drive.type": "Crush"})["values"]
        check(named["osc1.table"] == 2 and named["f1.type"] == 4 and named["mod2.dest"] == 1 and named["drive.type"] == 3,
              "Colosse's choices are set by their names")
        heard = colosse_synth(init=True, note=60, seconds=1.0, tail=2.0)
        check(-36.0 < heard["rmsDb"] < -6.0 and heard["endDb"] < -90.0,
              f"Colosse holds a note and ends it (rms {heard['rmsDb']:.1f} dB, end {heard['endDb']:.0f} dB)")
        spread = colosse_synth(set={"unison.voices": 8}, note=60, seconds=1.0, tail=1.0)
        check(spread["sideDb"] > -20.0, f"Colosse's eight unison voices spread ({spread['sideDb']:.0f} dB)")
        dark = colosse_synth(init=True, set={"f1.cutoff": 60, "mod1.amount": 0, "filter.key": 0}, note=60, seconds=1.0, tail=1.0)
        check(dark["rmsDb"] < heard["rmsDb"] - 10.0, f"Colosse's filter closed darkens it ({heard['rmsDb']:.1f} -> {dark['rmsDb']:.1f} dB)")
        colosse_synth(init=True)
        colosse_node = first["nodeId"]
        listed = need({"cmd": "presets", "track": colosse, "nodeId": colosse_node}, "Colosse presets")
        factory = [item["name"] for item in listed.get("presets", []) if item.get("factory")]
        check(len(factory) >= 20 and "Rumble" in factory, f"Colosse's factory presets ({len(factory)})")
        if "Rumble" in factory:
            need({"cmd": "presets", "track": colosse, "nodeId": colosse_node, "action": "load", "name": "Rumble"}, "load")
            growl = colosse_synth()["values"]
            check(growl["voice.mode"] == 1 and growl["f1.type"] == 5 and growl["mod1.source"] == 3,
                  "a Colosse preset loads its settings")
        exported = need({"cmd": "instrument_export_check", "track": colosse, "note": 57, "velocity": 1.0, "seconds": 1.0},
                        "Colosse plug-in")
        check(exported.get("waveform") == 10 and exported.get("rms", 0.0) > 1e-3,
              f"an exported plug-in plays Colosse (rms {exported.get('rms', 0.0):.3f})")
        need({"cmd": "get_track_graph", "track": colosse}, "select Colosse")
        need({"cmd": "show_dialog", "name": "colosse"}, "Colosse window")
        colosse_synth(set={"osc2.table": "Metal", "perf.step5": -0.25, "mod3.source": "Sequence"})

        # ---- A synth's own card taken from the modules: its whole instrument ----
        bare = need({"cmd": "add_midi_track", "name": "Bare card"}, "track")["name"]
        need({"cmd": "add_node", "track": bare, "key": "opale-synth"}, "a loose Opale card")
        need({"cmd": "insert_device", "track": bare, "key": "opale-synth"}, "Opale's card")
        nodes = [node["type"] for node in need({"cmd": "get_track_graph", "track": bare}, "graph")["nodes"]]
        check(nodes.count("opale-synth") == 1 and "voice-allocator" in nodes,
              f"Opale's own card on an instrument track: the whole instrument, the loose card gone ({nodes})")
        played = need({"cmd": "synth", "track": bare, "note": 60, "seconds": 0.5, "tail": 1.0}, "Opale heard")
        check(played.get("rmsDb", -120.0) > -45.0, f"that instrument plays ({played.get('rmsDb', -120.0):.1f} dB)")

        # ---- Mosaïque ----
        mosaique = need({"cmd": "add_midi_track", "name": "Mosaique", "instrument": "mosaique-instrument"}, "Mosaïque")["name"]

        def mosaique_synth(**fields) -> dict:
            return need(dict({"cmd": "synth", "track": mosaique}, **fields), "Mosaïque synth")

        first = mosaique_synth()
        check(first.get("model") == "mosaique" and len(first.get("values", {})) == 141,
              f"Mosaïque's card holds its 141 settings ({first.get('model')}, {len(first.get('values', {}))})")
        named = mosaique_synth(set={"a.type": "Modal", "a.material": "Glass", "b.exciter": "Bow", "mod8.source": "Turing"})["values"]
        check(named["a.type"] == 3 and named["a.material"] == 2 and named["b.exciter"] == 2 and named["mod8.source"] == 6,
              "Mosaïque's choices are set by their names")
        kinds_ok = True
        for kind in ("Analog", "Wavetable", "Harmonic", "Modal"):
            heard = mosaique_synth(init=True, set={"a.type": kind, "b.level": 0}, note=60, seconds=0.8, tail=3.0)
            fine = -45.0 < heard["rmsDb"] < -6.0 and heard["peakDb"] < -1.0 and heard["endDb"] < -90.0
            print(f"     {kind}: rms {heard['rmsDb']:.1f} dB, peak {heard['peakDb']:.1f} dB, end {heard['endDb']:.0f} dB")
            kinds_ok = kinds_ok and fine
        check(kinds_ok, "every kind of Mosaïque engine sounds and ends")
        mosaique_synth(init=True)
        mosaique_node = first["nodeId"]
        listed = need({"cmd": "presets", "track": mosaique, "nodeId": mosaique_node}, "Mosaïque presets")
        factory = [item["name"] for item in listed.get("presets", []) if item.get("factory")]
        check(len(factory) >= 20 and "Marimba" in factory, f"Mosaïque's factory presets ({len(factory)})")
        if "Marimba" in factory:
            need({"cmd": "presets", "track": mosaique, "nodeId": mosaique_node, "action": "load", "name": "Marimba"}, "load")
            marimba = mosaique_synth()["values"]
            check(marimba["a.type"] == 3 and marimba["a.material"] == 0 and marimba["b.level"] == 0,
                  "a Mosaïque preset loads its settings")
        exported = need({"cmd": "instrument_export_check", "track": mosaique, "note": 60, "velocity": 1.0, "seconds": 1.0},
                        "Mosaïque plug-in")
        check(exported.get("waveform") == 10 and exported.get("rms", 0.0) > 1e-3,
              f"an exported plug-in plays Mosaïque (rms {exported.get('rms', 0.0):.3f})")
        need({"cmd": "get_track_graph", "track": mosaique}, "select Mosaïque")
        need({"cmd": "show_dialog", "name": "mosaique"}, "Mosaïque window")
        mosaique_synth(set={"b.type": "Harmonic", "fn.point3": 0.125, "macro2": 0.75})

        # ---- Nébuleuse ----
        nebuleuse = need({"cmd": "add_midi_track", "name": "Nebuleuse", "instrument": "nebuleuse-instrument"}, "Nébuleuse")["name"]

        def nebuleuse_synth(**fields) -> dict:
            return need(dict({"cmd": "synth", "track": nebuleuse}, **fields), "Nébuleuse synth")

        first = nebuleuse_synth()
        check(first.get("model") == "nebuleuse" and len(first.get("values", {})) == 98,
              f"Nébuleuse's card holds its 98 settings ({first.get('model')}, {len(first.get('values', {}))})")
        graph = need({"cmd": "get_track_graph", "track": nebuleuse}, "Nébuleuse graph")
        types = [node.get("type") for node in graph.get("nodes", [])]
        check("halo" in types and types.index("halo") > types.index("nebuleuse-synth"), "Nébuleuse comes with Halo after it")
        textures_ok = True
        for texture in ("Choir", "Glass", "Wind", "Strings", "Bells", "Rain", "Drone"):
            heard = nebuleuse_synth(init=True, set={"a.texture": texture, "b.level": 0, "amp.release": 0.5}, note=60, seconds=1.0, tail=2.0)
            fine = -42.0 < heard["rmsDb"] < -6.0 and heard["peakDb"] < -1.0 and heard["endDb"] < -60.0
            print(f"     {texture}: rms {heard['rmsDb']:.1f} dB, peak {heard['peakDb']:.1f} dB, end {heard['endDb']:.0f} dB")
            textures_ok = textures_ok and fine
        check(textures_ok, "every Nébuleuse texture sounds as grains and ends")
        wide = nebuleuse_synth(init=True, note=60, seconds=1.0, tail=2.0)
        check(wide["sideDb"] > -30.0, f"Nébuleuse's grains spread across the stereo ({wide['sideDb']:.0f} dB)")
        nebuleuse_node = first["nodeId"]
        listed = need({"cmd": "presets", "track": nebuleuse, "nodeId": nebuleuse_node}, "Nébuleuse presets")
        factory = [item["name"] for item in listed.get("presets", []) if item.get("factory")]
        check(len(factory) >= 20 and "Carillon" in factory, f"Nébuleuse's factory presets ({len(factory)})")
        if "Carillon" in factory:
            need({"cmd": "presets", "track": nebuleuse, "nodeId": nebuleuse_node, "action": "load", "name": "Carillon"}, "load")
            bells = nebuleuse_synth()["values"]
            check(bells["a.texture"] == 4 and bells["a.harmonia"] == 4 and bells["b.level"] == 0,
                  "a Nébuleuse preset loads its settings")
        exported = need({"cmd": "instrument_export_check", "track": nebuleuse, "note": 60, "velocity": 1.0, "seconds": 1.0},
                        "Nébuleuse plug-in")
        check(exported.get("waveform") == 10 and exported.get("rms", 0.0) > 1e-3,
              f"an exported plug-in plays Nébuleuse (rms {exported.get('rms', 0.0):.3f})")
        need({"cmd": "show_dialog", "name": "nebuleuse"}, "Nébuleuse window")
        nebuleuse_synth(set={"a.texture": "Rain", "orb.x": -0.5, "mod7.dest": "Harmonia"})

        # ---- The models after it: each an instrument, its sounds, a preset ----
        for model in ("opale", "onyx", "tirette", "alliage", "quartz", "argile", "saphir", "derive", "bronze", "jade"):
            later = need({"cmd": "add_midi_track", "name": model.capitalize(), "instrument": f"{model}-instrument"}, model)["name"]
            heard = need({"cmd": "synth", "track": later, "note": 60, "seconds": 1.0, "tail": 3.0}, f"{model} synth")
            check(heard.get("model") == model and heard["rmsDb"] > -45.0 and heard["endDb"] < -60.0,
                  f"{model} sounds and ends ({heard['rmsDb']:.0f} dB, end {heard['endDb']:.0f} dB)")
            listed = need({"cmd": "presets", "track": later, "nodeId": heard["nodeId"]}, f"{model} presets")
            factory = [item["name"] for item in listed.get("presets", []) if item.get("factory")]
            check(len(factory) >= 20 and "Init" in factory, f"{model}'s factory presets ({len(factory)})")
            chosen = next((name for name in factory if name != "Init"), None)
            if chosen:
                need({"cmd": "presets", "track": later, "nodeId": heard["nodeId"], "action": "load", "name": chosen}, "load")
                loaded = need({"cmd": "synth", "track": later, "note": 48, "seconds": 1.0, "tail": 3.0}, f"{model} preset")
                check(loaded["values"] != heard["values"] and loaded["rmsDb"] > -50.0,
                      f"a {model} preset ({chosen}) loads and sounds ({loaded['rmsDb']:.0f} dB)")

        # ---- Its window, a project saved and opened ----
        need({"cmd": "get_track_graph", "track": track}, "select Ambre")
        need({"cmd": "show_dialog", "name": "ambre"}, "window")
        synth(set={"filter.cutoff": 777, "osc2.semi": 5, "lfo2.sync": 6})
        project = scratch / "Ambre.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        # The project opens in the background: asked until its track is there.
        kept = {}
        for _ in range(150):
            answer = request({"cmd": "synth", "track": track})
            if answer.get("ok"):
                kept = answer["result"]["values"]
                break
            time.sleep(0.2)
        check(abs(kept.get("filter.cutoff", 0) - 777) < 0.5 and kept.get("osc2.semi") == 5 and kept.get("lfo2.sync") == 6,
              "a saved project keeps Ambre's settings")
        reopened = request({"cmd": "synth", "track": colosse})
        kept = reopened.get("result", {}).get("values", {}) if reopened.get("ok") else {}
        check(kept.get("osc2.table") == 3 and abs(kept.get("perf.step5", 0) + 0.25) < 1e-4 and kept.get("mod3.source") == 5,
              "a saved project keeps Colosse's settings")
        reopened = request({"cmd": "synth", "track": mosaique})
        kept = reopened.get("result", {}).get("values", {}) if reopened.get("ok") else {}
        check(kept.get("b.type") == 2 and abs(kept.get("fn.point3", 0) - 0.125) < 1e-4 and abs(kept.get("macro2", 0) - 0.75) < 1e-4,
              "a saved project keeps Mosaïque's settings")
        reopened = request({"cmd": "synth", "track": nebuleuse})
        kept = reopened.get("result", {}).get("values", {}) if reopened.get("ok") else {}
        check(kept.get("a.texture") == 5 and abs(kept.get("orb.x", 0) + 0.5) < 1e-4 and kept.get("mod7.dest") == 18,
              "a saved project keeps Nébuleuse's settings")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=20)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
