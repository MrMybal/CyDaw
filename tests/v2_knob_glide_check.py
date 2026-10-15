"""A knob turned at once is heard as a slope, not a click. Silent (speakers off).

    python tests/v2_knob_glide_check.py --exe "<CyDAW 2.exe>" [--port 47850]

A synth holds a sine while its level jumps between two values, twenty
times a second, as a dragged knob sends it; the mix is exported as it plays.
In the file the wave never breaks: the jumps were spread over a few
milliseconds.

Exit code 0 when it holds.
"""
from __future__ import annotations

import argparse
import array
import json
import math
import os
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47850


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


def read_mono(path: Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as source:
        rate, frames, channels, width = source.getframerate(), source.getnframes(), source.getnchannels(), source.getsampwidth()
        raw = source.readframes(frames)
    if width == 3:
        values = [int.from_bytes(raw[i:i + 3] + (b"\xff" if raw[i + 2] & 0x80 else b"\x00"), "little", signed=True) / 8388608.0
                  for i in range(0, len(raw) - 2, 3 * channels)]
    elif width == 4:
        floats = array.array("f", raw)
        values = [floats[i] for i in range(0, len(floats), channels)]
    else:
        shorts = array.array("h", raw)
        values = [shorts[i] / 32768.0 for i in range(0, len(shorts), channels)]
    return values, rate


def scenario(scratch: Path) -> None:
    if not need({"cmd": "status"}, "status").get("speakersSilenced"):
        raise RuntimeError("the session is not silent")
    need({"cmd": "new_project"}, "new project")
    # The speakers are off (--silent); the export goes through the master,
    # which must not turn it down to its last bits.
    need({"cmd": "set_master_gain", "gainDb": 0, "muted": False}, "master")
    if not need({"cmd": "status"}, "status").get("speakersSilenced"):
        raise RuntimeError("the session is not silent")
    need({"cmd": "set_tempo", "bpm": 120}, "tempo")
    need({"cmd": "add_midi_track", "name": "Synth"}, "synth")
    need({"cmd": "insert_device", "track": "Synth", "key": "synth-keys"}, "instrument")
    # A plain sine through a chain left neutral: a jump of its level would
    # show as a break in the wave.
    keys = [p["key"] for p in need({"cmd": "list_parameters", "track": "Synth"}, "parameters")["parameters"]]
    settings = {"polyOscillator.waveform": 0, "envelope.attack": 5, "envelope.sustain": 1, "voiceFilter.cutoff": 20000,
                "voiceFilter.resonance": 0.7, "compressor.ratio": 1, "stereo.width": 1, "eq.tone": 0, "clip.threshold": 1}
    for key in keys:
        for suffix, value in settings.items():
            if key.endswith(suffix):
                need({"cmd": "set_parameter", "track": "Synth", "key": key, "value": value}, suffix)
    level = next(key for key in keys if key.endswith("polyOscillator.level"))
    need({"cmd": "set_parameter", "track": "Synth", "key": level, "value": 0.05}, "level")
    need({"cmd": "add_midi_clip", "track": "Synth", "name": "Note", "startBeats": 0, "lengthBeats": 12, "looping": False}, "clip")
    need({"cmd": "add_notes", "track": "Synth", "clip": "Note", "notes": [{"note": 57, "start": 0, "length": 12, "velocity": 0.8}]},
         "note")

    def export(name: str, turning: bool) -> tuple[list[float], int]:
        path = scratch / name
        need({"cmd": "set_parameter", "track": "Synth", "key": level, "value": 0.2}, "level")
        need({"cmd": "export_mix", "path": str(path).replace(os.sep, "/")}, "export")
        stop = threading.Event()

        def turn() -> None:
            # From one level to a quarter of it and back, twenty times a
            # second: what a fast drag sends, each value a jump.
            time.sleep(1.0)
            low = False
            while not stop.is_set():
                low = not low
                try:
                    request({"cmd": "set_parameter", "track": "Synth", "key": level, "value": 0.05 if low else 0.2})
                except OSError:
                    return
                time.sleep(0.05)

        turner = threading.Thread(target=turn)
        if turning:
            turner.start()
        try:
            for _ in range(600):
                time.sleep(0.1)
                if not need({"cmd": "status"}, "status").get("exporting"):
                    break
        finally:
            stop.set()
            if turning:
                turner.join()
        if os.environ.get("CYDAW_KEEP_EXPORT"):
            shutil.copy(path, Path(os.environ["CYDAW_KEEP_EXPORT"]) / name)
        return read_mono(path)

    def harshest(values: list[float], rate: int) -> tuple[float, float]:
        """The most of its height a 10 ms stretch has in its highest frequencies (its second difference), and how far
        the stretches' levels spread."""
        region = values[int(rate * 1.5):int(rate * 5.5)]
        size = rate // 100
        worst, levels = 0.0, []
        for at in range(0, len(region) - size, size):
            piece = region[at:at + size]
            whole = math.sqrt(sum(v * v for v in piece) / size)
            edge = math.sqrt(sum((piece[i] - 2.0 * piece[i - 1] + piece[i - 2]) ** 2 for i in range(2, size)) / size)
            if whole > 1.0e-4:
                worst = max(worst, edge / whole)
                levels.append(whole)
        spread = 20.0 * math.log10(max(levels) / min(levels)) if levels else 0.0
        return worst, spread

    steady, rate = export("steady.wav", False)
    turned, _ = export("turned.wav", True)
    check(len(turned) > rate * 3 and len(steady) > rate * 3, f"the mix was exported twice ({len(turned) / max(1, rate):.1f} s)")
    calm, _ = harshest(steady, rate)
    moved, spread = harshest(turned, rate)
    check(spread > 6.0, f"the level did move while it played ({spread:.1f} dB between the extremes)")
    # A jump breaks the wave: the stretch it falls in gets much brighter. A
    # slope leaves each stretch as bright as the held sound's.
    check(moved < calm * 1.5, f"each jump is heard as a slope, not a click (brightness {moved:.4f} against {calm:.4f} held)")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-glide-"))
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1")
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
