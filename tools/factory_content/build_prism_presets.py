"""Prisme's factory sounds: each set through CyDAW 2's control port on a
Prisme card, heard offline (no sound out), saved as a device preset and copied
into content/factory/Presets/device/prism-synth.

    python tools/factory_content/build_prism_presets.py [--exe "<CyDAW 2.exe>"] [--port 47836]

A sound is a few settings over the defaults ("init" first): keys as in
PrismSynth.h ("a.position", "m1.destination": "filter.cutoff").
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
TARGET = ROOT / "content" / "factory" / "Presets" / "device" / "prism-synth"
port = 47836

SAW = 0.67  # Basic Shapes: sine, triangle, saw, square


def slot(number, source, destination, amount):
    return {f"m{number}.source": source, f"m{number}.destination": destination, f"m{number}.amount": amount}


SOUNDS = {
    "Init": {},
    "Supersaw Lead": {
        "a.position": SAW, "a.unison": 9, "a.detune": 0.38, "a.blend": 0.8, "a.width": 1.0,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": 1, "b.unison": 5, "b.detune": 0.25,
        "b.level": 0.45,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 6_500, "filter.resonance": 0.15,
        "env1.attack": 0.005, "env1.decay": 0.5, "env1.sustain": 0.8, "env1.release": 0.3,
        **slot(1, "macro1", "filter.cutoff", 0.45),
        **slot(2, "velocity", "filter.cutoff", 0.15),
    },
    "Reese Bass": {
        "a.position": SAW, "a.octave": -2, "a.unison": 4, "a.detune": 0.18, "a.width": 0.35,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": -2, "b.unison": 4, "b.detune": 0.24,
        "b.level": 0.7,
        "sub.on": 1, "sub.octave": -3, "sub.level": 0.55,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 900, "filter.resonance": 0.2, "filter.drive": 0.3,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.22, "lfo1.unipolar": 0,
        **slot(1, "lfo1", "filter.cutoff", 0.12),
        **slot(2, "macro1", "filter.cutoff", 0.4),
        "voice.mode": "Legato", "voice.glide": 0.06,
        "env1.release": 0.12,
    },
    "Pluck Digital": {
        "a.table": "Harmonic Sweep", "a.position": 0.85, "a.unison": 3, "a.detune": 0.15,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 350, "filter.resonance": 0.3,
        "env1.attack": 0.001, "env1.decay": 0.7, "env1.sustain": 0.0, "env1.release": 0.35,
        "env2.attack": 0.001, "env2.decay": 0.25, "env2.sustain": 0.0,
        **slot(1, "env2", "filter.cutoff", 0.6),
        **slot(2, "velocity", "filter.cutoff", 0.2),
    },
    "Nappe evolutive": {
        "a.table": "Formant Vowels", "a.position": 0.2, "a.unison": 5, "a.detune": 0.22, "a.width": 1.0,
        "b.on": 1, "b.table": "Spectral Noise", "b.position": 0.3, "b.unison": 5, "b.detune": 0.3, "b.level": 0.4,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 3_200, "filter.resonance": 0.1,
        "env1.attack": 0.9, "env1.decay": 2.0, "env1.sustain": 0.8, "env1.release": 2.6,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.11,
        "lfo2.shape": "Smooth random", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.3,
        **slot(1, "lfo1", "a.position", 0.45),
        **slot(2, "lfo2", "b.position", 0.35),
        **slot(3, "macro1", "filter.cutoff", 0.4),
    },
    "Growl Bass": {
        "a.table": "Growl", "a.position": 0.3, "a.warp": "FM (B)", "a.warpAmount": 0.25, "a.octave": -1,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": 0.0, "b.octave": -1, "b.level": 0.0,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.6,
        "filter.on": 1, "filter.type": "Ladder", "filter.cutoff": 1_400, "filter.resonance": 0.4, "filter.drive": 0.5,
        "lfo1.shape": "Triangle", "lfo1.sync": 1, "lfo1.division": "1/8",
        **slot(1, "lfo1", "a.position", 0.6),
        **slot(2, "lfo1", "filter.cutoff", 0.25),
        **slot(3, "macro1", "a.warpAmount", 0.6),
        "voice.mode": "Mono", "voice.glide": 0.03,
        "env1.release": 0.1,
    },
    "Wobble": {
        "a.position": SAW, "a.unison": 3, "a.detune": 0.12, "a.octave": -1,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.5,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 250, "filter.resonance": 0.55,
        "lfo1.shape": "Sine", "lfo1.sync": 1, "lfo1.division": "1/8",
        **slot(1, "lfo1", "filter.cutoff", 0.6),
        **slot(2, "macro1", "filter.resonance", 0.3),
        "voice.mode": "Mono",
    },
    "Voix formant": {
        "a.table": "Formant Vowels", "a.position": 0.0, "a.unison": 3, "a.detune": 0.1,
        "filter.on": 1, "filter.type": "Formant", "filter.variation": 0.3, "filter.mix": 0.5, "filter.cutoff": 1_000,
        "filter.resonance": 0.3,
        "env1.attack": 0.08, "env1.release": 0.6,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.35,
        **slot(1, "macro1", "a.position", 1.0),
        **slot(2, "lfo1", "a.position", 0.2),
        **slot(3, "macro2", "filter.variation", 0.8),
    },
    "Cloche FM": {
        "a.table": "FM Bells", "a.position": 0.55,
        "b.on": 1, "b.table": "Overtones", "b.position": 0.25, "b.level": 0.3,
        "env1.attack": 0.001, "env1.decay": 3.0, "env1.sustain": 0.0, "env1.release": 2.5,
        "env2.attack": 0.001, "env2.decay": 1.2, "env2.sustain": 0.0,
        **slot(1, "env2", "a.position", 0.4),
        **slot(2, "velocity", "a.position", 0.2),
    },
    "Sub 808": {
        "a.position": 0.0, "a.octave": -1,
        "env1.attack": 0.001, "env1.decay": 1.6, "env1.sustain": 0.0, "env1.release": 0.4,
        "env2.attack": 0.0, "env2.decay": 0.07, "env2.sustain": 0.0,
        **slot(1, "env2", "a.coarse", 0.17),
        "a.warp": "Quantize", "a.warpAmount": 0.0,
        **slot(2, "macro1", "a.warpAmount", 0.5),
        "voice.mode": "Mono", "voice.glide": 0.05,
    },
    "Arpege synchro": {
        "a.table": "Sync Sweep", "a.position": 0.15,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 5_000,
        "env1.attack": 0.001, "env1.decay": 0.35, "env1.sustain": 0.2, "env1.release": 0.15,
        "env2.attack": 0.001, "env2.decay": 0.3, "env2.sustain": 0.0,
        **slot(1, "env2", "a.position", 0.55),
        **slot(2, "macro1", "a.position", 0.6),
    },
    "Cuivres": {
        "a.position": SAW, "a.unison": 3, "a.detune": 0.12,
        "b.on": 1, "b.table": "Square Build", "b.position": 0.6, "b.level": 0.5,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 700, "filter.resonance": 0.1,
        "env1.attack": 0.05, "env1.sustain": 0.9, "env1.release": 0.25,
        "env2.attack": 0.08, "env2.decay": 0.5, "env2.sustain": 0.6,
        **slot(1, "env2", "filter.cutoff", 0.35),
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 5.2, "lfo1.unipolar": 0, "lfo1.delay": 0.4, "lfo1.rise": 0.6,
        **slot(2, "lfo1", "a.fine", 0.04),
        **slot(3, "lfo1", "b.fine", 0.04),
    },
    "Orgue": {
        "a.table": "Drawbar Organ", "a.position": 0.45,
        "env1.attack": 0.005, "env1.sustain": 1.0, "env1.release": 0.08,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 6.0, "lfo1.mode": "Free",
        **slot(1, "lfo1", "a.level", 0.12),
        **slot(2, "macro1", "a.position", 0.55),
    },
    "Bruit et souffle": {
        "a.on": 0,
        "noise.on": 1, "noise.type": "Pink", "noise.level": 1.0, "noise.tone": 0.45, "voice.master": 1.0,
        "filter.on": 1, "filter.type": "Band pass 12", "filter.cutoff": 1_500, "filter.resonance": 0.15,
        "env1.attack": 0.4, "env1.release": 1.2,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.6,
        **slot(1, "lfo1", "filter.cutoff", 0.35),
        **slot(2, "note", "filter.cutoff", 0.4),
    },
    # With the third oscillator, the spectral morphs and the stereo LFOs.
    "Trois oscillateurs": {
        "a.position": SAW, "b.on": 1, "b.table": "Basic Shapes", "b.position": 1.0, "b.semi": 7, "b.level": 0.5,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -1, "c.level": 0.6,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 2_500, "filter.resonance": 0.2,
        "env2.decay": 0.5, "env2.sustain": 0.2, **slot(1, "env2", "filter.cutoff", 0.35),
    },
    "Cloche spectrale": {
        "a.table": "FM Bells", "a.position": 0.3, "a.spectral": "Octave", "a.spectralAmount": 0.5,
        "c.on": 1, "c.table": "Harmonic Sweep", "c.position": 0.4, "c.warp": "FM (A)", "c.warpAmount": 0.3, "c.level": 0.4,
        "env1.attack": 0.002, "env1.decay": 2.5, "env1.sustain": 0.0, "env1.release": 2.0,
    },
    "Formant qui parle": {
        "a.table": "Formant Vowels", "a.position": 0.4, "a.spectral": "Formant", "a.spectralAmount": 0.3,
        "lfo1.shape": "Triangle", "lfo1.sync": 0, "lfo1.rate": 0.5, "lfo1.stereo": 0.5,
        **slot(1, "lfo1", "a.spectralAmount", 0.35), **slot(2, "lfo1", "a.position", 0.25),
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 6_000,
    },
    "Pad stéréo": {
        "a.position": SAW, "a.unison": 7, "a.detune": 0.3, "b.on": 1, "b.table": "Harmonic Sweep", "b.position": 0.6,
        "b.level": 0.5, "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 1_500, "filter.resonance": 0.25,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.2, "lfo1.stereo": 1.0,
        **slot(1, "lfo1", "filter.cutoff", 0.3), "env1.attack": 0.8, "env1.release": 2.0,
    },
    "Pincé spectral": {
        "a.table": "Harmonic Sweep", "a.position": 1.0, "a.spectral": "Low pass", "a.spectralAmount": 0.8,
        "env2.attack": 0.001, "env2.decay": 0.35, "env2.sustain": 0.0, **slot(1, "env2", "a.spectralAmount", -0.8),
        "env1.decay": 0.6, "env1.sustain": 0.0, "env1.release": 0.4,
    },
    "Peigne mouvant": {
        "a.position": SAW, "a.unison": 3, "a.detune": 0.2, "a.spectral": "Comb", "a.spectralAmount": 0.3,
        "lfo2.shape": "Triangle", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.15, "lfo2.stereo": 0.5,
        **slot(1, "lfo2", "a.spectralAmount", 0.4), "env1.attack": 0.3, "env1.release": 1.2,
    },
    "Impairs creux": {
        "voice.mode": "Legato", "voice.glide": 0.06, "a.position": SAW, "a.spectral": "Odd", "a.spectralAmount": 0.9,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 3_500, "lfo2.sync": 0, "lfo2.rate": 5.0,
        "lfo2.delay": 0.4, **slot(1, "lfo2", "a.fine", 0.05),
    },
    "Basse à trois": {
        "voice.mode": "Mono", "a.position": SAW, "a.octave": -1, "b.on": 1, "b.table": "Basic Shapes", "b.position": 1.0,
        "b.octave": -1, "b.fine": 5, "b.level": 0.5, "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0,
        "c.octave": -2, "c.level": 0.7, "filter.on": 1, "filter.type": "Ladder", "filter.cutoff": 400,
        "filter.resonance": 0.25, "env2.decay": 0.3, "env2.sustain": 0.1, **slot(1, "env2", "filter.cutoff", 0.45),
    },
    "Nappe en octaves": {
        "a.table": "Harmonic Sweep", "a.position": 0.5, "a.spectral": "Octave", "a.spectralAmount": 0.6,
        "c.on": 1, "c.table": "Overtones", "c.octave": 1, "c.level": 0.3, "env1.attack": 1.0, "env1.release": 2.5,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 4_000,
    },
    "Arpège stéréo": {
        "a.table": "Digital Steps", "a.position": 0.5, "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 900,
        "filter.resonance": 0.5, "lfo1.shape": "Saw down", "lfo1.sync": 1, "lfo1.division": "1/16", "lfo1.stereo": 0.5,
        **slot(1, "lfo1", "filter.cutoff", 0.45), "env1.decay": 0.3, "env1.sustain": 0.3,
    },
    "Cristal haut": {
        "a.position": SAW, "a.spectral": "High pass", "a.spectralAmount": 0.5, "c.on": 1, "c.table": "FM Bells",
        "c.octave": 1, "c.level": 0.35, "env1.attack": 0.05, "env1.release": 2.5, "a.unison": 3, "a.detune": 0.15,
    },
    "Voix synthétique": {
        "voice.mode": "Legato", "voice.glide": 0.08, "a.table": "Formant Vowels", "a.position": 0.2,
        "a.spectral": "Formant", "a.spectralAmount": 0.25, "lfo1.shape": "Smooth random", "lfo1.sync": 0,
        "lfo1.rate": 0.4, **slot(1, "lfo1", "a.position", 0.4), "lfo2.sync": 0, "lfo2.rate": 5.5, "lfo2.delay": 0.3,
        **slot(2, "lfo2", "a.fine", 0.06),
    },
    "Trémolo stéréo": {
        "a.table": "Analog PWM", "a.position": 0.4, "lfo1.shape": "Sine", "lfo1.sync": 1, "lfo1.division": "1/8",
        "lfo1.stereo": 1.0, **slot(1, "lfo1", "voice.master", -0.4), "env1.release": 0.8,
    },
    "Croissance FM": {
        "a.table": "Basic Shapes", "a.position": 0.0, "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0,
        "c.semi": 7, "c.warp": "FM (A)", "c.warpAmount": 0.0, "c.level": 0.7, "a.level": 0.3,
        "env3.attack": 1.5, "env3.sustain": 1.0, **slot(1, "env3", "c.warpAmount", 0.7), "env1.attack": 0.2,
        "env1.release": 1.5,
    },
    "Orgue spectral": {
        "a.table": "Drawbar Organ", "a.position": 0.5, "a.spectral": "Even", "a.spectralAmount": 0.4,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 6.0, "lfo1.stereo": 0.5,
        **slot(1, "lfo1", "voice.master", 0.12), "env1.attack": 0.01, "env1.release": 0.15,
    },
    "Basse spectrale": {
        "voice.mode": "Mono", "a.table": "Growl", "a.position": 0.3, "a.octave": -1, "a.spectral": "Low pass",
        "a.spectralAmount": 0.5, "lfo1.sync": 1, "lfo1.division": "1/8", **slot(1, "lfo1", "a.spectralAmount", 0.4),
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -2, "c.level": 0.6,
    },
}


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
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-prism-presets-"))
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
        request({"cmd": "set_master_gain", "gainDb": -60, "muted": True})
        request({"cmd": "new_project"})
        request({"cmd": "add_midi_track", "name": "Prisme", "instrument": "prism-instrument"})
        for name, settings in SOUNDS.items():
            answer = request({"cmd": "prism", "track": "Prisme", "init": True, "set": settings})
            if answer.get("problems"):
                print("FAIL", name, answer["problems"], flush=True)
                failures += 1
                continue
            time.sleep(0.3)
            node = answer["nodeId"]
            heard = request({"cmd": "instrument_export_check", "track": "Prisme", "note": 48, "velocity": 0.8, "seconds": 1.0})
            rms = heard.get("rms", 0.0)
            peak = heard.get("peakDb", -200.0)
            ok = 1.0e-3 < rms and peak < 3.0
            print(("ok   " if ok else "FAIL ") + f"{name}: rms {rms:.3f}, peak {peak:.1f} dB", flush=True)
            failures += 0 if ok else 1
            request({"cmd": "presets", "track": "Prisme", "nodeId": node, "action": "save", "name": name})
        TARGET.mkdir(parents=True, exist_ok=True)
        for old in TARGET.glob("*.cypreset"):
            old.unlink()
        written = sorted((presets / "device" / "prism-synth").glob("*.cypreset"))
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
    sys.exit(main())
