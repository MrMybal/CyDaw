"""The factory presets of the studio effects (Réverbe Pro, Compresseur Pro,
Limiteur Pro, Filtre Pro), of the amp, cabinet and microphone simulators and of
the mixing effects (Pression, Fournaise, Écho Bande, Multibande Pro, Velours,
Patine, Sonomètre) of the suites (Tranche, Finition), of Halo and of the Égaliseur Pro, of the
voice, pitch, space, rhythm, motion, resonance and utility effects (Vocodeur… Stéréoscope), of the
MIDI effects (Arpège… Roulement) and of the basic devices (the factory modules — Poly Keys, Stereo Finisher… — and the plain effect, filter,
source and MIDI cards), written by CyDAW itself into content/factory/Presets/device/<effect>.

For each preset: a fresh card of the device on a track that takes it (an
audio track for an effect, an empty instrument track for an instrument, an
instrument track for a MIDI effect or a modulation source), its values set
through the control port, saved as a device preset. A silent session (the
speakers silenced) in a folder of its own; nothing is played.

    python tools/factory_content/build_studio_presets.py [--exe ...] [--only reverb-pro,...]
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

from studio_presets import amps, basics, dynamics, space
from studio_presets import voice, pitch, spacefx, rhythm, motion, resonance, utility, midi

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
TARGET = ROOT / "content" / "factory" / "Presets" / "device"
port = 47839

# Values by binding suffix; what is not given keeps its default. One
# module a family (studio_presets/).
PRESETS = {**dynamics.PRESETS, **space.PRESETS, **amps.PRESETS, **basics.PRESETS, **voice.PRESETS, **pitch.PRESETS, **spacefx.PRESETS, **rhythm.PRESETS, **motion.PRESETS, **resonance.PRESETS, **utility.PRESETS, **midi.PRESETS}


def request(command: dict, timeout: float = 60.0) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(timeout)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(1 << 20)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode("utf-8"))



def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(DEFAULT_EXE))
    parser.add_argument("--port", type=int, default=port)
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    port = args.port
    only = {key for key in args.only.split(",") if key}
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-studio-presets-"))
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
        if not request({"cmd": "status"})["result"].get("speakersSilenced"):
            print("the speakers are not silenced: stopping")
            return 1
        request({"cmd": "new_project"})
        # An audio track: one of the templates' loops, renamed.
        loop = sorted((ROOT / "templates" / "audio").glob("*.wav"))[0]
        track = request({"cmd": "add_audio_track", "file": str(loop)})["result"]["name"]
        request({"cmd": "set_track", "track": track, "name": "Effet"})
        # An empty instrument track for the instruments, one that plays for
        # the MIDI effects and the modulation sources.
        request({"cmd": "add_midi_track", "name": "Instrument"})
        request({"cmd": "add_midi_track", "name": "Notes", "instrument": "prism-instrument"})
        catalogue = {}
        for device in request({"cmd": "list_devices"})["result"]["devices"]:
            catalogue.setdefault(device["key"], device)
        for effect, sounds in PRESETS.items():
            if only and effect not in only:
                continue
            device = catalogue.get(effect, {})
            holder = ("Instrument" if device.get("instrument")
                      else "Notes" if device.get("input") in ("midi", "none") else "Effet")
            for name, values in sounds.items():
                answer = request({"cmd": "insert_device", "track": holder, "key": effect})
                node = next((item["id"] for item in answer.get("result", {}).get("nodes", [])
                             if item.get("type") == effect or item.get("typeKey") == effect), None)
                if node is None:
                    print("FAIL", effect, name, answer)
                    failures += 1
                    continue
                for suffix, value in values.items():
                    result = request({"cmd": "set_parameter", "track": holder, "key": "node.%s.%s" % (node, suffix), "value": value})
                    if not result.get("ok"):
                        print("FAIL", effect, name, suffix, result.get("error"))
                        failures += 1
                saved = request({"cmd": "presets", "track": holder, "nodeId": node, "action": "save", "name": name})
                print(("ok   " if saved.get("ok") else "FAIL ") + effect + " / " + name, flush=True)
                failures += 0 if saved.get("ok") else 1
                request({"cmd": "remove_device", "track": holder, "nodeId": node})
        for effect in PRESETS:
            if only and effect not in only:
                continue
            target = TARGET / effect
            target.mkdir(parents=True, exist_ok=True)
            for old in target.glob("*.cypreset"):
                old.unlink()
            written = sorted((presets / "device" / effect).glob("*.cypreset"))
            for file in written:
                # Without the date: the same settings make the same files.
                document = json.loads(file.read_text(encoding="utf-8"))
                document.pop("created", None)
                (target / file.name).write_text(json.dumps(document, ensure_ascii=False, indent=1), encoding="utf-8")
            print(f"{len(written)} presets in {target}")
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
