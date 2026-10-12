"""Batterie's factory sounds: a kit and its mix, each set through CyDAW 2's
control port on a Batterie card, heard offline (no sound out: the exported
plug-in's path renders a hit in memory), saved as a device preset and copied
into content/factory/Presets/device/batterie.

    python tools/factory_content/build_batterie_presets.py [--exe "<CyDAW 2.exe>"] [--port 47839]

The kits' samples are the free banks CyDAW downloads (Options > Banques de
sons): this reads where they are from the person's Locations file. A sound is
a kit and a few settings over the neutral mix: keys as in DrumKit.h
("snare.tune", "kit.humanise").
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
TARGET = ROOT / "content" / "factory" / "Presets" / "device" / "batterie"
port = 47839

TOMS = ("tom1", "tom2", "tom3", "tom4")
CYMBALS = ("crash", "ride", "china")


def each(pieces, field, value):
    return {f"{piece}.{field}": value for piece in pieces}


SOUNDS = {
    "Rouillée brute": {"kit": 0, "set": {}},
    "Rock sec": {"kit": 0, "set": {"kick.level": 2, "snare.level": 1.5, **each(TOMS, "decay", 0.75), "crash.level": -3,
                                   "hihat.level": -2}},
    "Metal serré": {"kit": 0, "set": {"kick.level": 3, "kick.decay": 0.6, "snare.level": 2, "snare.tune": 1,
                                      **each(TOMS, "tune", -1), **each(TOMS, "decay", 0.7), **each(CYMBALS, "level", -4)}},
    "Toms profonds": {"kit": 0, "set": {**each(TOMS, "tune", -3), **each(TOMS, "level", 2), "kick.tune": -1}},
    "Hip-hop poussiéreux": {"kit": 0, "set": {"kick.level": 3, "kick.tune": -2, "snare.tune": -1, "snare.decay": 0.5,
                                              "hihat.level": -4, "kit.humanise": 0.3}},
    "Jazz feutré": {"kit": 1, "set": {"kick.level": -4, "ride.level": 2, "snare.level": -2, "kit.humanise": 0.3}},
    "Jazz lumineux": {"kit": 1, "set": {"kit.tune": 1, **each(CYMBALS, "level", 1), "hihat.level": 1}},
    "Tourbillon pop": {"kit": 2, "set": {"snare.level": 1, "hihat.level": -1, "kick.level": 1}},
    "Funk serré": {"kit": 3, "set": {"hihat.decay": 0.6, "snare.level": 2, "kick.decay": 0.7, "kit.humanise": 0.2}},
    "Indocile ouverte": {"kit": 3, "set": {**each(CYMBALS, "level", 1), "kick.level": 1}},
    "Balais intimes": {"kit": 4, "set": {"kick.level": -3, "snare.level": 1, "kit.humanise": 0.4}},
    "Années 70": {"kit": 5, "set": {"kit.tune": -1, **each(TOMS, "decay", 0.8)}},
    "Lo-fi grave": {"kit": 5, "set": {"kit.tune": -4, "kit.humanise": 0.5, "snare.decay": 0.6}},
}


def request(command: dict, timeout: float = 120.0) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=10.0) as connection:
        connection.settimeout(timeout)
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


def banks_folder() -> str | None:
    """Where the person keeps the banks (their Locations file), if anywhere."""
    for name in ("Locations.json",):
        path = Path(os.environ.get("APPDATA", "")) / "CyDAW" / name
        if path.is_file():
            folders = json.loads(path.read_text(encoding="utf-8")).get("folders", {})
            if folders.get("banks"):
                return folders["banks"]
    return None


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(DEFAULT_EXE))
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    banks = banks_folder()
    if banks is None or not (Path(banks) / "big-rusty-drums").is_dir():
        print("the drum banks are not here (Options > Banques de sons downloads them)")
        return 1
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-batterie-presets-"))
    presets = scratch / "Presets"
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1,
                                     "folders": {"presets": str(presets), "banks": banks}}), encoding="utf-8")
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
        if not request({"cmd": "status"}).get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        request({"cmd": "set_master_gain", "gainDb": -60, "muted": True})
        request({"cmd": "new_project"})
        request({"cmd": "add_midi_track", "name": "Batterie", "instrument": "batterie-instrument"})
        for name, sound in SOUNDS.items():
            answer = request({"cmd": "batterie", "track": "Batterie", "kit": sound["kit"], "init": True, "set": sound["set"]})
            if answer.get("problems"):
                print("FAIL", name, answer["problems"], flush=True)
                failures += 1
                continue
            heard = {}
            for note in (36, 38):
                heard[note] = request({"cmd": "instrument_export_check", "track": "Batterie", "note": note, "velocity": 0.9,
                                       "seconds": 1.5})
            ok = all(item.get("rms", 0.0) > 1.0e-4 and item.get("peakDb", 0.0) < 0.0 for item in heard.values())
            print(("ok   " if ok else "FAIL ") + f"{name}: {answer['kit']}, kick peak {heard[36].get('peakDb', -200):.1f} dB, "
                  f"snare peak {heard[38].get('peakDb', -200):.1f} dB", flush=True)
            failures += 0 if ok else 1
            request({"cmd": "presets", "track": "Batterie", "nodeId": answer["nodeId"], "action": "save", "name": name})
        TARGET.mkdir(parents=True, exist_ok=True)
        for old in TARGET.glob("*.cypreset"):
            old.unlink()
        written = sorted((presets / "device" / "batterie").glob("*.cypreset"))
        for file in written:
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
    sys.exit(main())
