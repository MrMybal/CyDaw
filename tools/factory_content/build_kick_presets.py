"""Kick's factory sounds: each set through CyDAW 2's control port on a Kick
card, measured offline (no sound out), brought to -1 dBFS at its peak, saved as
a device preset and copied into content/factory/Presets/device/kick-synth.

    python tools/factory_content/build_kick_presets.py [--exe "<CyDAW 2.exe>"] [--port 47837]

A sound is a few settings over the defaults ("init" first): keys as in
KickSynth.h ("pitchStart", "decay"); a choice by its English name ("Noise").
Every sound plays as set on C2 (the note tunes it, key tracking on).

Names are English and descriptive, with no brand, product, artist or machine
model in new ones, and none of the characters a file name refuses (slashes,
colon, comma, star, question mark, quotes, angle brackets, bar): they become
file names, here and in the CyDAW Electro bank (build_electro_banks.py takes
these sounds as its kicks).
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
    # ---- Styles set in full: body, envelope, click, sub, drive, tone, velocity.
    # Where a sub sits under the body, the pitch time puts the two in phase once
    # the fall is over (at C2), so the sub adds to the body instead of hollowing it.
    "Techno Rumble Source": {
        # Short and dry, made to feed a reverb and distortion rumble bus: no tail of its own.
        "bodyShape": "Sine", "bodyPhase": 0, "pitchStart": 420, "pitchEnd": 49, "pitchTime": 55, "pitchCurve": 5.0,
        "attack": 0.2, "hold": 35, "decay": 260, "decayCurve": 2.4, "length": 600,
        "clickType": "Click", "clickLevel": -9, "clickDecay": 2.5, "clickTone": 4_200,
        "subLevel": -60,
        "driveType": "Tube", "drive": 6, "driveTone": 7_000,
        "lowCut": 28, "highCut": 14_000, "lowBoost": 1.5, "velocity": 0.3,
    },
    "Hard Techno Distorted": {
        "bodyShape": "Sine", "bodyPhase": 30, "pitchStart": 900, "pitchEnd": 52, "pitchTime": 82, "pitchCurve": 6.0,
        "attack": 0, "hold": 70, "decay": 420, "decayCurve": 1.4, "length": 900,
        "clickType": "Noise", "clickLevel": -6, "clickDecay": 5, "clickTone": 5_500,
        "subLevel": -10, "subDecay": 400, "subOctave": "Same",
        "driveType": "Hard", "drive": 20, "driveTone": 5_500,
        "lowCut": 30, "highCut": 12_000, "lowBoost": 2, "velocity": 0.2,
    },
    "Industrial Fold Crunch": {
        "bodyShape": "Triangle", "bodyPhase": 60, "pitchStart": 650, "pitchEnd": 46, "pitchTime": 108, "pitchCurve": 4.5,
        "attack": 0, "hold": 90, "decay": 520, "decayCurve": 1.3, "length": 1_000,
        "clickType": "Noise", "clickLevel": -5, "clickDecay": 14, "clickTone": 2_600,
        "subLevel": -14, "subDecay": 500, "subOctave": "Same",
        "driveType": "Fold", "drive": 17, "driveTone": 4_200,
        "lowCut": 32, "highCut": 11_000, "lowBoost": 3, "velocity": 0.25,
    },
    "House Punchy Thump": {
        "bodyShape": "Sine", "bodyPhase": 45, "pitchStart": 330, "pitchEnd": 55, "pitchTime": 57.5, "pitchCurve": 4.5,
        "attack": 0.3, "hold": 22, "decay": 300, "decayCurve": 1.9, "length": 700,
        "clickType": "Noise", "clickLevel": -10, "clickDecay": 4, "clickTone": 4_500,
        "subLevel": -16, "subDecay": 280, "subOctave": "Same",
        "driveType": "Soft", "drive": 5, "driveTone": 12_000,
        "lowCut": 25, "highCut": 16_000, "lowBoost": 2, "velocity": 0.4,
    },
    "Deep House Warm Round": {
        "bodyShape": "Round", "bodyPhase": 0, "pitchStart": 180, "pitchEnd": 46, "pitchTime": 107.5, "pitchCurve": 2.6,
        "attack": 1.5, "hold": 45, "decay": 560, "decayCurve": 1.5, "length": 1_100,
        "clickType": "Blip", "clickLevel": -22, "clickDecay": 4, "clickTone": 1_800,
        "subLevel": -12, "subDecay": 500, "subOctave": "Same",
        "driveType": "Tube", "drive": 3, "driveTone": 4_000,
        "lowCut": 22, "highCut": 6_000, "lowBoost": 3, "velocity": 0.5,
    },
    "Trap Long Sub Tail": {
        "bodyShape": "Sine", "bodyPhase": 0, "pitchStart": 150, "pitchEnd": 43.65, "pitchTime": 90, "pitchCurve": 2.2,
        "attack": 0.5, "hold": 20, "decay": 3_200, "decayCurve": 1.0, "length": 4_500,
        "clickType": "Click", "clickLevel": -14, "clickDecay": 2, "clickTone": 3_500,
        "subLevel": -8, "subDecay": 3_500, "subOctave": "Same",
        "driveType": "Soft", "drive": 8, "driveTone": 6_000,
        "lowCut": 20, "highCut": 12_000, "lowBoost": 2, "velocity": 0.3,
    },
    "Drill Sliding Sub": {
        # A long, nearly straight glide down an octave (G2 to G1), as a drill bass slides.
        "bodyShape": "Sine", "bodyPhase": 0, "pitchStart": 98, "pitchEnd": 49, "pitchTime": 260, "pitchCurve": 0.8,
        "attack": 0.5, "hold": 40, "decay": 2_600, "decayCurve": 1.1, "length": 3_500,
        "clickType": "Click", "clickLevel": -12, "clickDecay": 2.5, "clickTone": 3_000,
        "subLevel": -60,
        "driveType": "Soft", "drive": 9, "driveTone": 5_000,
        "lowCut": 22, "highCut": 11_000, "lowBoost": 1, "velocity": 0.3,
    },
    "Hardstyle Long Tuned Tail": {
        "bodyShape": "Triangle", "bodyPhase": 90, "pitchStart": 3_000, "pitchEnd": 52, "pitchTime": 181, "pitchCurve": 7.0,
        "attack": 0, "hold": 160, "decay": 650, "decayCurve": 0.9, "length": 1_000,
        "clickType": "Click", "clickLevel": -3, "clickDecay": 3, "clickTone": 7_000,
        "subLevel": -12, "subDecay": 500, "subOctave": "Same",
        "driveType": "Fold", "drive": 16, "driveTone": 7_000,
        "lowCut": 35, "highCut": 15_000, "lowBoost": 3, "velocity": 0.1,
    },
    "Raw Hard Dance Screamer": {
        "bodyShape": "Hard", "bodyPhase": 0, "pitchStart": 1_800, "pitchEnd": 48, "pitchTime": 140.4, "pitchCurve": 5.0,
        "attack": 0, "hold": 140, "decay": 520, "decayCurve": 1.2, "length": 900,
        "clickType": "Noise", "clickLevel": -4, "clickDecay": 12, "clickTone": 3_000,
        "subLevel": -16, "subDecay": 450, "subOctave": "Same",
        "driveType": "Hard", "drive": 28, "driveTone": 3_500,
        "lowCut": 40, "highCut": 10_000, "lowBoost": 4, "velocity": 0.1,
    },
    "Psytrance Tight Click": {
        # Out of the way of a rolling bass: short, cut low, a sharp click.
        "bodyShape": "Sine", "bodyPhase": 0, "pitchStart": 1_600, "pitchEnd": 46.25, "pitchTime": 28, "pitchCurve": 2.2,
        "attack": 0, "hold": 15, "decay": 190, "decayCurve": 2.6, "length": 320,
        "clickType": "Click", "clickLevel": -5, "clickDecay": 1.5, "clickTone": 6_000,
        "subLevel": -60,
        "driveType": "Soft", "drive": 2, "driveTone": 16_000,
        "lowCut": 32, "highCut": 18_000, "lowBoost": 1, "velocity": 0.1,
    },
    "Drum and Bass Rolling Punch": {
        "bodyShape": "Sine", "bodyPhase": 20, "pitchStart": 520, "pitchEnd": 58.3, "pitchTime": 28.7, "pitchCurve": 3.5,
        "attack": 0, "hold": 12, "decay": 200, "decayCurve": 2.2, "length": 450,
        "clickType": "Click", "clickLevel": -7, "clickDecay": 2, "clickTone": 7_000,
        "subLevel": -14, "subDecay": 220, "subOctave": "Same",
        "driveType": "Tube", "drive": 6, "driveTone": 10_000,
        "lowCut": 30, "highCut": 17_000, "lowBoost": 1, "velocity": 0.35,
    },
    "Lo-Fi Muffled Tape Thump": {
        "bodyShape": "Round", "bodyPhase": 0, "pitchStart": 160, "pitchEnd": 52, "pitchTime": 70, "pitchCurve": 2.5,
        "attack": 2, "hold": 15, "decay": 320, "decayCurve": 1.7, "length": 600,
        "clickType": "Noise", "clickLevel": -14, "clickDecay": 18, "clickTone": 1_200,
        "subLevel": -60,
        "driveType": "Tube", "drive": 10, "driveTone": 2_500,
        "lowCut": 35, "highCut": 2_800, "lowBoost": 4, "velocity": 0.6,
    },
    "Gabber Overload Stomp": {
        "bodyShape": "Hard", "bodyPhase": 0, "pitchStart": 1_100, "pitchEnd": 61.7, "pitchTime": 90, "pitchCurve": 4.0,
        "attack": 0, "hold": 180, "decay": 420, "decayCurve": 1.0, "length": 700,
        "clickType": "Click", "clickLevel": -4, "clickDecay": 2, "clickTone": 5_000,
        "subLevel": -60,
        "driveType": "Hard", "drive": 32, "driveTone": 3_200,
        "lowCut": 45, "highCut": 9_000, "lowBoost": 3, "velocity": 0.0,
    },
    "Synthwave Gated Boom": {
        # Held flat, then shut: a low decay curve and a short length make the gate.
        "bodyShape": "Triangle", "bodyPhase": 0, "pitchStart": 260, "pitchEnd": 55, "pitchTime": 78, "pitchCurve": 3.0,
        "attack": 0.5, "hold": 120, "decay": 260, "decayCurve": 0.4, "length": 380,
        "clickType": "Noise", "clickLevel": -11, "clickDecay": 14, "clickTone": 3_500,
        "subLevel": -14, "subDecay": 380, "subOctave": "Same",
        "driveType": "Tube", "drive": 4, "driveTone": 9_000,
        "lowCut": 30, "highCut": 9_000, "lowBoost": 2, "velocity": 0.5,
    },
    "Cinematic Boom Impact": {
        "bodyShape": "Sine", "bodyPhase": 0, "pitchStart": 260, "pitchEnd": 36.7, "pitchTime": 215, "pitchCurve": 3.0,
        "attack": 1, "hold": 80, "decay": 3_500, "decayCurve": 1.3, "length": 5_500,
        "clickType": "Noise", "clickLevel": -6, "clickDecay": 40, "clickTone": 1_800,
        "subLevel": -4, "subDecay": 4_500, "subOctave": "Same",
        "driveType": "Tube", "drive": 8, "driveTone": 3_000,
        "lowCut": 18, "highCut": 9_000, "lowBoost": 5, "velocity": 0.6,
    },
    "Dub Techno Soft Thud": {
        "bodyShape": "Round", "bodyPhase": 0, "pitchStart": 140, "pitchEnd": 49, "pitchTime": 99, "pitchCurve": 2.0,
        "attack": 3, "hold": 30, "decay": 450, "decayCurve": 1.5, "length": 900,
        "clickType": "None", "clickLevel": -48,
        "subLevel": -12, "subDecay": 600, "subOctave": "Same",
        "driveType": "Tube", "drive": 4, "driveTone": 1_800,
        "lowCut": 25, "highCut": 3_000, "lowBoost": 3, "velocity": 0.5,
    },
    "Minimal Tight Knock": {
        "bodyShape": "Triangle", "bodyPhase": 0, "pitchStart": 700, "pitchEnd": 65.4, "pitchTime": 15, "pitchCurve": 3.0,
        "attack": 0, "hold": 8, "decay": 150, "decayCurve": 2.2, "length": 260,
        "clickType": "Blip", "clickLevel": -8, "clickDecay": 4, "clickTone": 3_000,
        "subLevel": -60,
        "driveType": "Soft", "drive": 3, "driveTone": 12_000,
        "lowCut": 40, "highCut": 14_000, "lowBoost": 0, "velocity": 0.6,
    },
    "Festival Layered Punch": {
        "bodyShape": "Sine", "bodyPhase": 30, "pitchStart": 420, "pitchEnd": 49, "pitchTime": 58, "pitchCurve": 4.5,
        "attack": 0, "hold": 55, "decay": 480, "decayCurve": 1.5, "length": 900,
        "clickType": "Noise", "clickLevel": -6, "clickDecay": 7, "clickTone": 5_500,
        "subLevel": -7, "subDecay": 650, "subOctave": "Same",
        "driveType": "Tube", "drive": 10, "driveTone": 9_000,
        "lowCut": 28, "highCut": 17_000, "lowBoost": 3, "velocity": 0.2,
    },
    "Two-Step Short Knock": {
        "bodyShape": "Sine", "bodyPhase": 15, "pitchStart": 300, "pitchEnd": 58.3, "pitchTime": 32.4, "pitchCurve": 3.0,
        "attack": 0, "hold": 8, "decay": 180, "decayCurve": 2.2, "length": 400,
        "clickType": "Noise", "clickLevel": -9, "clickDecay": 5, "clickTone": 6_500,
        "subLevel": -16, "subDecay": 200, "subOctave": "Same",
        "driveType": "Soft", "drive": 3, "driveTone": 14_000,
        "lowCut": 30, "highCut": 16_000, "lowBoost": 1, "velocity": 0.6,
    },
    "Boom Bap Chesty Kick": {
        "bodyShape": "Round", "bodyPhase": 0, "pitchStart": 200, "pitchEnd": 55, "pitchTime": 48.3, "pitchCurve": 2.5,
        "attack": 0.5, "hold": 20, "decay": 340, "decayCurve": 1.6, "length": 700,
        "clickType": "Noise", "clickLevel": -10, "clickDecay": 9, "clickTone": 2_800,
        "subLevel": -18, "subDecay": 300, "subOctave": "Same",
        "driveType": "Tube", "drive": 7, "driveTone": 5_000,
        "lowCut": 30, "highCut": 7_000, "lowBoost": 3, "velocity": 0.6,
    },
    "Melodic Techno Deep Pulse": {
        "bodyShape": "Sine", "bodyPhase": 0, "pitchStart": 240, "pitchEnd": 43.65, "pitchTime": 91.7, "pitchCurve": 3.5,
        "attack": 0.3, "hold": 50, "decay": 620, "decayCurve": 1.6, "length": 1_100,
        "clickType": "Click", "clickLevel": -14, "clickDecay": 2, "clickTone": 3_000,
        "subLevel": -12, "subDecay": 700, "subOctave": "Same",
        "driveType": "Tube", "drive": 5, "driveTone": 4_500,
        "lowCut": 24, "highCut": 12_000, "lowBoost": 2.5, "velocity": 0.4,
    },
    "Rock Beater Thump": {
        "bodyShape": "Round", "bodyPhase": 0, "pitchStart": 180, "pitchEnd": 61.7, "pitchTime": 30, "pitchCurve": 2.0,
        "attack": 0, "hold": 5, "decay": 220, "decayCurve": 1.8, "length": 500,
        "clickType": "Noise", "clickLevel": -6, "clickDecay": 6, "clickTone": 3_800,
        "subLevel": -60,
        "driveType": "Soft", "drive": 2, "driveTone": 10_000,
        "lowCut": 35, "highCut": 10_000, "lowBoost": 2, "velocity": 0.8,
    },
    "Folded Glitch Zap": {
        "bodyShape": "Hard", "bodyPhase": 90, "pitchStart": 4_000, "pitchEnd": 65, "pitchTime": 25, "pitchCurve": 1.5,
        "attack": 0, "hold": 10, "decay": 160, "decayCurve": 2.0, "length": 300,
        "clickType": "Blip", "clickLevel": -6, "clickDecay": 10, "clickTone": 6_000,
        "subLevel": -60,
        "driveType": "Fold", "drive": 12, "driveTone": 9_000,
        "lowCut": 50, "highCut": 16_000, "lowBoost": 0, "velocity": 0.7,
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
