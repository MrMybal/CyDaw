"""The window of a plug-in exported from CyDAW: CyDAW's own interface of its
device, never the graph. Silent; nothing on screen.

    python tests/v2_plugin_window_check.py --exe "<CyDAW 2.exe>" --host <cydaw_graph_runtime_host> \\
        --plugin "<CyDAW Graph Runtime.vst3>" [--port 47853]

CyDAW (silent, its own presets folder) writes the state an export embeds
for a few devices; the test host loads the runtime plug-in every export is
built from, restores that state, opens its window off the screen while a
note and a sine go through it, and the plug-in writes one frame of it
(CYDAW_PLUGIN_SNAPSHOT).

* Prisme, Jade and Kick open on their own windows (their sizes: 1240 x 800,
  1180 x 780, 1080 x 700 at 100 %), Réverbe Pro and a chain of effects on
  their large views (1100 x 700) — never the 720 x 420 window made from the
  published settings.
* Each frame is drawn (not one colour), and the host closes the window,
  releases the plug-in and exits by itself (a plug-in that hangs its host
  when unloaded fails).

Exit code 0 when all hold.
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
import zlib
from pathlib import Path

failures: list[str] = []
port = 47853
ROOT = Path(__file__).resolve().parents[1]


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


def png_facts(path: Path) -> tuple[int, int, int]:
    """Its width, height and how many different bytes its first rows hold."""
    data = path.read_bytes()
    width, height = struct.unpack(">II", data[16:24])
    # The image data: every IDAT chunk, inflated.
    at, chunks = 8, b""
    while at < len(data):
        length = struct.unpack(">I", data[at:at + 4])[0]
        kind = data[at + 4:at + 8]
        if kind == b"IDAT":
            chunks += data[at + 8:at + 8 + length]
        at += 12 + length
    raw = zlib.decompress(chunks)
    return width, height, len(set(raw[:min(len(raw), width * 4 * 200)]))


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--host", required=True)
    parser.add_argument("--plugin", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-plugin-window-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1400x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    states: dict[str, Path] = {}
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
        loop = sorted((ROOT / "templates" / "audio").glob("*.wav"))[0]
        devices = {
            "prisme": ("prism-instrument", None),
            "jade": ("jade-instrument", None),
            "kick": ("kick-instrument", None),
            "reverb": (None, ["reverb-pro"]),
            "chain": (None, ["fournaise", "echo-bande", "halo"]),
        }
        for name, (instrument, effects) in devices.items():
            need({"cmd": "new_project"}, "new")
            if instrument:
                track = need({"cmd": "add_midi_track", "name": name, "instrument": instrument}, name)["name"]
            else:
                track = need({"cmd": "add_audio_track", "file": str(loop)}, name)["name"]
                for effect in effects:
                    need({"cmd": "insert_device", "track": track, "key": effect}, effect)
            state = scratch / f"{name}.json"
            # The state is written first; the check after it may fail for an
            # audio track (it plays a note through an instrument).
            request({"cmd": "instrument_export_check", "track": track, "stateOut": str(state).replace(os.sep, "/")})
            check(state.exists() and state.stat().st_size > 100, f"{name}: the state an export embeds is written")
            states[name] = state
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:  # noqa: BLE001
            process.kill()

    sizes = {"prisme": (1240, 800), "jade": (1180, 780), "kick": (1080, 700), "reverb": (1100, 700), "chain": (1100, 700)}
    for name, state in states.items():
        if not state.exists():
            continue
        frame = scratch / f"{name}.png"
        env = dict(os.environ, CYDAW_PLUGIN_SNAPSHOT=str(frame))
        started = time.time()
        try:
            run = subprocess.run([args.host, args.plugin, str(state), "60", "editor", str(frame)], env=env,
                                 capture_output=True, text=True, timeout=90)
            exited, code = True, run.returncode
        except subprocess.TimeoutExpired:
            exited, code = False, -1
        check(exited and code == 0, f"{name}: the window is drawn and the host exits by itself "
                                    f"({'exit ' + str(code) if exited else 'still running after 90 s'}, {time.time() - started:.1f} s)")
        if not frame.exists():
            check(False, f"{name}: a frame of its window")
            continue
        width, height, variety = png_facts(frame)
        expected = sizes[name]
        # Its own size times the display's scale: the proportions tell.
        scale = width / expected[0]
        check(abs(height - expected[1] * scale) <= 2 and scale >= 0.99,
              f"{name}: CyDAW's own window ({width} x {height}, {expected[0]} x {expected[1]} expected at 100 %)")
        check(variety > 12, f"{name}: the frame is drawn ({variety} different values)")
    shutil.rmtree(scratch, ignore_errors=True)
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
