"""The sampler plays wherever its card goes, silently (master at -60 dB).

    python tests/v2_sampler_check.py --exe "<CyDAW 2.exe>" [--port 47826] [--host ... --plugin ...]

CyDAW 2 is started with its control port. Four stereo test notes (A3 and A4,
a soft and a loud layer each, the right channel at half the left) are
written, then:

* made into a sampler by their names (new_sampler): roots, layers, stereo;
* played through the track (its meter), and as an exported plug-in plays
  them (instrument_export_check, and the compiled runtime plug-in hosted
  offline when --host and --plugin are given);
* taken through a device (Ctrl+G), undo and redo, the project saved and
  opened, the workshop (a knob turned), My devices;
* read from an SFZ;
* the factory instruments, when the factory content is there: listed, one
  harpsichord and the looped violin played, also by the compiled plug-in.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
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
port = 47826
host: str | None = None
plugin: str | None = None

SINE_RMS = 1 / math.sqrt(2)


def velocity_gain(velocity: float, range_db: float = 24.0) -> float:
    return 10 ** (range_db * (velocity - 1.0) / 20.0)


def write_note(path: Path, frequency: float, level: float, seconds: float = 2.0, rate: int = 44_100) -> None:
    frames = int(seconds * rate)
    data = bytearray()
    for index in range(frames):
        value = level * math.sin(2 * math.pi * frequency * index / rate)
        data += struct.pack("<hh", int(round(value * 32767)), int(round(0.5 * value * 32767)))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(data))


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


def exported(track: str, what: str, note: int = 69, velocity: float = 0.9, expected: float | None = None,
             seconds: float = 0.6) -> dict:
    """The track's instrument as an exported plug-in plays it, offline."""
    result = need({"cmd": "instrument_export_check", "track": track, "note": note, "velocity": velocity,
                   "seconds": seconds}, "export " + what)
    playing = result.get("sampler") and result.get("waveform") == 7 and result.get("samplesReady")
    rms = result.get("rms", 0.0)
    close = abs(rms - expected) < expected * 0.12 if expected is not None else rms > 0.005
    check(bool(playing and close),
          f"{what}: the exported plug-in plays the samples (sampler {result.get('sampler')}, waveform "
          f"{result.get('waveform')}, ready {result.get('samplesReady')}, rms {rms:.4f}"
          + (f" for {expected:.4f})" if expected is not None else ")"))
    return result


def hosted(track: str, what: str, scratch: Path, expected: float | None, note: int = 69) -> None:
    """The compiled runtime plug-in, its state restored as a host does, a note played (velocity 0.8)."""
    if not host or not plugin:
        return
    state = scratch / "runtime-state.json"
    need({"cmd": "instrument_export_check", "track": track, "seconds": 0.1,
          "stateOut": str(state).replace(os.sep, "/")}, "state " + what)
    run = subprocess.run([host, plugin, str(state), str(note)], capture_output=True, text=True, timeout=180)
    lines = [line for line in run.stdout.splitlines() if line.strip().startswith("{")]
    result = json.loads(lines[-1]) if lines else {"error": run.stdout + run.stderr}
    rms = result.get("rms", 0.0)
    close = abs(rms - expected) < expected * 0.15 if expected is not None else rms > 0.005
    check(result.get("loaded") is True and close,
          f"{what}: the compiled plug-in, its state restored, plays the samples (rms {rms:.4f}"
          + (f" for {expected:.4f}" if expected is not None else "") + f"; {result.get('error', 'ok')})")


def heard(track: str, what: str) -> None:
    """The track plays its note (the master at -60 dB)."""
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "transport", "action": "play", "position": 0}, "play")
    time.sleep(0.4)
    peak = -120.0
    end = time.time() + 1.0
    while time.time() < end:
        status = need({"cmd": "status"}, "status")
        for meter in status["meters"]:
            if meter["name"] == track:
                peak = max(peak, meter["peakDb"])
        if status["masterMeter"]["peakDb"] > -40:
            check(False, "the master stays quiet")
        time.sleep(0.05)
    need({"cmd": "transport", "action": "stop"}, "stop")
    time.sleep(0.3)
    check(peak > -24.0, f"{what}: the track plays it (peak {peak:.1f} dB)")


