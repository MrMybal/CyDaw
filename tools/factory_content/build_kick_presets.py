"""Kick's factory sounds: each set through CyDAW 2's control port on a Kick
card, measured offline (no sound out), brought to -1 dBFS at its peak, saved as
a device preset and copied into content/factory/Presets/device/kick-synth.

    python tools/factory_content/build_kick_presets.py [--exe "<CyDAW 2.exe>"] [--port 47837]

A sound is a few settings over the defaults ("init" first): keys as in
KickSynth.h ("pitchStart", "decay"); a choice by its English name ("Noise").
Every sound plays as set on C2 (the note tunes it, key tracking on).
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
TARGET = ROOT / "content" / "factory" / "Presets" / "device" / "kick-synth"
port = 47837

# Where each sound's peak lands.
PEAK_DB = -1.0

SOUNDS = {
    "Init": {},
    "808 Long": {
        "pitchStart": 120, "pitchEnd": 49, "pitchTime": 60, "pitchCurve": 2.0,
        "attack": 0.2, "hold": 0, "decay": 1_900, "decayCurve": 1.2, "length": 3_000,
        "clickType": "Click", "clickLevel": -16, "clickDecay": 2, "clickTone": 3_000,
        "driveType": "Soft", "drive": 3,
    },
    "808 Short": {
        "pitchStart": 130, "pitchEnd": 52, "pitchTime": 50, "pitchCurve": 2.0,
        "attack": 0.2, "hold": 0, "decay": 480, "decayCurve": 1.4, "length": 1_000,
        "clickType": "Click", "clickLevel": -16, "clickDecay": 2, "clickTone": 3_000,
    },
    "909 Punch": {
        "pitchStart": 380, "pitchEnd": 55, "pitchTime": 45, "pitchCurve": 3.0,
        "hold": 20, "decay": 300, "decayCurve": 1.6,
        "clickType": "Noise", "clickLevel": -8, "clickDecay": 8, "clickTone": 3_500,
        "driveType": "Soft", "drive": 4,
    },
    "House Classic": {
        "pitchStart": 300, "pitchEnd": 55, "pitchTime": 80, "pitchCurve": 4.0,
        "hold": 25, "decay": 350, "decayCurve": 1.8,
        "clickType": "Noise", "clickLevel": -12, "clickDecay": 5, "clickTone": 4_000,
        "driveType": "Soft", "drive": 2,
    },
    "Deep House": {
        "pitchStart": 200, "pitchEnd": 48, "pitchTime": 120, "pitchCurve": 3.0,
        "hold": 40, "decay": 520, "decayCurve": 1.6,
        "clickType": "None", "lowBoost": 2,
    },
    "Techno Hard": {
        "pitchStart": 500, "pitchEnd": 50, "pitchTime": 70, "pitchCurve": 5.0,
        "hold": 40, "decay": 380, "decayCurve": 2.0,
        "clickType": "Click", "clickLevel": -6, "clickDecay": 3, "clickTone": 5_000,
        "driveType": "Hard", "drive": 9, "driveTone": 9_000,
    },
    "Techno Rumble": {
        "pitchStart": 260, "pitchEnd": 46, "pitchTime": 90, "pitchCurve": 5.0,
        "hold": 60, "decay": 700, "decayCurve": 1.3,
        "clickType": "Noise", "clickLevel": -14, "clickDecay": 4,
        "driveType": "Tube", "drive": 12, "driveTone": 2_500, "lowBoost": 3,
    },
    "Hardstyle": {
        "bodyShape": "Hard", "pitchStart": 1_200, "pitchEnd": 55, "pitchTime": 150, "pitchCurve": 6.0,
        "hold": 120, "decay": 500, "decayCurve": 1.5,
        "clickType": "Click", "clickLevel": -4, "clickDecay": 3, "clickTone": 6_000,
        "driveType": "Fold", "drive": 14, "driveTone": 6_000,
    },
    "Gabber": {
        "pitchStart": 900, "pitchEnd": 60, "pitchTime": 100, "pitchCurve": 4.0,
        "hold": 150, "decay": 400, "decayCurve": 1.5,
        "clickType": "Click", "clickLevel": -6, "clickDecay": 2,
        "driveType": "Hard", "drive": 24, "driveTone": 4_000,
    },
    "Trap Boom": {
        "pitchStart": 160, "pitchEnd": 45, "pitchTime": 70, "pitchCurve": 2.5,
        "hold": 10, "decay": 2_400, "decayCurve": 1.1, "length": 3_500,
        "clickType": "Click", "clickLevel": -12, "clickDecay": 3,
        "driveType": "Soft", "drive": 6,
    },
    "Drum & Bass": {
        "pitchStart": 450, "pitchEnd": 58, "pitchTime": 35, "pitchCurve": 3.0,
        "hold": 15, "decay": 220, "decayCurve": 2.0,
        "clickType": "Click", "clickLevel": -6, "clickDecay": 3, "clickTone": 6_000,
        "driveType": "Soft", "drive": 3,
    },
    "Minimal Blip": {
        "pitchStart": 600, "pitchEnd": 60, "pitchTime": 20, "pitchCurve": 3.0,
        "hold": 10, "decay": 150, "decayCurve": 2.2,
        "clickType": "Blip", "clickLevel": -6, "clickDecay": 6, "clickTone": 2_400,
    },
    "Big Room": {
        "pitchStart": 350, "pitchEnd": 50, "pitchTime": 60, "pitchCurve": 4.0,
        "hold": 50, "decay": 450, "decayCurve": 1.6,
        "clickType": "Noise", "clickLevel": -8, "clickDecay": 6, "clickTone": 5_000,
        "subLevel": -6, "subDecay": 600,
        "driveType": "Tube", "drive": 8, "driveTone": 8_000,
    },
    "Sub Kick": {
        "pitchStart": 90, "pitchEnd": 40, "pitchTime": 80, "pitchCurve": 2.0,
        "hold": 30, "decay": 900, "decayCurve": 1.3,
        "clickType": "None", "subLevel": -6, "subDecay": 900, "lowCut": 20,
    },
    "Lo-Fi Dusty": {
        "pitchStart": 220, "pitchEnd": 52, "pitchTime": 60, "pitchCurve": 3.0,
        "hold": 20, "decay": 260, "decayCurve": 1.8,
        "clickType": "Noise", "clickLevel": -16, "clickDecay": 10, "clickTone": 1_500,
        "driveType": "Tube", "drive": 6, "highCut": 3_500,
    },
    "Acoustic Felt": {
        "bodyShape": "Round", "pitchStart": 150, "pitchEnd": 60, "pitchTime": 40, "pitchCurve": 2.0,
        "hold": 5, "decay": 250, "decayCurve": 1.6,
        "clickType": "Noise", "clickLevel": -10, "clickDecay": 12, "clickTone": 2_500,
        "highCut": 8_000,
    },
}


def request(command):
    with socket.create_connection(("127.0.0.1", port), timeout=10.0) as connection:
        connection.settimeout(120)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(1 << 20)
            if not chunk:
                break
            data += chunk
    answer = json.loads(data.decode("utf-8"))
    if not answer.get("ok"):
        raise RuntimeError(f"{command.get('cmd')}: {answer.get('error')}")
    return answer.get("result", {})


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(DEFAULT_EXE))
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-kick-presets-"))
    presets = scratch / "Presets"
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {"presets": str(presets)}}),
                         encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([args.exe, f"--control-port={port}", "--silent", "--window-size=1400x900"], env=environment,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = 0
    try:
        for _ in range(200):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        status = request({"cmd": "status"})
        if not status.get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        request({"cmd": "set_master_gain", "gainDb": -60, "muted": True})
        request({"cmd": "new_project"})
        request({"cmd": "add_midi_track", "name": "Kick", "instrument": "kick-instrument"})
        for name, settings in SOUNDS.items():
            answer = request({"cmd": "kick", "track": "Kick", "init": True, "set": settings})
            if answer.get("problems"):
                print("FAIL", name, answer["problems"], flush=True)
                failures += 1
                continue
            # The output after the drive: its gain moves the peak by as much.
            if name != "Init":
                output = answer["values"]["output"] + (PEAK_DB - answer["peakDb"])
                answer = request({"cmd": "kick", "track": "Kick", "set": {"output": round(output, 2)}})
            time.sleep(0.3)
            node = answer["nodeId"]
            # As the instrument plays in an exported plug-in: its state written, restored, played.
            heard = request({"cmd": "instrument_export_check", "track": "Kick", "note": 36, "velocity": 1.0, "seconds": 1.0})
            rms = heard.get("rms", 0.0)
            peak = heard.get("peakDb", -200.0)
            ok = 1.0e-3 < rms and peak < 1.0 and heard.get("waveform") == 9
            print(("ok   " if ok else "FAIL ") + f"{name}: peak {answer['peakDb']:.1f} dB, {answer['lengthMs']:.0f} ms, "
                  f"ends {answer['endHz']:.0f} Hz; plug-in rms {rms:.3f}, peak {peak:.1f} dB", flush=True)
            failures += 0 if ok else 1
            request({"cmd": "presets", "track": "Kick", "nodeId": node, "action": "save", "name": name})
        TARGET.mkdir(parents=True, exist_ok=True)
        for old in TARGET.glob("*.cypreset"):
            old.unlink()
        written = sorted((presets / "device" / "kick-synth").glob("*.cypreset"))
        for file in written:
            # Without the date: the same sounds make the same files.
            document = json.loads(file.read_text(encoding="utf-8"))
            document.pop("created", None)
            (TARGET / file.name).write_text(json.dumps(document, ensure_ascii=False, indent=1), encoding="utf-8")
        print(f"{len(written)} presets in {TARGET}")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=20)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
