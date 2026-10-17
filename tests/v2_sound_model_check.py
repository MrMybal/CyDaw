"""A sound model keeps playing wherever its card goes, silently (master at -60 dB).

    python tests/v2_sound_model_check.py --exe "<CyDAW 2.exe>" [--port 47823]

CyDAW 2 is started with its control port; a small model (tests/fixtures,
two partials of A3) is loaded as an instrument, then taken through every
path that used to drop it:

* its card made into a device (a module), undone and redone;
* the project saved and opened again;
* the device's inside edited in the workshop and written back;
* the instrument saved in My devices and inserted on another track;
* the instrument exported as a VST3 project: the state written into it,
  restored as the plug-in restores it (instrument_export_check) and played
  offline by the plug-in's renderer. No plug-in is built.

Every check plays the model: through the track (its meter) and through the
export path (offline). Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
FIXTURE = HERE / "fixtures" / "two-partials.cysound"
# Two sines of 0.501 and 0.251: their RMS.
EXPECTED_RMS = (0.5012 ** 2 / 2 + 0.2512 ** 2 / 2) ** 0.5

failures: list[str] = []
port = 47823
host: str | None = None
plugin: str | None = None


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


def exported(track: str, what: str) -> dict:
    """The track's instrument as an exported plug-in plays it, offline."""
    result = need({"cmd": "instrument_export_check", "track": track, "note": 69, "seconds": 0.6}, "export " + what)
    playing = result.get("soundModel") and result.get("waveform") == 6
    close = abs(result.get("rms", 0.0) - EXPECTED_RMS) < EXPECTED_RMS * 0.15
    check(bool(playing and close),
          f"{what}: the exported plug-in plays the model (model {result.get('soundModel')}, waveform "
          f"{result.get('waveform')}, rms {result.get('rms', 0):.3f} for {EXPECTED_RMS:.3f})")
    return result


def hosted(track: str, what: str, scratch: Path) -> None:
    """The compiled runtime plug-in, the state restored as a host does, a note played."""
    if not host or not plugin:
        return
    state = scratch / "runtime-state.json"
    need({"cmd": "instrument_export_check", "track": track, "seconds": 0.1,
          "stateOut": str(state).replace(os.sep, "/")}, "state " + what)
    run = subprocess.run([host, plugin, str(state), "69"], capture_output=True, text=True, timeout=120, encoding="utf-8")
    lines = [line for line in run.stdout.splitlines() if line.strip().startswith("{")]
    result = json.loads(lines[-1]) if lines else {"error": run.stdout + run.stderr}
    rms = result.get("rms", 0.0)
    check(result.get("loaded") is True and abs(rms - EXPECTED_RMS) < EXPECTED_RMS * 0.2,
          f"{what}: the compiled plug-in, its state restored, plays the model (rms {rms:.3f}; {result.get('error', 'ok')})")
    # The card's settings are the host's parameters: listed, and moving them
    # from the host (its harmonics and its noise down) is heard.
    names = result.get("parameters", [])
    listed = any(name in ("Harmoniques", "Harmonics") for name in names)
    check(listed, f"{what}: a host lists the model's settings ({len(names)} parameters)")
    run = subprocess.run([host, plugin, str(state), "69", "early", "set:Harmoniques|Harmonics=0", "set:Bruit|Noise=0"],
                         capture_output=True, text=True, timeout=120)
    lines = [line for line in run.stdout.splitlines() if line.strip().startswith("{")]
    quiet = json.loads(lines[-1]) if lines else {"error": run.stdout + run.stderr}
    check(len(quiet.get("moved", [])) >= 2 and quiet.get("rms", 1.0) < rms * 0.2,
          f"{what}: the host turns them and hears it ({quiet.get('moved')}, rms {rms:.3f} -> {quiet.get('rms', 0):.3f})")
    # The voice's own filter too (an instrument's settings reach the voices).
    cutoff = next((index for index, name in enumerate(names) if name in ("Fréquence de coupure", "Cutoff")), -1)
    run = subprocess.run([host, plugin, str(state), "69", "early", f"set:#{cutoff}=0"],
                         capture_output=True, text=True, timeout=120, encoding="utf-8")
    lines = [line for line in run.stdout.splitlines() if line.strip().startswith("{")]
    dark = json.loads(lines[-1]) if lines else {"error": run.stdout + run.stderr}
    check(len(dark.get("moved", [])) >= 1 and dark.get("rms", 1.0) < rms * 0.2,
          f"{what}: the host closes the voice's filter and hears it ({dark.get('moved')}, rms {rms:.3f} -> {dark.get('rms', 0):.3f})")


def heard(track: str, what: str) -> None:
    """The track plays its A4 (the master at -60 dB)."""
    need({"cmd": "set_master_gain", "gainDb": -60}, "master")
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
    check(peak > -12.0, f"{what}: the track plays it (peak {peak:.1f} dB)")


def nodes(track: str) -> list[dict]:
    return need({"cmd": "get_track_graph", "track": track}, "graph")["nodes"]


def read_voice(path: Path) -> dict:
    """What an exported plug-in's voice data (.cyvoice) holds: the sampler's
    samples and zones, the sound model's zones."""
    data = path.read_bytes()
    at = 0

    def take(fmt: str):
        nonlocal at
        values = struct.unpack_from("<" + fmt, data, at)
        at += struct.calcsize("<" + fmt)
        return values if len(values) > 1 else values[0]

    def text() -> str:
        nonlocal at
        size = take("I")
        at += size
        return data[at - size:at].decode("utf-8")

    magic, version, parts = take("III")
    voice = {"magic": magic == 0x41565943, "version": version}
    if parts & 1:
        voice["sampler"] = text()
        samples = []
        for _ in range(take("I")):
            name = text()
            channels, frames, rate, _scale, _loop_start, _loop_end, count = take("iqdfqqQ")
            at += 2 * count
            samples.append({"name": name, "channels": channels, "frames": frames, "rate": rate, "pcm": count})
        voice["samples"] = samples
        voice["zones"] = take("I")
    elif parts & 2:
        voice["model"] = text()
        voice["modelZones"] = take("I")
    return voice