def nodes(track: str) -> list[dict]:
    return need({"cmd": "get_track_graph", "track": track}, "graph")["nodes"]


def embedded_state(header: Path) -> dict:
    text = header.read_text(encoding="utf-8")
    block = text[text.index("embeddedStateJsonChunks"):]
    chunks = re.findall(r'^\s*"((?:[^"\\]|\\.)*)",$', block, re.MULTILINE)
    escapes = {"n": "\n", "r": "\r", '"': '"', "\\": "\\"}
    joined = re.sub(r"\\(.)", lambda match: escapes.get(match.group(1), match.group(1)), "".join(chunks))
    return json.loads(joined)


def has_map(graph: dict) -> bool:
    for container in [graph] + list(graph.get("modules", [])):
        for node in container.get("nodes", []):
            if node.get("type") == "sampler" and isinstance(node.get("sampler"), dict):
                return True
    return False


def scenario(scratch: Path) -> None:
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new")
    notes = scratch / "notes"
    notes.mkdir()
    files = []
    for name, frequency, level in (("Test_A3_v1", 220.0, 0.2), ("Test_A3_v2", 220.0, 0.5),
                                   ("Test_A4_v1", 440.0, 0.2), ("Test_A4_v2", 440.0, 0.5)):
        path = notes / f"{name}.wav"
        write_note(path, frequency, level)
        files.append(str(path).replace(os.sep, "/"))

    # The simple sampler: one sound over the whole keyboard around its note;
    # another dropped replaces it.
    simple = need({"cmd": "add_midi_track", "name": "Simple", "instrument": "simple-sampler-instrument"}, "simple sampler")["name"]
    need({"cmd": "load_sampler", "track": simple, "file": files[2]}, "simple sample")
    held = need({"cmd": "sampler_map", "track": simple}, "simple map")["map"]["zones"]
    check(len(held) == 1 and held[0]["keys"] == [0, 127] and held[0]["root"] == 69,
          f"the simple sampler spreads its one sound over the keys ({held})")
    need({"cmd": "load_sampler", "track": simple, "file": files[0]}, "simple sample replaced")
    held = need({"cmd": "sampler_map", "track": simple}, "simple map")["map"]["zones"]
    check(len(held) == 1 and held[0]["root"] == 57 and "Test_A3_v1" in held[0]["file"], f"a sound dropped replaces it ({held})")
    refused = request({"cmd": "load_sampler", "track": simple, "file": files[0].replace(".wav", ".missing.wav")})
    check(not refused.get("ok"), "a sound that is not there is refused")
    need({"cmd": "remove_track", "track": simple}, "simple removed")

    made = need({"cmd": "new_sampler", "files": files}, "new sampler")
    track = made["track"]
    status = need({"cmd": "sampler_status", "track": track, "wait": 30}, "status")
    check(status.get("zones") == 4 and status.get("ready") == 4, f"four zones, decoded ({status})")
    zones = need({"cmd": "sampler_map", "track": track}, "map")["map"]["zones"]
    roots = sorted((zone["root"], tuple(zone["velocities"])) for zone in zones)
    check([root for root, _ in roots] == [57, 57, 69, 69] and roots[0][1][1] < roots[1][1][0],
          f"the names give the notes and the layers ({roots})")
    loud = 0.5 * velocity_gain(0.9) * SINE_RMS
    soft = 0.2 * velocity_gain(0.3) * SINE_RMS
    result = exported(track, "a top-level card", expected=loud)
    check(abs(result.get("rmsRight", 0) / max(result.get("rms", 1e-9), 1e-9) - 0.5) < 0.03,
          f"in stereo: the right at half the left ({result.get('rmsRight', 0):.4f})")
    exported(track, "a soft note", velocity=0.3, expected=soft)
    hosted(track, "a top-level card", scratch, 0.5 * velocity_gain(0.8) * SINE_RMS)
    need({"cmd": "add_midi_clip", "track": track, "start": 0, "length": 4}, "clip")
    need({"cmd": "add_notes", "track": track, "notes": [{"note": 69, "start": 0, "length": 3, "velocity": 0.9}]}, "notes")
    heard(track, "a top-level card")

    # Its voice section made into a device (notes in, sound out).
    section = [node["id"] for node in nodes(track)
               if node["type"] in ("voice-allocator", "sampler", "envelope", "gain", "voice-filter")]
    need({"cmd": "create_device", "track": track, "nodeIds": section, "name": "Sampler groupe"}, "group")
    check("sampler" not in [node["type"] for node in nodes(track)], "the card is inside a device now")
    exported(track, "inside a device", expected=loud)
    hosted(track, "inside a device", scratch, 0.5 * velocity_gain(0.8) * SINE_RMS)
    need({"cmd": "undo", "scope": "graph"}, "undo")
    check("sampler" in [node["type"] for node in nodes(track)], "undone, the card is back")
    exported(track, "undone", expected=loud)
    need({"cmd": "redo", "scope": "graph"}, "redo")
    exported(track, "redone", expected=loud)

    # Saved and opened.
    project = scratch / "sampler-check.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    exported(track, "reopened", expected=loud)
    heard(track, "reopened")

    # The device's inside in the workshop, a knob turned, written back.
    device = [node["id"] for node in nodes(track) if node["type"] not in
              ("midi-input", "voice-allocator", "envelope", "gain", "voice-filter", "master")][0]
    need({"cmd": "workshop", "action": "open_track", "track": track, "nodeId": device}, "workshop open")
    inside = need({"cmd": "get_track_graph", "track": "workshop"}, "workshop graph")["nodes"]
    cards = [node for node in inside if node["type"] == "sampler"]
    check(len(cards) == 1, "the workshop shows the card inside the device")
    if cards:
        need({"cmd": "set_node_parameter", "track": "workshop", "nodeId": cards[0]["id"],
              "parameter": "sampler.velocityDb", "value": 12.0}, "velocity range")
    need({"cmd": "workshop", "action": "close"}, "workshop close")
    result = exported(track, "after the workshop", expected=0.5 * velocity_gain(0.9, 12.0) * SINE_RMS)
    values = result.get("samplerValues", [])
    check(len(values) > 3 and abs(values[3] - 12.0) < 1e-3, f"the knob turned in the workshop reached the plug-in ({values[:4]})")

    # In My devices, then on another track.
    need({"cmd": "workshop", "action": "open_track", "track": track, "nodeId": 0}, "workshop instrument")
    need({"cmd": "workshop", "action": "info", "name": "Sampler check"}, "name")
    saved = need({"cmd": "workshop", "action": "save", "asNew": True}, "library save")
    written = Path(saved.get("written", ""))
    try:
        need({"cmd": "workshop", "action": "close"}, "workshop close")
        other = need({"cmd": "add_midi_track", "name": "Autre"}, "track")
        other_name = other.get("name", "Autre")
        need({"cmd": "insert_my_device", "track": other_name, "name": written.stem}, "library insert")
        exported(other_name, "from My devices", expected=0.5 * velocity_gain(0.9, 12.0) * SINE_RMS)
    finally:
        if written.is_file():
            written.unlink()

    # An SFZ: the same notes, A4 only, its loud layer louder by 6 dB.
    sfz = notes / "test.sfz"
    sfz.write_text("// test\n<group> lokey=c3 hikey=c6\n"
                   "<region> sample=Test_A4_v1.wav pitch_keycenter=a4 lovel=1 hivel=63\n"
                   "<region> sample=Test_A4_v2.wav pitch_keycenter=a4 lovel=64 hivel=127 volume=-6\n",
                   encoding="utf-8")
    loaded = need({"cmd": "load_sampler", "file": str(sfz).replace(os.sep, "/")}, "sfz")
    need({"cmd": "sampler_status", "track": loaded["track"], "wait": 30}, "sfz status")
    exported(loaded["track"], "an SFZ", note=69, velocity=0.9, expected=loud * 10 ** (-6 / 20))

    # Exported as a VST3 project (generated only, not built).
    need({"cmd": "workshop", "action": "open_track", "track": track, "nodeId": 0}, "workshop instrument")
    folder = scratch / "export"
    answer = need({"cmd": "workshop", "action": "vst3_export", "folder": str(folder).replace(os.sep, "/")}, "vst3 project")
    header = Path(answer["project"]) / "generated" / "CyDawGeneratedGraphConfig.h"
    state = embedded_state(header)
    plan = state.get("polySynth", {})
    check(plan.get("waveform") == 7 and len(plan.get("samplerValues", [])) >= 10,
          f"the plug-in's plan says sampler ({plan.get('waveform')})")
    check(has_map(state.get("editor", {})), "the plug-in's state carries the zones (inside its device)")
    need({"cmd": "workshop", "action": "close"}, "workshop close")

    # The factory instruments, when built.
    listed = need({"cmd": "factory_instruments"}, "factory")
    instruments = {item["key"]: item for item in listed.get("instruments", [])}
    if not instruments:
        print("skip the factory instruments: none in", listed.get("folder"), flush=True)
        return
    check(len(instruments) >= 17, f"the factory instruments are listed ({len(instruments)})")
    for key, note, seconds in (("harpsichord-italian", 60, 0.8), ("violin-arco", 67, 20.0)):
        if key not in instruments:
            check(False, f"{key} is a factory instrument")
            continue
        added = need({"cmd": "add_factory_instrument", "key": key}, key)
        status = need({"cmd": "sampler_status", "track": added["track"], "wait": 60}, key + " status")
        check(status.get("ready") == status.get("zones") and status.get("failed") == 0,
              f"{key}: every sample found and decoded ({status.get('ready')}/{status.get('zones')})")
        result = exported(added["track"], key, note=note, velocity=0.8, seconds=seconds)
        if key == "violin-arco":
            check(result.get("tailRms", 0) > result.get("rms", 1) * 0.25,
                  f"the violin's loop holds the note past its recording ({result.get('tailRms', 0):.4f} at 20 s)")
        hosted(added["track"], key, scratch, None, note)

    # The pianos made from others' notes: a honky-tonk (each note twice, detuned), a felt piano.
    for key in ("upright-honky-tonk", "felt-piano"):
        if key not in instruments:
            check(False, f"{key} is a factory instrument")
            continue
        added = need({"cmd": "add_factory_instrument", "key": key}, key)
        status = need({"cmd": "sampler_status", "track": added["track"], "wait": 60}, key + " status")
        check(status.get("ready") == status.get("zones") and status.get("failed") == 0,
              f"{key}: its notes found (those of the piano it comes from, {status.get('ready')}/{status.get('zones')})")
        exported(added["track"], key, note=60, velocity=0.6)

    # The same instruments as sound models (the companion's analysis of the notes).
    for key in ("harpsichord-italian", "grand-steinway"):
        if not instruments.get(key, {}).get("model"):
            check(False, f"{key} has a sound model")
            continue
        added = need({"cmd": "add_factory_instrument", "key": key, "model": True}, key + " model")
        result = need({"cmd": "instrument_export_check", "track": added["track"], "note": 60, "velocity": 0.8,
                       "seconds": 1.0}, key + " model export")
        check(result.get("soundModel") and result.get("waveform") == 6 and result.get("rms", 0) > 0.003,
              f"{key}: its sound model plays, also as an exported plug-in (rms {result.get('rms', 0):.4f}, "
              f"{result.get('description', '')})")


def main() -> int:
    global port, host, plugin
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=47826)
    parser.add_argument("--host", help="cydaw_graph_runtime_host, to play the compiled runtime plug-in")
    parser.add_argument("--plugin", help="CyDAW Graph Runtime.vst3")
    arguments = parser.parse_args()
    port = arguments.port
    host = str(Path(arguments.host).resolve()) if arguments.host else None
    plugin = str(Path(arguments.plugin).resolve()) if arguments.plugin else None
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1")
    executable = str(Path(arguments.exe).resolve())
    process = subprocess.Popen([executable, f"--control-port={port}", "--silent", "--window-size=1500x900"],
                               env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-sampler-"))
    try:
        for _ in range(200):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        scenario(scratch)
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        try:
            request({"cmd": "quit"})
        except OSError:
            pass
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print("failures:", len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
