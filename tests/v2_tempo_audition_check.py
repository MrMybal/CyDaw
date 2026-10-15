"""Loops heard at the project's tempo from the sound library. Silent (master at -60 dB, speakers off).

    python tests/v2_tempo_audition_check.py --exe "<CyDAW 2.exe>" [--port 47848]

A folder of the test's own holds a drum loop named for its tempo (four beats at 100 BPM, a click on each
beat) and a one-shot. The library reads them; then:

* the loop heard at the tempo: four beats, from 100 to the project's 120 BPM; the one-shot as it is;
* the transport playing, the loop comes in on its beat: where it is in its four beats is where the
  transport is (as heard, after the mix's latency);
* the playhead moved: it comes back in step;
* the tempo moved while it plays: it follows, still in step;
* the switch off: the loop as it is.

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
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47848


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


def write_clicks(path: Path, beats: int, bpm: float, rate: int = 48_000) -> None:
    frames = int(round(beats * 60.0 / bpm * rate))
    beat = 60.0 / bpm * rate
    data = bytearray()
    for index in range(frames):
        since = index % beat
        value = 0.8 * math.exp(-since / 120.0) * math.sin(2 * math.pi * 3000.0 * since / rate) if since < 1200 else 0.0
        sample = int(round(value * 32767))
        data += struct.pack("<hh", sample, sample)
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(data))


def wait_for(condition, seconds: float) -> bool:
    end = time.time() + seconds
    while time.time() < end:
        if condition():
            return True
        time.sleep(0.2)
    return condition()


def phase_error(state: dict) -> float:
    """How far the loop is from where the transport says it should be, in beats (-2..2)."""
    beats = state["loopBeats"]
    heard = (state["transportPosition"] - state["latencySeconds"]) * state["tempo"] / 60.0
    expected = heard % beats
    loop_seconds = beats * 60.0 / 100.0
    actual = state["position"] / loop_seconds * beats
    error = (actual - expected) % beats
    return error - beats if error > beats / 2 else error


def scenario(scratch: Path) -> None:
    if not need({"cmd": "status"}, "status").get("speakersSilenced"):
        raise RuntimeError("the session is not silent")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new project")
    need({"cmd": "set_tempo", "bpm": 120}, "tempo")
    folder = scratch / "Loops"
    folder.mkdir()
    loop = folder / "Drum Loop 100bpm.wav"
    write_clicks(loop, 4, 100.0)
    shot = folder / "Kick One Shot.wav"
    write_clicks(shot, 1, 240.0)
    need({"cmd": "sample_folders", "includeBanks": False}, "no banks")
    need({"cmd": "sample_folders", "add": str(folder)}, "folder")
    check(wait_for(lambda: not need({"cmd": "samples_status"}, "status").get("scanning")
                   and need({"cmd": "samples_status"}, "status").get("samples", 0) >= 2, 60), "the library reads the folder")

    # ---- At the tempo, stopped ----
    heard = need({"cmd": "preview_sample", "path": str(loop), "atTempo": True}, "loop")
    check(heard.get("loopBeats") == 4 and abs(heard.get("fromBpm", 0) - 100.0) < 0.5 and abs(heard.get("tempo", 0) - 120.0) < 1e-6,
          f"the loop is heard at the tempo: four beats, 100 -> 120 BPM ({heard.get('loopBeats')}, {heard.get('fromBpm')})")
    time.sleep(0.6)
    playing = need({"cmd": "preview_sample", "query": True}, "query")
    check(playing.get("previewing") and 0.0 < playing.get("position", 0) < 2.4, "it plays round its loop")
    plain = need({"cmd": "preview_sample", "path": str(shot), "atTempo": True}, "one-shot")
    check(plain.get("loopBeats") == 0 and plain.get("previewing"), "a one-shot is heard as it is")
    need({"cmd": "preview_sample", "stop": True}, "stop")

    # ---- In step with the transport ----
    need({"cmd": "transport", "action": "play", "position": 0.37}, "play")
    time.sleep(0.5)
    need({"cmd": "preview_sample", "path": str(loop), "atTempo": True}, "loop in step")
    time.sleep(0.5)
    errors = []
    for _ in range(5):
        state = need({"cmd": "preview_sample", "query": True}, "query")
        errors.append(phase_error(state))
        time.sleep(0.15)
    check(all(abs(error) < 0.06 for error in errors),
          f"it comes in on the transport's beat and stays there ({', '.join(f'{e:+.3f}' for e in errors)} beat)")

    # ---- The playhead jumps: it comes back in step ----
    need({"cmd": "transport", "action": "play", "position": 5.123}, "jump")
    time.sleep(0.8)
    jumped = phase_error(need({"cmd": "preview_sample", "query": True}, "query"))
    check(abs(jumped) < 0.06, f"the playhead moved, it comes back in step ({jumped:+.3f} beat)")

    # ---- The tempo moves ----
    need({"cmd": "set_tempo", "bpm": 90}, "slower")
    time.sleep(0.6)
    moved = need({"cmd": "preview_sample", "query": True}, "query")
    error = phase_error(moved)
    check(moved.get("loopBeats") == 4 and abs(moved.get("tempo", 0) - 90.0) < 1e-6 and abs(error) < 0.06,
          f"the tempo moved, it follows in step ({error:+.3f} beat)")
    need({"cmd": "transport", "action": "stop"}, "stop transport")

    # ---- The switch off ----
    off = need({"cmd": "preview_sample", "path": str(loop), "atTempo": False}, "as it is")
    check(off.get("loopBeats") == 0 and off.get("previewing"), "the switch off: the loop as it is")
    need({"cmd": "preview_sample", "stop": True}, "stop")
    check(not need({"cmd": "preview_sample", "query": True}, "query").get("previewing"), "stopped, nothing plays")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-tempo-audition-"))
    (scratch / "Banks").mkdir()
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "banks": str(scratch / "Banks"), "cache": str(scratch / "Cache"), "presets": str(scratch / "Presets")}}),
        encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations),
                       CYDAW_SAMPLES_SETTINGS=str(scratch / "samples.settings"))
    environment.pop("CYDAW_BANKS_DIR", None)
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
        scenario(scratch)
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL", error, flush=True)
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
