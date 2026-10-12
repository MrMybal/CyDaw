"""Audio editing, takes and the rest of the "what was missing" round. Silent, offline.

    python tests/v2_editing_check.py --exe "<CyDAW 2.exe>" [--plugin "<CyDAW Test Gain.vst3>"] [--port 47842]

The sounds are made here (tones in WAV files); the edits' files land in the test's own folder.

* Destructive edits of a clip: normalize (its peak where asked), reverse, a fade in (its shape),
  cut (the clip and its file a second shorter), crop, insert silence, copy and paste. Each is a
  version in the clip's history; going back to one plays it; undo takes an edit back.
* Takes: a second take added, the passages it plays chosen (comping); a project keeps the takes,
  their shift and the passages. A clip transposed keeps it in the project.
* While the session plays (muted), a stretched and transposed clip still sounds.
* The modulation graph: its LFO set from the port, cards added to it (a constant, a range) and
  wired; its output is what the cards compute, and a project keeps it.
* A hosted plug-in (with --plugin): its knobs pinned, kept by a project; its parameter moved by
  the track's modulation, back to its value when the modulation stops; the value as the plug-in
  writes it.
* Prisme: a wavetable imported into an oscillator, kept by a project.
* Flattening the comp: one file, each passage from its take (440 Hz, then 660 Hz), joined without
  a click, silence past the last passage as when it plays; one take left, in the history; undone.
* Events: a clip of one sound given a second (it becomes a timeline of events), its own sound
  trimmed, the other moved, split, a piece made quieter and one removed, one normalized in place,
  one duplicated and the copy transposed an octave; exported, each plays where it was put, silence
  between, the copy an octave up.

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
port = 47842


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


def write_wav(path: Path, samples: list[float], rate: int = 48_000) -> None:
    data = b"".join(struct.pack("<h", int(round(max(-1.0, min(1.0, s)) * 32767))) for s in samples)
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE", b"fmt ", 16, 1, 1, rate, rate * 2, 2, 16,
                         b"data", len(data))
    path.write_bytes(header + data)


def read_wav(path: str) -> tuple[list[float], int]:
    with wave.open(path, "rb") as file:
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
        else:
            value = int.from_bytes(frame, "little", signed=True) / 32_768.0
        out.append(value)
    return out, rate


def tone(hz: float, seconds: float, level: float = 0.5, rate: int = 48_000) -> list[float]:
    return [level * math.sin(2 * math.pi * hz * n / rate) for n in range(int(seconds * rate))]


def frequency(samples: list[float], rate: int, start: float, end: float) -> float:
    part = samples[int(start * rate):int(end * rate)]
    crossings = sum(1 for a, b in zip(part, part[1:]) if a <= 0.0 < b)
    return crossings / max(1e-9, end - start)


def rms(samples: list[float], rate: int, start: float, end: float) -> float:
    part = samples[int(start * rate):int(end * rate)]
    return math.sqrt(sum(x * x for x in part) / max(1, len(part)))


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--plugin", default="")
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-editing-check-"))
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
        # The project lives in the test's folder: the edits' files go beside it.
        project = scratch / "Editing.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "first save")
        sound_a = scratch / "tone-a.wav"
        sound_b = scratch / "tone-b.wav"
        write_wav(sound_a, tone(440.0, 4.0, 0.25))
        write_wav(sound_b, tone(660.0, 4.0, 0.25))
        track = need({"cmd": "add_audio_track", "file": str(sound_a)}, "track")["name"]

        def clip() -> dict:
            return need({"cmd": "list_audio_clips", "track": track}, "clips")["clips"][0]

        clip_id = clip()["id"]

        def edit(operation: str, start: float, length: float, **extra) -> dict:
            return need(dict({"cmd": "edit_audio", "track": track, "clipId": clip_id, "operation": operation,
                              "start": start, "length": length}, **extra), operation)["clips"][0]

        # ---- Destructive edits ----
        edited = edit("normalize", 0.0, 4.0, level=-1.0)
        samples, _ = read_wav(edited["source"])
        peak = max(abs(s) for s in samples)
        check(abs(20 * math.log10(peak) + 1.0) < 0.1, f"normalize puts the peak at -1 dBFS ({20 * math.log10(peak):.2f})")
        check(Path(edited["source"]).parent == scratch / "edits", "the edit's file is beside the project")
        edited = edit("fade-in", 0.0, 1.0, curve="slow")
        samples, rate = read_wav(edited["source"])
        quarter = max(abs(s) for s in samples[int(0.24 * rate):int(0.26 * rate)])
        check(quarter < 0.1 * peak, f"a slow fade in is quiet at its quarter ({quarter:.3f})")
        edited = edit("reverse", 1.0, 1.0)
        check(edited["versions"] == 4 and edited["version"] == 3, f"each edit is a version ({edited['versions']})")
        before = clip()
        edited = edit("cut", 2.0, 1.0)
        check(abs(edited["duration"] - (before["duration"] - 1.0)) < 0.01 and abs(edited["sourceLength"] - 3.0) < 0.01,
              f"a cut takes a second out of the clip and its file ({edited['duration']:.2f} s)")
        need({"cmd": "copy_audio", "track": track, "clipId": clip_id, "start": 0.5, "length": 0.5}, "copy")
        edited = edit("paste", 1.0, 0.0)
        check(abs(edited["sourceLength"] - 3.5) < 0.01, f"paste puts the copied half second in ({edited['sourceLength']:.2f} s)")
        edited = edit("insert-silence", 0.5, 0.25)
        samples, rate = read_wav(edited["source"])
        hole = max(abs(s) for s in samples[int(0.52 * rate):int(0.73 * rate)])
        check(hole < 1e-4 and abs(edited["sourceLength"] - 3.75) < 0.01, "insert silence makes a gap of silence")
        edited = edit("crop", 0.0, 3.0)
        check(abs(edited["duration"] - 3.0) < 0.01, f"crop keeps the selection alone ({edited['duration']:.2f} s)")
        history = need({"cmd": "audio_clip_history", "track": track, "clipId": clip_id}, "history")
        labels = [version["label"] for version in history["versions"]]
        check(labels[0] == "Original" and len(labels) == 8, f"the history lists every version ({len(labels)})")
        back = need({"cmd": "audio_clip_history", "track": track, "clipId": clip_id, "version": 0}, "version 0")
        check(abs(back["duration"] - 4.0) < 0.01 and Path(clip()["source"]) == sound_a, "the original version plays again")
        need({"cmd": "undo", "scope": "arrangement"}, "undo")
        check(clip()["version"] == 7, "undo takes the choice of version back")
        problem = request({"cmd": "edit_audio", "track": track, "clipId": clip_id, "operation": "nonsense", "start": 0, "length": 1})
        check(not problem.get("ok"), "an unknown edit is refused")

        # ---- Takes and comping ----
        need({"cmd": "audio_clip_history", "track": track, "clipId": clip_id, "version": 0}, "version 0")
        takes = need({"cmd": "audio_takes", "track": track, "clipId": clip_id, "action": "add", "file": str(sound_b),
                      "shift": 0.25, "activate": False}, "add a take")
        check(len(takes["takes"]) == 2 and takes["activeTake"] == 0, f"a second take ({len(takes['takes'])})")
        comp = need({"cmd": "audio_takes", "track": track, "clipId": clip_id, "action": "comp",
                     "comp": [{"start": 0.0, "end": 1.5, "take": 0}, {"start": 1.5, "end": 4.0, "take": 1}]}, "comp")
        check(len(comp["comp"]) == 2 and comp["comp"][1]["take"] == 1, "the passages are chosen from both takes")
        need({"cmd": "set_audio_clip", "track": track, "clipId": clip_id, "pitch": 7, "stretchMode": "tonal"}, "transpose")
        need({"cmd": "set_audio_clip", "track": track, "clipId": clip_id, "sourceDuration": 4.0, "duration": 6.0}, "stretch")
        # Playing (muted): the stretched, transposed, comped clip sounds.
        need({"cmd": "transport", "action": "play"}, "play")
        loudest = -120.0
        for _ in range(30):
            time.sleep(0.1)
            meters = need({"cmd": "status"}, "status").get("meters", [])
            for meter in meters:
                if meter.get("name") == track:
                    loudest = max(loudest, meter.get("peakDb", -120.0))
        need({"cmd": "transport", "action": "stop"}, "stop")
        check(loudest > -40.0, f"a stretched, transposed, comped clip sounds ({loudest:.1f} dB)")
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        kept = {}
        for _ in range(100):
            answer = request({"cmd": "list_audio_clips", "track": track})
            if answer.get("ok") and answer["result"]["clips"]:
                kept = answer["result"]["clips"][0]
                break
            time.sleep(0.2)
        check(kept.get("pitch") == 7 and kept.get("takes") == 2 and kept.get("compPassages") == 2 and kept.get("versions") == 8,
              f"a project keeps the transposition, the takes, the passages, the history ({kept.get('pitch')}, "
              f"{kept.get('takes')}, {kept.get('compPassages')}, {kept.get('versions')})")
        clip_id = kept.get("id", clip_id)
        takes = need({"cmd": "audio_takes", "track": track, "clipId": clip_id}, "takes")
        check(abs(takes["takes"][1]["shift"] - 0.25) < 1e-6, "a take keeps its shift")

        # ---- The comp flattened ----
        # Stretched to 6 s from 4: the passages 0-1.5 (take 1) and 1.5-4 (take 2) of the clip are 0-1 and
        # 1-2.667 s of its source; past the last passage the clip plays nothing.
        flat = need({"cmd": "audio_takes", "track": track, "clipId": clip_id, "action": "flatten"}, "flatten")
        check(len(flat["takes"]) == 1 and not flat["comp"], f"flattened: one take, no passages ({len(flat['takes'])})")
        samples, rate = read_wav(flat["takes"][0]["file"])
        low = frequency(samples, rate, 0.1, 0.9)
        high = frequency(samples, rate, 1.2, 2.5)
        check(abs(low - 440.0) < 5.0 and abs(high - 660.0) < 5.0,
              f"each passage from its take, as it plays ({low:.0f} Hz, then {high:.0f} Hz)")
        join = samples[int(0.97 * rate):int(1.03 * rate)]
        jump = max(abs(b - a) for a, b in zip(join, join[1:]))
        check(jump < 0.05, f"the passages meet without a click (largest step {jump:.3f})")
        check(rms(samples, rate, 2.8, 3.8) < 1e-4, "past the last passage, silence, as when it plays")
        history = need({"cmd": "audio_clip_history", "track": track, "clipId": clip_id}, "history")
        check(history["versions"][-1]["label"] == "Mise à plat des prises",
              f"the flattening is in the history ({history['versions'][-1]['label']})")
        need({"cmd": "undo", "scope": "arrangement"}, "undo the flattening")
        again = need({"cmd": "audio_takes", "track": track, "clipId": clip_id}, "takes")
        check(len(again["takes"]) == 2 and len(again["comp"]) == 2, "undone, the takes and the passages are back")

        # ---- The modulation graph ----
        need({"cmd": "insert_device", "track": track, "key": "multimode-filter"}, "filter")
        modulation = need({"cmd": "track_modulation", "track": track, "lfo": {"depth": 0.0, "offset": 0.5},
                           "assign": {"filter.cutoff": 0.5}}, "assign")
        check(modulation["lfo"]["found"] and any(t["enabled"] for t in modulation["targets"]), "the cutoff is modulated")
        graph = need({"cmd": "get_track_graph", "track": track, "graph": "modulation"}, "modulation graph")
        output = [node["id"] for node in graph["nodes"] if node["type"] == "graph-value-output"][0]
        lfo = [node["id"] for node in graph["nodes"] if node["type"] == "lfo"][0]
        constant = need({"cmd": "add_node", "track": track, "graph": "modulation", "key": "value-constant"}, "constant")["id"]
        ranged = need({"cmd": "add_node", "track": track, "graph": "modulation", "key": "map-range"}, "range")["id"]
        need({"cmd": "set_node_parameter", "track": track, "graph": "modulation", "nodeId": constant, "parameter": "value.constant",
              "value": 0.25}, "constant value")
        need({"cmd": "set_node_parameter", "track": track, "graph": "modulation", "nodeId": ranged, "parameter": "map.outputMinimum",
              "value": -1.0}, "range")
        need({"cmd": "disconnect", "track": track, "graph": "modulation", "source": lfo, "destination": output}, "unplug the LFO")
        need({"cmd": "connect", "track": track, "graph": "modulation", "source": constant, "sourcePort": 1, "destination": ranged,
              "destinationPort": 1}, "wire")
        need({"cmd": "connect", "track": track, "graph": "modulation", "source": ranged, "sourcePort": 2, "destination": output,
              "destinationPort": 1}, "wire")
        value = None
        for _ in range(20):
            time.sleep(0.1)
            value = need({"cmd": "track_modulation", "track": track}, "modulation")["output"]
            if abs(value + 0.5) < 1e-6:
                break
        check(value is not None and abs(value + 0.5) < 1e-6, f"the modulation's output is what its cards compute ({value})")
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        value = None
        for _ in range(30):
            time.sleep(0.1)
            answer = request({"cmd": "track_modulation", "track": track})
            if answer.get("ok"):
                value = answer["result"]["output"]
                if abs(value + 0.5) < 1e-6:
                    break
        check(value is not None and abs(value + 0.5) < 1e-6, f"a project keeps the modulation graph ({value})")

        # ---- A hosted plug-in ----
        if args.plugin:
            need({"cmd": "add_plugin", "track": track, "file": args.plugin}, "plugin")
            listed = need({"cmd": "list_plugin_parameters", "track": track, "plugin": 0, "pin": [0]}, "pin")
            check(listed.get("pinned") == [0] and listed["parameters"][0]["text"] != "", "the knobs pinned, the value's text")
            need({"cmd": "track_modulation", "track": track, "assign": {"plugin:0:0": 0.4, "filter.cutoff": False}}, "modulate")
            moved = None
            for _ in range(30):
                time.sleep(0.1)
                moved = need({"cmd": "list_plugin_parameters", "track": track, "plugin": 0}, "values")["parameters"][0]["value"]
                if abs(moved - 0.3) < 1e-3:
                    break
            check(moved is not None and abs(moved - 0.3) < 1e-3, f"the modulation moves the plug-in's parameter ({moved})")
            need({"cmd": "track_modulation", "track": track, "assign": {"plugin:0:0": False}}, "stop")
            back = need({"cmd": "list_plugin_parameters", "track": track, "plugin": 0}, "values")["parameters"][0]["value"]
            check(abs(back - 0.5) < 1e-3, f"stopped, it goes back to its value ({back})")
            need({"cmd": "save_project", "path": str(project)}, "save")
            need({"cmd": "new_project"}, "new")
            need({"cmd": "open_project", "path": str(project)}, "open")
            pinned = None
            for _ in range(50):
                answer = request({"cmd": "list_plugin_parameters", "track": track, "plugin": 0})
                if answer.get("ok"):
                    pinned = answer["result"].get("pinned")
                    break
                time.sleep(0.2)
            check(pinned == [0], f"a project keeps the pinned knobs ({pinned})")

        # ---- Prisme's own table ----
        frames = []
        for frame in range(8):
            mix = frame / 7
            frames += [(1 - mix) * math.sin(2 * math.pi * i / 2048) + mix * (2 * i / 2048 - 1) * 0.9 for i in range(2048)]
        table = scratch / "table.wav"
        write_wav(table, frames)
        synth = need({"cmd": "add_midi_track", "name": "Prisme", "instrument": "prism-instrument"}, "prisme")["name"]
        imported = need({"cmd": "prism", "track": synth, "importTable": {"osc": "a", "file": str(table)}}, "import")
        check(imported["ownTables"].get("a", {}).get("frames") == 8, f"a wavetable imported ({imported['ownTables']})")
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        own = {}
        for _ in range(50):
            answer = request({"cmd": "prism", "track": synth})
            if answer.get("ok"):
                own = answer["result"]["ownTables"]
                break
            time.sleep(0.2)
        check(own.get("a", {}).get("name") == "table", f"a project keeps the imported table ({own})")

        # ---- Events of a clip ----
        events_track = need({"cmd": "add_audio_track", "file": str(sound_b)}, "events track")["index"]
        events_clip = need({"cmd": "list_audio_clips", "track": events_track}, "clips")["clips"][0]["id"]

        def events(**command) -> dict:
            return need(dict({"cmd": "audio_events", "track": events_track, "clipId": events_clip}, **command),
                        command.get("action", "list"))

        listed = events()
        check(len(listed["events"]) == 1 and not listed["eventTimeline"], "a clip of one sound lists that sound")
        own_event = listed["events"][0]["id"]
        added = events(action="add", file=str(sound_a), start=5.0)
        check(len(added["events"]) == 2 and added["eventTimeline"] and abs(added["duration"] - 9.0) < 0.01,
              f"a second sound makes it a timeline of events ({added['duration']:.2f} s)")
        other = added["eventId"]
        trimmed = events(action="trim", eventId=own_event, start=0.5, end=2.0)
        own = [e for e in trimmed["events"] if e["id"] == own_event][0]
        check(abs(own["offset"] - 0.5) < 1e-6 and abs(own["duration"] - 1.5) < 1e-6,
              f"trimmed, its sound stays in place ({own['offset']:.2f}, {own['duration']:.2f})")
        events(action="move", eventId=other, start=3.0)
        piece = events(action="split", eventId=other, at=4.0)["eventId"]
        events(action="set", eventId=piece, gainDb=-6.0, fadeIn=0.05)
        quieter = [e for e in events()["events"] if e["id"] == piece][0]
        check(abs(quieter["gainDb"] + 6.0) < 1e-6 and abs(quieter["fadeIn"] - 0.05) < 1e-6, "a piece's gain and fade set")
        events(action="remove", eventId=piece)
        before = [e for e in events()["events"] if e["id"] == own_event][0]["file"]
        edited = events(action="edit", eventId=own_event, operation="normalize", level=-1.0)
        after = [e for e in edited["events"] if e["id"] == own_event][0]["file"]
        check(after != before and Path(after).exists(), "an event normalized in place plays a new file")
        events(action="duplicate", eventId=own_event, start=7.0)
        final = events()
        check(len(final["events"]) == 3, f"split, removed, duplicated: three events ({len(final['events'])})")
        copy = [e for e in final["events"] if abs(e["start"] - 7.0) < 1e-6][0]["id"]
        raised = events(action="set", eventId=copy, pitch=12, stretchMode="tonal")
        check([e for e in raised["events"] if e["id"] == copy][0]["pitch"] == 12, "an event transposed on its own")
        # Heard alone (the other tracks muted), exported: each where it was put.
        for spec in (track, synth):
            need({"cmd": "set_track", "track": spec, "muted": True}, "mute")
        need({"cmd": "set_master_gain", "gainDb": 0.0, "muted": False}, "master up")
        mix_path = scratch / "events.wav"
        need({"cmd": "export_mix", "path": str(mix_path)}, "export")
        for _ in range(600):
            time.sleep(0.1)
            if not need({"cmd": "status"}, "status").get("exporting"):
                break
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master down")
        mix, mix_rate = read_wav(str(mix_path))
        heard = [rms(mix, mix_rate, a, b) for a, b in ((0.7, 1.8), (3.2, 3.8), (7.2, 8.3))]
        quiet = [rms(mix, mix_rate, a, b) for a, b in ((0.0, 0.4), (2.2, 2.8), (4.2, 6.8))]
        check(min(heard) > 0.02 and max(quiet) < 0.002,
              f"each event plays where it was put, silence between ({[round(x, 3) for x in heard]}, {[round(x, 4) for x in quiet]})")
        check(abs(frequency(mix, mix_rate, 0.7, 1.8) - 660.0) < 5.0 and abs(frequency(mix, mix_rate, 3.2, 3.8) - 440.0) < 5.0,
              "its own sound, then the added one")
        raised_hz = frequency(mix, mix_rate, 7.2, 8.3)
        check(abs(raised_hz - 1320.0) < 10.0, f"the copy an octave up, its length kept ({raised_hz:.0f} Hz)")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print("FAILED: " + "; ".join(failures) if failures else "All editing checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
