"""The mixing round: latency compensation, sidechains, automation reaching every setting. Silent.

    python tests/v2_mixing_check.py --exe "<CyDAW 2.exe>" [--port 47843]

The sounds are made here (clicks and tones in WAV files) and the mixes are exported (in real time,
the speakers silenced by --silent) into the test's own folder, where each is measured.

* Latency: a Compresseur Pro looking 10 ms ahead makes its track that late; the track beside it
  waits as long, the mix says so, and the two meet in time: a click and its half, inverted, on the
  other track, sum to one click of half height. The export skips the mix's latency (and the
  master limiter's): the click is in the file where it is in the song. No look-ahead, no wait.
* Sidechain: a bass through a Compresseur Pro whose sidechain listens to a "kick" track (its fader
  down, sending before it): while the kick is silent the bass passes whole, though it alone would
  be compressed; once the kick plays the bass drops by 20 dB and more. A project keeps the
  choice; clearing it leaves the bass alone.
* Automation of an instrument's own settings: Prisme's volume, silent then full, heard so.
* Automation inside a module: a gain card made into a device; its knob, automated, is heard.

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
port = 47843


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


def write_wav(path: Path, samples: list[float], rate: int) -> None:
    data = b"".join(struct.pack("<h", int(round(max(-1.0, min(1.0, s)) * 32767))) for s in samples)
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE", b"fmt ", 16, 1, 1, rate, rate * 2, 2, 16,
                         b"data", len(data))
    path.write_bytes(header + data)


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
        else:
            value = int.from_bytes(frame, "little", signed=True) / 32_768.0
        out.append(value)
    return out, rate


def rms(samples: list[float], rate: int, start: float, end: float) -> float:
    part = samples[int(start * rate):int(end * rate)]
    return math.sqrt(sum(s * s for s in part) / max(1, len(part)))


def db(value: float) -> float:
    return 20.0 * math.log10(max(value, 1e-9))


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-mixing-check-"))
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
        rate = int(need({"cmd": "audio_settings"}, "audio settings").get("sampleRate", 48_000))

        def track_entry(name: str) -> dict:
            status = need({"cmd": "status"}, "status")
            for entry in status.get("audioTracks", []) + status.get("midiTracks", []):
                if entry.get("name") == name:
                    return entry
            raise RuntimeError(f"no track {name}")

        def keys(track: str) -> list[str]:
            return [p["key"] for p in need({"cmd": "list_parameters", "track": track}, "parameters")["parameters"]]

        def key_ending(track: str, suffix: str) -> str:
            found = [key for key in keys(track) if key.endswith(suffix)]
            if not found:
                raise RuntimeError(f"no parameter ending {suffix} on {track}")
            return found[-1]

        def node_of(answer: dict, kind: str) -> int:
            return [item["id"] for item in answer["nodes"] if item.get("type") == kind][-1]

        def export(name: str) -> tuple[list[float], int]:
            # The speakers are silenced by --silent: the mix is heard by the file alone.
            need({"cmd": "set_master_gain", "gainDb": 0.0, "muted": False}, "master up")
            path = scratch / f"{name}.wav"
            need({"cmd": "export_mix", "path": str(path)}, "export " + name)
            for _ in range(600):
                time.sleep(0.1)
                if not need({"cmd": "status"}, "status").get("exporting"):
                    break
            need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master down")
            return read_wav(path)

        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master")

        # ---- Latency compensation ----
        click_at = rate // 2
        click = [0.0] * (2 * rate)
        click[click_at] = 0.8
        half = [0.0] * (2 * rate)
        half[click_at] = -0.4
        write_wav(scratch / "click.wav", click, rate)
        write_wav(scratch / "half.wav", half, rate)
        late = need({"cmd": "add_audio_track", "file": str(scratch / "click.wav")}, "late track")["name"]
        dry = need({"cmd": "add_audio_track", "file": str(scratch / "half.wav")}, "dry track")["name"]
        need({"cmd": "insert_device", "track": late, "key": "comp-pro"}, "compressor")
        # A pure delay: ratio 1 compresses nothing, the look-ahead holds it back.
        need({"cmd": "set_parameter", "track": late, "key": key_ending(late, "comppro.ratio"), "value": 1.0}, "ratio")
        need({"cmd": "set_parameter", "track": late, "key": key_ending(late, "comppro.lookahead"), "value": 10.0}, "look-ahead")
        expected = round(0.010 * rate)
        status = {}
        for _ in range(30):
            time.sleep(0.1)
            status = need({"cmd": "status"}, "status")
            if status.get("latencyCompensationSamples") == expected:
                break
        late_entry = track_entry(late)
        dry_entry = track_entry(dry)
        check(late_entry.get("latencySamples") == expected,
              f"a 10 ms look-ahead makes its track {expected} samples late ({late_entry.get('latencySamples')})")
        check(dry_entry.get("compensationSamples") == expected and late_entry.get("compensationSamples") == 0,
              f"the other track waits as long ({dry_entry.get('compensationSamples')}), the late one not at all")
        check(status.get("latencyCompensationSamples") == expected,
              f"the mix is that late ({status.get('latencyCompensationSamples')})")
        mix, mix_rate = export("latency")
        peak_at = max(range(len(mix)), key=lambda index: abs(mix[index]))
        peak = mix[peak_at]
        rest = max((abs(value) for index, value in enumerate(mix) if abs(index - peak_at) > 2), default=0.0)
        check(peak > 0.0 and rest < 0.1 * abs(peak),
              f"the click and its inverted half meet: one click of half height ({peak:.3f}; elsewhere {rest:.4f})")
        check(abs(peak_at - click_at) <= 1,
              f"the export skips the latency: the click is where the song has it ({peak_at} for {click_at})")
        need({"cmd": "set_parameter", "track": late, "key": key_ending(late, "comppro.lookahead"), "value": 0.0}, "no look-ahead")
        for _ in range(30):
            time.sleep(0.1)
            if track_entry(dry).get("compensationSamples") == 0:
                break
        check(track_entry(late).get("latencySamples") == 0 and track_entry(dry).get("compensationSamples") == 0,
              "no look-ahead: no latency, no wait")

        # ---- Sidechain ----
        need({"cmd": "new_project"}, "new")
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master")
        bass = [0.25 * math.sin(2 * math.pi * 110.0 * n / rate) for n in range(3 * rate)]
        kick = [0.0 if n < int(1.5 * rate) else 0.8 * math.sin(2 * math.pi * 60.0 * n / rate) for n in range(3 * rate)]
        write_wav(scratch / "bass.wav", bass, rate)
        write_wav(scratch / "kick.wav", kick, rate)
        bass_track = need({"cmd": "add_audio_track", "file": str(scratch / "bass.wav")}, "bass")["name"]
        kick_track = need({"cmd": "add_audio_track", "file": str(scratch / "kick.wav")}, "kick")["name"]
        # Not in the mix: its fader down. It sends before its fader.
        need({"cmd": "set_track", "track": kick_track, "gainDb": -60.0}, "kick down")
        need({"cmd": "insert_device", "track": bass_track, "key": "comp-pro"}, "compressor")
        for suffix, value in (("comppro.threshold", -40.0), ("comppro.ratio", 20.0), ("comppro.knee", 0.0),
                              ("comppro.attack", 0.05)):
            need({"cmd": "set_parameter", "track": bass_track, "key": key_ending(bass_track, suffix), "value": value}, suffix)
        chosen = need({"cmd": "sidechain", "track": bass_track, "source": kick_track}, "sidechain")
        check(chosen.get("source") == kick_track and chosen.get("input", 0) != 0,
              f"the compressor's sidechain listens to the kick ({chosen.get('source')})")
        mix, mix_rate = export("sidechain")
        before = rms(mix, mix_rate, 0.3, 1.3)
        after = rms(mix, mix_rate, 2.0, 2.9)
        check(before > 0.05, f"while the kick is silent the bass passes whole, though alone it would be compressed ({db(before):.1f} dB)")
        check(after < before * 0.1, f"the kick pulls the bass down by 20 dB and more ({db(before):.1f} -> {db(after):.1f} dB)")
        project = scratch / "Sidechain.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        kept = need({"cmd": "sidechain", "track": bass_track}, "sidechain kept")
        check(kept.get("source") == kick_track, f"a project keeps the sidechain ({kept.get('source')})")
        cleared = need({"cmd": "sidechain", "track": bass_track, "clear": True}, "clear")
        check("source" not in cleared, "clearing it, the detector hears the bass again")
        loop = request({"cmd": "sidechain", "track": bass_track, "source": bass_track})
        check(not loop.get("ok"), "a track cannot be its own sidechain")

        # ---- Automation of Prisme's own settings ----
        need({"cmd": "new_project"}, "new")
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master")
        need({"cmd": "set_tempo", "bpm": 120}, "tempo")
        synth = need({"cmd": "add_midi_track", "name": "Prisme", "instrument": "prism-instrument"}, "prisme")["name"]
        need({"cmd": "add_midi_clip", "track": synth, "name": "held", "startBeats": 0, "lengthBeats": 8}, "clip")
        need({"cmd": "add_notes", "track": synth, "clip": "held", "notes": [{"note": 57, "start": 0, "length": 8, "velocity": 0.9}]},
             "note")
        volume = key_ending(synth, "prism.voice.master")
        lane = request({"cmd": "add_automation", "track": synth, "key": volume,
                        "points": [{"time": 0.0, "value": 0.0}, {"time": 1.99, "value": 0.0}, {"time": 2.0, "value": 1.0}]})
        check(bool(lane.get("ok")), f"Prisme's volume takes a lane ({lane.get('error', 'ok')})")
        mix, mix_rate = export("prisme")
        silent = rms(mix, mix_rate, 0.3, 1.8)
        loud = rms(mix, mix_rate, 2.3, 3.8)
        check(silent < 1e-3 and loud > 0.01,
              f"the lane is heard: silent, then playing ({db(silent):.1f} -> {db(loud):.1f} dB)")

        # ---- Automation inside a module ----
        need({"cmd": "new_project"}, "new")
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master")
        tone_track = need({"cmd": "add_audio_track", "file": str(scratch / "bass.wav")}, "tone")["name"]
        gain = node_of(need({"cmd": "insert_device", "track": tone_track, "key": "gain"}, "gain"), "gain")
        made = need({"cmd": "create_device", "track": tone_track, "nodeIds": [gain], "name": "Gain du test"}, "module")
        check(made.get("nodeId", 0) != 0, "the gain card becomes a module")
        inner = [key for key in keys(tone_track) if ".node." in key and key.endswith("gain.db")]
        check(len(inner) == 1, f"its knob is published from inside it ({inner})")
        if inner:
            lane = request({"cmd": "add_automation", "track": tone_track, "key": inner[0],
                            "points": [{"time": 0.0, "value": -60.0}, {"time": 1.49, "value": -60.0},
                                       {"time": 1.5, "value": 0.0}]})
            check(bool(lane.get("ok")), f"the knob inside the module takes a lane ({lane.get('error', 'ok')})")
            mix, mix_rate = export("module")
            quiet = rms(mix, mix_rate, 0.3, 1.3)
            full = rms(mix, mix_rate, 1.8, 2.8)
            check(full > 0.05 and full > quiet * 300,
                  f"the lane inside the module is heard ({db(quiet):.1f} -> {db(full):.1f} dB)")

        # ---- A module in an instrument's chain (the other compiler) ----
        need({"cmd": "new_project"}, "new")
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master")
        need({"cmd": "set_tempo", "bpm": 120}, "tempo")
        keys_track = need({"cmd": "add_midi_track", "name": "Clavier"}, "keys")["name"]
        need({"cmd": "insert_device", "track": keys_track, "key": "synth-keys"}, "instrument")
        need({"cmd": "add_midi_clip", "track": keys_track, "name": "held", "startBeats": 0, "lengthBeats": 8}, "clip")
        need({"cmd": "add_notes", "track": keys_track, "clip": "held",
              "notes": [{"note": 57, "start": 0, "length": 8, "velocity": 0.9}]}, "note")
        gain = node_of(need({"cmd": "insert_device", "track": keys_track, "key": "gain"}, "gain"), "gain")
        module = need({"cmd": "create_device", "track": keys_track, "nodeIds": [gain], "name": "Gain du clavier"}, "module")
        # The instrument is a module too: its own gain plays in its voices, which no lane reaches.
        inner = [key for key in keys(keys_track) if key.startswith(f"node.{module.get('nodeId')}.node.") and key.endswith("gain.db")]
        check(len(inner) == 1, f"the module in the instrument's chain publishes its knob ({inner})")
        voice_gain = [key for key in keys(keys_track) if ".node." in key and key.endswith("gain.db") and key not in inner]
        if voice_gain:
            refused = request({"cmd": "add_automation", "track": keys_track, "key": voice_gain[0],
                               "points": [{"time": 0.0, "value": -6.0}]})
            check(not refused.get("ok"), "a knob inside the instrument's voice section says it takes no lane")
        if inner:
            mix, mix_rate = export("instrument-open")
            opened = rms(mix, mix_rate, 0.5, 3.5)
            need({"cmd": "set_parameter", "track": keys_track, "key": inner[0], "value": -40.0}, "knob down")
            mix, mix_rate = export("instrument-down")
            lowered = rms(mix, mix_rate, 0.5, 3.5)
            check(opened > 0.01 and abs(db(opened) - db(lowered) - 40.0) < 1.5,
                  f"the knob inside a module of an instrument's chain is heard ({db(opened):.1f} -> {db(lowered):.1f} dB)")
            lane = request({"cmd": "add_automation", "track": keys_track, "key": inner[0],
                            "points": [{"time": 0.0, "value": -60.0}, {"time": 1.99, "value": -60.0},
                                       {"time": 2.0, "value": 0.0}]})
            check(bool(lane.get("ok")), f"and takes a lane ({lane.get('error', 'ok')})")
            mix, mix_rate = export("instrument-lane")
            quiet = rms(mix, mix_rate, 0.5, 1.8)
            full = rms(mix, mix_rate, 2.3, 3.8)
            check(full > 0.01 and full > quiet * 300, f"which is heard ({db(quiet):.1f} -> {db(full):.1f} dB)")
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        try:
            request({"cmd": "quit"})
        except OSError:
            pass
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    if failures:
        print(f"{len(failures)} failure(s)")
        return 1
    print("all mixing checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