def scenario(scratch: Path) -> None:
    need({"cmd": "set_master_gain", "gainDb": -60}, "master")
    need({"cmd": "new_project"}, "new")
    loaded = need({"cmd": "load_sound_model", "file": str(FIXTURE)}, "load")
    track = loaded["track"]
    check(track == "Deux partiels", f"the model makes an instrument track ({track})")
    need({"cmd": "add_midi_clip", "track": track, "start": 0, "length": 4}, "clip")
    need({"cmd": "add_notes", "track": track, "notes": [{"note": 69, "start": 0, "length": 3, "velocity": 0.8}]}, "notes")
    exported(track, "a top-level card")
    hosted(track, "a top-level card", scratch)
    heard(track, "a top-level card")

    # Its voice section made into a device (notes in, sound out).
    section = [node["id"] for node in nodes(track)
               if node["type"] in ("voice-allocator", "sound-model", "envelope", "gain", "voice-filter")]
    grouped = need({"cmd": "create_device", "track": track, "nodeIds": section, "name": "Modele groupe"}, "group")
    device = grouped["nodeId"]
    types = [node["type"] for node in nodes(track)]
    check("sound-model" not in types, f"the card is inside a device now ({types})")
    exported(track, "inside a device")
    hosted(track, "inside a device", scratch)
    heard(track, "inside a device")
    need({"cmd": "undo", "scope": "graph"}, "undo")
    check("sound-model" in [node["type"] for node in nodes(track)], "undone, the card is back")
    exported(track, "undone")
    need({"cmd": "redo", "scope": "graph"}, "redo")
    exported(track, "redone")

    # Saved and opened.
    project = scratch / "sound-model-check.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    exported(track, "reopened")
    heard(track, "reopened")

    # The device's inside in the workshop, a knob turned, written back.
    device = [node["id"] for node in nodes(track) if node["type"] not in
              ("midi-input", "voice-allocator", "envelope", "gain", "voice-filter", "master")][0]
    need({"cmd": "workshop", "action": "open_track", "track": track, "nodeId": device}, "workshop open")
    inside = need({"cmd": "get_track_graph", "track": "workshop"}, "workshop graph")["nodes"]
    model_card = [node for node in inside if node["type"] == "sound-model"]
    check(len(model_card) == 1, "the workshop shows the card inside the device")
    if model_card:
        need({"cmd": "set_node_parameter", "track": "workshop", "nodeId": model_card[0]["id"],
              "parameter": "model.brightness", "value": 3.0}, "brightness")
    need({"cmd": "workshop", "action": "close"}, "workshop close")
    result = exported(track, "after the workshop")
    values = result.get("soundModelValues", [])
    check(len(values) > 3 and abs(values[3] - 3.0) < 1e-3, f"the knob turned in the workshop reached the plug-in ({values[:4]})")

    # In My devices, then on another track.
    need({"cmd": "workshop", "action": "open_track", "track": track, "nodeId": 0}, "workshop instrument")
    need({"cmd": "workshop", "action": "info", "name": "Deux partiels check"}, "name")
    saved = need({"cmd": "workshop", "action": "save", "asNew": True}, "library save")
    written = Path(saved.get("written", ""))
    try:
        need({"cmd": "workshop", "action": "close"}, "workshop close")
        other = need({"cmd": "add_midi_track", "name": "Autre"}, "track")
        other_name = other.get("name", "Autre")
        need({"cmd": "insert_my_device", "track": other_name, "name": written.stem}, "library insert")
        exported(other_name, "from My devices")
        need({"cmd": "add_midi_clip", "track": other_name, "start": 0, "length": 4}, "clip")
        need({"cmd": "add_notes", "track": other_name, "notes": [{"note": 69, "start": 0, "length": 3, "velocity": 0.8}]},
             "notes")
        heard(other_name, "from My devices")
    finally:
        if written.is_file():
            written.unlink()

    # Exported as a native project (generated only, not built): the plan in
    # its sources, the model in its voice data.
    need({"cmd": "workshop", "action": "open_track", "track": track, "nodeId": 0}, "workshop instrument")
    folder = scratch / "export"
    answer = need({"cmd": "workshop", "action": "vst3_export", "folder": str(folder).replace(os.sep, "/")}, "vst3 project")
    project = Path(answer["project"])
    definition = (project / "generated" / "Definition.cpp").read_text(encoding="utf-8")
    check("PolyOscillatorWaveform>(6)" in definition and definition.count("p.soundModelValues[") >= 13,
          "the plug-in's plan says sound model")
    voice_file = project / "generated" / "Voice.cyvoice"
    voice = read_voice(voice_file) if voice_file.is_file() else {}
    check('d.voiceAssets="Voice.cyvoice"' in definition and voice.get("magic") and voice.get("modelZones", 0) >= 1,
          f"the voice data carries the model (inside its device; {voice.get('modelZones')} zone(s))")
    need({"cmd": "workshop", "action": "close"}, "workshop close")


def main() -> int:
    global port, host, plugin
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=47823)
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
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-sound-model-"))
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
