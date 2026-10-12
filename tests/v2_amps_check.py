"""The amp, cabinet and microphone simulators in the software. Silent, offline.

    python tests/v2_amps_check.py --exe "<CyDAW 2.exe>" [--port 47840]

The presets folder is the test's own (CYDAW_LOCATIONS_FILE). Each check renders the track's
effects as an exported plug-in gets them (effect_chain_check: the state written, restored,
compiled, a sine through it offline): nothing is played.

* Ampli, Baffle et micro and Micro go on an audio track; each has its factory presets.
* Ampli: a high-gain preset distorts more than a clean one; the treble knob raises the highs.
* Baffle et micro: the cabinet keeps the highs out. An impulse response loaded from a file plays
  instead of the models: a single click passes the sound as it is (-6 dB); a click and an echo
  300 samples later comb it as the arithmetic says (the echo falls in the convolver's FFT part, the
  click in its direct part); a project and a preset keep the response; taking it out brings the
  models back.
* Micro: the telephone keeps its narrow band.
* A saved project opened again keeps their settings.

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
from pathlib import Path

failures: list[str] = []
port = 47840
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


def write_wav(path: Path, samples: list[float], rate: int = 48_000) -> None:
    data = b"".join(struct.pack("<h", int(round(max(-1.0, min(1.0, s)) * 32767))) for s in samples)
    header = struct.pack("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len(data), b"WAVE", b"fmt ", 16, 1, 1, rate, rate * 2, 2, 16,
                         b"data", len(data))
    path.write_bytes(header + data)


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-amps-check-"))
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
        loop = sorted((ROOT / "templates" / "audio").glob("*.wav"))[0]
        track = need({"cmd": "add_audio_track", "file": str(loop)}, "track")["name"]

        def chain(hz: float, amplitude: float = 0.3) -> dict:
            return need({"cmd": "effect_chain_check", "track": track, "hz": hz, "amplitude": amplitude, "seconds": 0.6}, "chain")

        def node_of(answer: dict, kind: str) -> int:
            return [item["id"] for item in answer["nodes"] if item.get("type") == kind][-1]

        def load(node: int, name: str) -> None:
            need({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": name}, f"preset {name}")

        def factory(node: int) -> list[str]:
            listed = need({"cmd": "presets", "track": track, "nodeId": node}, "presets")
            return [item["name"] for item in listed.get("presets", []) if item.get("factory")]

        # ---- Ampli ----
        amp = node_of(need({"cmd": "insert_device", "track": track, "key": "amp-sim"}, "amp"), "amp-sim")
        names = factory(amp)
        check(len(names) >= 12 and "Métal moderne" in names, f"the amp's factory presets ({len(names)})")
        load(amp, "Clean jazz")
        clean = chain(220.0)
        load(amp, "Métal moderne")
        metal = chain(220.0)
        check(metal["thirdDb"] > clean["thirdDb"] + 10.0,
              f"high gain distorts more than clean (3rd harmonic {clean['thirdDb']:.1f} -> {metal['thirdDb']:.1f} dB)")
        check(-60.0 < metal["outputDb"] < 0.0 and metal["peakDb"] < 3.0, f"the amp's level is sane ({metal['outputDb']:.1f} dB)")
        load(amp, "Clean jazz")
        keys = {p["key"].split(".", 2)[-1]: p["key"] for p in need({"cmd": "list_parameters", "track": track}, "keys")["parameters"]}
        need({"cmd": "set_parameter", "track": track, "key": keys["amp.treble"], "value": 0.0}, "treble")
        dark = chain(3_000.0, 0.02)
        need({"cmd": "set_parameter", "track": track, "key": keys["amp.treble"], "value": 10.0}, "treble")
        bright = chain(3_000.0, 0.02)
        check(bright["outputDb"] > dark["outputDb"] + 6.0, f"the treble knob raises the highs ({dark['outputDb']:.1f} -> {bright['outputDb']:.1f} dB)")
        # Two channels: A clean, B the lead; the switch is a value as the others, a lane moves it.
        load(amp, "Clair et lead A-B")
        on_a = chain(220.0)
        need({"cmd": "set_parameter", "track": track, "key": keys["amp.channel"], "value": 1.0}, "channel B")
        on_b = chain(220.0)
        check(on_b["thirdDb"] > on_a["thirdDb"] + 10.0,
              f"channel B plays its lead (3rd harmonic {on_a['thirdDb']:.1f} -> {on_b['thirdDb']:.1f} dB)")
        need({"cmd": "set_parameter", "track": track, "key": keys["amp.channel"], "value": 0.0}, "channel A")
        lane = request({"cmd": "add_automation", "track": track, "key": keys["amp.channel"],
                        "points": [{"time": 0.0, "value": 0.0}, {"time": 8.0, "value": 1.0}]})
        check(lane.get("ok") is True, f"a lane switches the channel ({lane.get('error', '')})")
        need({"cmd": "clear_automation", "track": track, "key": keys["amp.channel"]}, "clear the lane")
        # The cabinet's checks below hear it through the bright clean as before.
        load(amp, "Clean jazz")
        need({"cmd": "set_parameter", "track": track, "key": keys["amp.treble"], "value": 10.0}, "treble")

        # ---- Baffle et micro ----
        cab = node_of(need({"cmd": "insert_device", "track": track, "key": "cab-sim"}, "cab"), "cab-sim")
        check(len(factory(cab)) >= 8, "the cabinet's factory presets")
        load(cab, "4x12 classique")
        low = chain(1_000.0, 0.02)
        high = chain(9_000.0, 0.02)
        check(high["outputDb"] < low["outputDb"] - 15.0,
              f"the cabinet keeps the highs out (1 kHz {low['outputDb']:.1f}, 9 kHz {high['outputDb']:.1f} dB)")

        # ---- Baffle et micro: an impulse response ----
        need({"cmd": "remove_device", "track": track, "nodeId": amp}, "remove amp")
        keys = {p["key"].split(".", 2)[-1]: p["key"] for p in need({"cmd": "list_parameters", "track": track}, "keys")["parameters"]}
        for key, value in (("cab.room", 0.0), ("cab.lowCut", 20.0), ("cab.highCut", 20_000.0), ("cab.output", 0.0), ("cab.mix", 1.0)):
            need({"cmd": "set_parameter", "track": track, "key": keys[key], "value": value}, key)
        dirac = scratch / "click.wav"
        echo = scratch / "click-and-echo.wav"
        write_wav(dirac, [1.0] + [0.0] * 1023)
        write_wav(echo, [0.5] + [0.0] * 299 + [1.0] + [0.0] * 723)
        held = need({"cmd": "cab_impulse", "track": track, "nodeId": cab, "file": str(dirac)}, "load the click")
        check(held.get("name") == "click" and held.get("frames") == 1, f"the click is loaded, its silence trimmed ({held})")
        plain = {hz: chain(hz, 0.05) for hz in (220.0, 1_000.0)}
        through = plain[1_000.0]["outputDb"] - plain[1_000.0]["inputDb"]
        check(abs(through + 6.02) < 0.6, f"a click passes the sound as it is, -6 dB ({through:.2f} dB)")
        need({"cmd": "cab_impulse", "track": track, "nodeId": cab, "file": str(echo)}, "load the echo")
        scale = 1.0 / math.sqrt(1.25)
        for hz in (220.0, 1_000.0):
            theta = 2.0 * math.pi * hz * 300.0 / 48_000.0
            expected = 20.0 * math.log10(abs(complex(0.5 + math.cos(theta), -math.sin(theta))) * scale)
            measured = chain(hz, 0.05)["fundamentalDb"] - plain[hz]["fundamentalDb"]
            check(abs(measured - expected) < 0.5, f"a click and its echo comb {hz:.0f} Hz as computed "
                                                  f"({measured:.2f} dB, expected {expected:.2f})")
        project = scratch / "Cabinet.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        reopened = {}
        for _ in range(150):
            answer = request({"cmd": "cab_impulse", "track": track, "nodeId": cab})
            if answer.get("ok"):
                reopened = answer["result"]
                break
            time.sleep(0.2)
        check(reopened.get("name") == "click-and-echo" and reopened.get("frames") == 301,
              f"a saved project keeps the response ({reopened})")
        saved = need({"cmd": "presets", "track": track, "nodeId": cab, "action": "save", "name": "Mon IR"}, "save preset")
        need({"cmd": "cab_impulse", "track": track, "nodeId": cab, "clear": True}, "clear")
        modelled = chain(9_000.0, 0.02)
        check(modelled["outputDb"] < low["outputDb"] - 10.0 and not need({"cmd": "cab_impulse", "track": track, "nodeId": cab},
                                                                          "held").get("name"),
              "taking the response out brings the modelled cabinet back")
        load(cab, "Mon IR")
        check(need({"cmd": "cab_impulse", "track": track, "nodeId": cab}, "held").get("name") == "click-and-echo",
              f"a preset brings its response back ({saved.get('current')})")
        need({"cmd": "remove_device", "track": track, "nodeId": cab}, "remove cab")

        # ---- Micro ----
        mic = node_of(need({"cmd": "insert_device", "track": track, "key": "mic-sim"}, "mic"), "mic-sim")
        check(len(factory(mic)) >= 10, "the microphone's factory presets")
        load(mic, "Téléphone")
        band = chain(1_000.0, 0.05)
        under = chain(100.0, 0.05)
        check(under["outputDb"] < band["outputDb"] - 20.0,
              f"the telephone keeps its narrow band (1 kHz {band['outputDb']:.1f}, 100 Hz {under['outputDb']:.1f} dB)")

        # ---- A project keeps them ----
        load(mic, "Ruban doux")
        project = scratch / "Amps.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        kept = {}
        for _ in range(150):
            answer = request({"cmd": "list_parameters", "track": track})
            if answer.get("ok"):
                kept = {p["key"].split(".", 2)[-1]: p["value"] for p in answer["result"]["parameters"]}
                break
            time.sleep(0.2)
        check(kept.get("micsim.model") == 2.0 and kept.get("micsim.distance") == 25.0, "a saved project keeps the microphone")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print("FAILED: " + "; ".join(failures) if failures else "All amp, cabinet and microphone checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
