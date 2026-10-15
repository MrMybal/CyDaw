"""Prisme's factory sounds: each set through CyDAW 2's control port on a
Prisme card, heard offline (no sound out), saved as a device preset and copied
into content/factory/Presets/device/prism-synth (the folder is emptied first:
only the sounds below remain).

    python tools/factory_content/build_prism_presets.py [--exe "<CyDAW 2.exe>"] [--port 47836]

A sound is settings over the defaults ("init" first): keys as in
PrismSynth.h ("a.position", "m1.destination": "filter.cutoff"). mods() numbers
the modulation slots; drawn() gives an LFO a drawn shape (32 points, -1..1).

A slot's amount moves its destination across its whole range (0..1 of the
knob): for the cutoff 0.1 is about an octave, for "coarse" 1/72 a semitone,
for "fine" 0.005 a cent, for an LFO's rate 0.08 an octave. Envelopes,
velocity, the mod wheel, aftertouch and the macros read 0..1; LFOs -1..1 or
0..1 (unipolar, the default); "note" -1..1 around middle C.

Names are in English, descriptive, without a brand, product, artist or
hardware model name, and without / \\ : , * ? " < > | (they become files).
"""
from __future__ import annotations

import argparse
import json
import math
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


def harmonic(number: int) -> float:
    """Overtones' position for one harmonic alone (1..32)."""
    return (number - 1) / 31.0


def slot(number, source, destination, amount):
    return {f"m{number}.source": source, f"m{number}.destination": destination, f"m{number}.amount": amount}


def mods(*entries):
    """Modulation slots 1, 2, 3… from (source, destination, amount)."""
    assert len(entries) <= 16, "Prisme has 16 modulation slots"
    settings = {}
    for number, (source, destination, amount) in enumerate(entries, start=1):
        settings.update(slot(number, source, destination, amount))
    return settings


def steps(values):
    """A stepped shape: each value held over an equal share of the 32 points."""
    assert 32 % len(values) == 0
    return [value for value in values for _ in range(32 // len(values))]


def drawn(lfo, points):
    """An LFO's drawn shape (its shape set to "Drawn" separately)."""
    assert len(points) == 32
    return {f"lfo{lfo}.draw{index + 1}": round(max(-1.0, min(1.0, value)), 4) for index, value in enumerate(points)}


# Ducked at the start of each cycle (+1), back up (-1) within a third of it:
# the pump of a sidechained compressor, on the volume.
PUMP = [-1.0 + 2.0 * math.exp(-index / 4.0) for index in range(32)]


def gate(pattern):
    """Eight steps of four points: an open step (-1) closes on its last point,
    a closed one stays shut (+1): with a negative amount on the volume, a gate."""
    assert len(pattern) == 8
    points = []
    for step in pattern:
        points += [-1.0, -1.0, -1.0, 1.0] if step else [1.0, 1.0, 1.0, 1.0]
    return points


# Two vowel gestures a cycle ("ya-ow", then a shorter "wa"), for a formant filter.
VOWEL_CONTOUR = [-1.0, -0.6, 0.0, 0.6, 1.0, 1.0, 0.8, 0.5, 0.1, -0.3, -0.7, -1.0, -1.0, -1.0, -0.4, 0.4,
                 1.0, 0.6, 0.0, -0.6, -1.0, -1.0, -0.5, 0.3, 0.9, 0.9, 0.3, -0.5, -1.0, -1.0, -1.0, -1.0]


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
    "Digital Pluck": {
        "a.table": "Harmonic Sweep", "a.position": 0.85, "a.unison": 3, "a.detune": 0.15,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 350, "filter.resonance": 0.3,
        "env1.attack": 0.001, "env1.decay": 0.7, "env1.sustain": 0.0, "env1.release": 0.35,
        "env2.attack": 0.001, "env2.decay": 0.25, "env2.sustain": 0.0,
        **slot(1, "env2", "filter.cutoff", 0.6),
        **slot(2, "velocity", "filter.cutoff", 0.2),
    },
    "Evolving Pad": {
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
    "Formant Voice": {
        "a.table": "Formant Vowels", "a.position": 0.0, "a.unison": 3, "a.detune": 0.1,
        "filter.on": 1, "filter.type": "Formant", "filter.variation": 0.3, "filter.mix": 0.5, "filter.cutoff": 1_000,
        "filter.resonance": 0.3,
        "env1.attack": 0.08, "env1.release": 0.6,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.35,
        **slot(1, "macro1", "a.position", 1.0),
        **slot(2, "lfo1", "a.position", 0.2),
        **slot(3, "macro2", "filter.variation", 0.8),
    },
    "FM Bell": {
        "a.table": "FM Bells", "a.position": 0.55,
        "b.on": 1, "b.table": "Overtones", "b.position": 0.25, "b.level": 0.3,
        "env1.attack": 0.001, "env1.decay": 3.0, "env1.sustain": 0.0, "env1.release": 2.5,
        "env2.attack": 0.001, "env2.decay": 1.2, "env2.sustain": 0.0,
        **slot(1, "env2", "a.position", 0.4),
        **slot(2, "velocity", "a.position", 0.2),
    },
    "Boom Sub": {
        "a.position": 0.0, "a.octave": -1,
        "env1.attack": 0.001, "env1.decay": 1.6, "env1.sustain": 0.0, "env1.release": 0.4,
        "env2.attack": 0.0, "env2.decay": 0.07, "env2.sustain": 0.0,
        **slot(1, "env2", "a.coarse", 0.17),
        "a.warp": "Quantize", "a.warpAmount": 0.0,
        **slot(2, "macro1", "a.warpAmount", 0.5),
        "voice.mode": "Mono", "voice.glide": 0.05,
    },
    "Sync Arpeggio": {
        "a.table": "Sync Sweep", "a.position": 0.15,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 5_000,
        "env1.attack": 0.001, "env1.decay": 0.35, "env1.sustain": 0.2, "env1.release": 0.15,
        "env2.attack": 0.001, "env2.decay": 0.3, "env2.sustain": 0.0,
        **slot(1, "env2", "a.position", 0.55),
        **slot(2, "macro1", "a.position", 0.6),
    },
    "Brass": {
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
    "Organ": {
        "a.table": "Drawbar Organ", "a.position": 0.45,
        "env1.attack": 0.005, "env1.sustain": 1.0, "env1.release": 0.08,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 6.0, "lfo1.mode": "Free",
        **slot(1, "lfo1", "a.level", 0.12),
        **slot(2, "macro1", "a.position", 0.55),
    },
    "Noise and Breath": {
        "a.on": 0,
        "noise.on": 1, "noise.type": "Pink", "noise.level": 1.0, "noise.tone": 0.45, "voice.master": 1.0,
        "filter.on": 1, "filter.type": "Band pass 12", "filter.cutoff": 1_500, "filter.resonance": 0.15,
        "env1.attack": 0.4, "env1.release": 1.2,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.6,
        **slot(1, "lfo1", "filter.cutoff", 0.35),
        **slot(2, "note", "filter.cutoff", 0.4),
    },
    # With the third oscillator, the spectral morphs and the stereo LFOs.
    "Three Oscillators": {
        "a.position": SAW, "b.on": 1, "b.table": "Basic Shapes", "b.position": 1.0, "b.semi": 7, "b.level": 0.5,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -1, "c.level": 0.6,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 2_500, "filter.resonance": 0.2,
        "env2.decay": 0.5, "env2.sustain": 0.2, **slot(1, "env2", "filter.cutoff", 0.35),
    },
    "Spectral Bell": {
        "a.table": "FM Bells", "a.position": 0.3, "a.spectral": "Octave", "a.spectralAmount": 0.5,
        "c.on": 1, "c.table": "Harmonic Sweep", "c.position": 0.4, "c.warp": "FM (A)", "c.warpAmount": 0.3, "c.level": 0.4,
        "env1.attack": 0.002, "env1.decay": 2.5, "env1.sustain": 0.0, "env1.release": 2.0,
        # Struck harder, brighter; its FM shimmer fades first.
        "env2.attack": 0.001, "env2.decay": 0.9, "env2.sustain": 0.0,
        **mods(("env2", "c.warpAmount", 0.3), ("velocity", "c.warpAmount", 0.25), ("velocity", "a.spectralAmount", 0.2),
               ("modWheel", "a.position", 0.4)),
    },
    "Talking Formant": {
        "a.table": "Formant Vowels", "a.position": 0.4, "a.spectral": "Formant", "a.spectralAmount": 0.3,
        "lfo1.shape": "Triangle", "lfo1.sync": 0, "lfo1.rate": 0.5, "lfo1.stereo": 0.5,
        **slot(1, "lfo1", "a.spectralAmount", 0.35), **slot(2, "lfo1", "a.position", 0.25),
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 6_000,
    },
    "Stereo Pad": {
        "a.position": SAW, "a.unison": 7, "a.detune": 0.3, "b.on": 1, "b.table": "Harmonic Sweep", "b.position": 0.6,
        "b.level": 0.5, "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 1_500, "filter.resonance": 0.25,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.2, "lfo1.stereo": 1.0,
        **slot(1, "lfo1", "filter.cutoff", 0.3), "env1.attack": 0.8, "env1.release": 2.0,
    },
    "Spectral Pluck": {
        "a.table": "Harmonic Sweep", "a.position": 1.0, "a.spectral": "Low pass", "a.spectralAmount": 0.8,
        "env2.attack": 0.001, "env2.decay": 0.35, "env2.sustain": 0.0, **slot(1, "env2", "a.spectralAmount", -0.8),
        "env1.decay": 0.6, "env1.sustain": 0.0, "env1.release": 0.4,
    },
    "Moving Comb": {
        "a.position": SAW, "a.unison": 3, "a.detune": 0.2, "a.spectral": "Comb", "a.spectralAmount": 0.3,
        "lfo2.shape": "Triangle", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.15, "lfo2.stereo": 0.5,
        **slot(1, "lfo2", "a.spectralAmount", 0.4), "env1.attack": 0.3, "env1.release": 1.2,
    },
    "Hollow Odd Harmonics": {
        "voice.mode": "Legato", "voice.glide": 0.06, "a.position": SAW, "a.spectral": "Odd", "a.spectralAmount": 0.9,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 3_500, "lfo2.sync": 0, "lfo2.rate": 5.0,
        "lfo2.delay": 0.4, **slot(1, "lfo2", "a.fine", 0.05),
    },
    "Three-Oscillator Bass": {
        "voice.mode": "Mono", "a.position": SAW, "a.octave": -1, "b.on": 1, "b.table": "Basic Shapes", "b.position": 1.0,
        "b.octave": -1, "b.fine": 5, "b.level": 0.5, "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0,
        "c.octave": -2, "c.level": 0.7, "filter.on": 1, "filter.type": "Ladder", "filter.cutoff": 400,
        "filter.resonance": 0.25, "env2.decay": 0.3, "env2.sustain": 0.1, **slot(1, "env2", "filter.cutoff", 0.45),
    },
    "Octave Pad": {
        "a.table": "Harmonic Sweep", "a.position": 0.5, "a.spectral": "Octave", "a.spectralAmount": 0.6,
        "a.unison": 4, "a.detune": 0.18,
        "c.on": 1, "c.table": "Overtones", "c.octave": 1, "c.level": 0.3, "env1.attack": 1.0, "env1.release": 2.5,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 4_000,
        # The octave above breathes in and out; the wheel opens it.
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.12, "lfo1.unipolar": 0,
        **mods(("lfo1", "a.spectralAmount", 0.2), ("modWheel", "filter.cutoff", 0.25), ("aftertouch", "c.level", 0.3)),
    },
    "Stereo Arpeggio": {
        "a.table": "Digital Steps", "a.position": 0.5, "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 900,
        "filter.resonance": 0.5, "lfo1.shape": "Saw down", "lfo1.sync": 1, "lfo1.division": "1/16", "lfo1.stereo": 0.5,
        **slot(1, "lfo1", "filter.cutoff", 0.45), "env1.decay": 0.3, "env1.sustain": 0.3,
    },
    "High Crystal": {
        "a.position": SAW, "a.spectral": "High pass", "a.spectralAmount": 0.5, "c.on": 1, "c.table": "FM Bells",
        "c.octave": 1, "c.level": 0.35, "env1.attack": 0.05, "env1.release": 2.5, "a.unison": 3, "a.detune": 0.15,
        # The bell drifts across the field; velocity rings it, the wheel thins the saw.
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.3, "lfo1.unipolar": 0,
        **mods(("lfo1", "c.pan", 0.3), ("velocity", "c.level", 0.2), ("modWheel", "a.spectralAmount", 0.3)),
    },
    "Synthetic Voice": {
        "voice.mode": "Legato", "voice.glide": 0.08, "a.table": "Formant Vowels", "a.position": 0.2,
        "a.spectral": "Formant", "a.spectralAmount": 0.25, "lfo1.shape": "Smooth random", "lfo1.sync": 0,
        "lfo1.rate": 0.4, **slot(1, "lfo1", "a.position", 0.4), "lfo2.sync": 0, "lfo2.rate": 5.5, "lfo2.delay": 0.3,
        **slot(2, "lfo2", "a.fine", 0.06),
    },
    "Stereo Tremolo": {
        "a.table": "Analog PWM", "a.position": 0.4, "lfo1.shape": "Sine", "lfo1.sync": 1, "lfo1.division": "1/8",
        "lfo1.stereo": 1.0, **slot(1, "lfo1", "voice.master", -0.4), "env1.release": 0.8,
    },
    "Growing FM": {
        "a.table": "Basic Shapes", "a.position": 0.0, "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0,
        "c.semi": 7, "c.warp": "FM (A)", "c.warpAmount": 0.0, "c.level": 0.7, "a.level": 0.3,
        "env3.attack": 1.5, "env3.sustain": 1.0, **slot(1, "env3", "c.warpAmount", 0.7), "env1.attack": 0.2,
        "env1.release": 1.5,
    },
    "Spectral Organ": {
        "a.table": "Drawbar Organ", "a.position": 0.5, "a.spectral": "Even", "a.spectralAmount": 0.4,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 6.0, "lfo1.stereo": 0.5,
        **slot(1, "lfo1", "voice.master", 0.12), "env1.attack": 0.01, "env1.release": 0.15,
    },
    "Spectral Bass": {
        "voice.mode": "Mono", "a.table": "Growl", "a.position": 0.3, "a.octave": -1, "a.spectral": "Low pass",
        "a.spectralAmount": 0.5, "lfo1.sync": 1, "lfo1.division": "1/8", **slot(1, "lfo1", "a.spectralAmount", 0.4),
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -2, "c.level": 0.6,
    },

    # ---- Basses: drum and bass, dubstep, neuro, acid, synthwave, sub ----------------------
    # Two detuned saw stacks, a comb sweeping slowly through one of them (the "breath"),
    # a drifting stereo filter, the sub kept out of the filter.
    "Breathing Reese": {
        "a.position": SAW, "a.octave": -1, "a.unison": 4, "a.detune": 0.28, "a.blend": 0.7, "a.width": 0.5,
        "a.level": 0.7,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": -1, "b.unison": 4, "b.detune": 0.36,
        "b.width": 0.7, "b.level": 0.6, "b.spectral": "Comb", "b.spectralAmount": 0.2,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.55, "sub.direct": 1,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 520, "filter.resonance": 0.22,
        "filter.drive": 0.35, "filter.keytrack": 0.35,
        "env1.attack": 0.003, "env1.release": 0.12,
        "env2.attack": 0.005, "env2.decay": 0.45, "env2.sustain": 0.25,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.18, "lfo1.unipolar": 0,
        "lfo1.stereo": 0.35,
        "lfo2.shape": "Triangle", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.07, "lfo2.unipolar": 0,
        "voice.mode": "Legato", "voice.glide": 0.07, "voice.master": 0.7,
        **mods(("lfo1", "filter.cutoff", 0.1), ("lfo1", "a.detune", 0.1), ("lfo2", "b.spectralAmount", 0.35),
               ("env2", "filter.cutoff", 0.18), ("velocity", "filter.cutoff", 0.08), ("velocity", "filter.drive", 0.2),
               ("modWheel", "filter.cutoff", 0.3), ("modWheel", "a.detune", 0.25), ("macro1", "filter.drive", 0.5),
               ("macro2", "a.width", 0.5), ("macro2", "b.width", 0.3)),
    },
    # A growl table frequency-modulated by a sync sweep, ring-modulated squares, a drawn
    # half-bar pattern over position, FM and cutoff, hollow/full flips every sixteenth.
    "Neuro Growler": {
        "a.table": "Growl", "a.position": 0.25, "a.warp": "FM (B)", "a.warpAmount": 0.15, "a.unison": 2,
        "a.detune": 0.08, "a.octave": -1, "a.level": 0.7, "a.spectral": "Odd", "a.spectralAmount": 0.0,
        "b.on": 1, "b.table": "Sync Sweep", "b.position": 0.3, "b.octave": -1, "b.level": 0.25, "b.warp": "Bend +",
        "b.warpAmount": 0.3,
        "c.on": 1, "c.table": "Square Build", "c.position": 0.5, "c.octave": -1, "c.level": 0.25, "c.warp": "RM (A)",
        "c.warpAmount": 0.4,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.5, "sub.direct": 1,
        "filter.on": 1, "filter.type": "Ladder", "filter.cutoff": 1_100, "filter.resonance": 0.45, "filter.drive": 0.55,
        "filter.keytrack": 0.3,
        "env1.attack": 0.002, "env1.release": 0.1,
        "env2.attack": 0.0, "env2.decay": 0.25, "env2.sustain": 0.3,
        "lfo1.shape": "Drawn", "lfo1.sync": 1, "lfo1.division": "1/2", "lfo1.unipolar": 0, "lfo1.smooth": 0.15,
        **drawn(1, steps([-0.9, 0.5, -0.2, 1.0, -0.7, 0.3, 0.9, -0.4])),
        "lfo2.shape": "Sample & hold", "lfo2.sync": 1, "lfo2.division": "1/16", "lfo2.unipolar": 1,
        "voice.mode": "Mono", "voice.glide": 0.04, "voice.master": 0.6,
        **mods(("lfo1", "a.position", 0.35), ("lfo1", "filter.cutoff", 0.18), ("lfo1", "a.warpAmount", 0.25),
               ("lfo2", "b.position", 0.3), ("lfo2", "a.spectralAmount", 0.5), ("env2", "filter.cutoff", 0.15),
               ("velocity", "filter.drive", 0.25), ("modWheel", "filter.cutoff", 0.25), ("modWheel", "c.level", 0.4),
               ("macro1", "a.warpAmount", 0.5), ("macro2", "c.warpAmount", 0.5)),
    },
    # A quarter-note wobble (half time at 140) that also turns the saw into a square and
    # narrows the pulse; a slow two-bar drift of resonance.
    "Half-Time Wobble": {
        "a.position": 0.75, "a.unison": 3, "a.detune": 0.15, "a.octave": -1, "a.level": 0.7,
        "b.on": 1, "b.table": "Square Build", "b.position": 0.7, "b.octave": -1, "b.level": 0.5, "b.warp": "PWM",
        "b.warpAmount": 0.2,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.6, "sub.direct": 1,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 180, "filter.resonance": 0.55,
        "filter.drive": 0.45, "filter.keytrack": 0.2,
        "env1.release": 0.08,
        "lfo1.shape": "Sine", "lfo1.sync": 1, "lfo1.division": "1/4", "lfo1.phase": 0.75,
        "lfo2.shape": "Triangle", "lfo2.sync": 1, "lfo2.division": "2/1", "lfo2.unipolar": 0,
        "voice.mode": "Mono", "voice.glide": 0.03, "voice.master": 0.65,
        **mods(("lfo1", "filter.cutoff", 0.5), ("lfo1", "a.position", 0.25), ("lfo1", "b.warpAmount", 0.4),
               ("lfo2", "filter.resonance", 0.1), ("velocity", "filter.cutoff", 0.05), ("modWheel", "filter.cutoff", 0.2),
               ("aftertouch", "filter.cutoff", 0.15), ("macro1", "filter.drive", 0.4), ("macro2", "filter.resonance", 0.3)),
    },
    # The wobble's speed on the mod wheel (and aftertouch): slow, then up to a buzz.
    "Wheel-Speed Wobble": {
        "a.table": "Growl", "a.position": 0.4, "a.octave": -1, "a.unison": 2, "a.detune": 0.1, "a.level": 0.7,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": -1, "b.level": 0.55,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.55, "sub.direct": 1,
        "filter.on": 1, "filter.type": "Ladder", "filter.cutoff": 200, "filter.resonance": 0.5, "filter.drive": 0.4,
        "filter.keytrack": 0.25,
        "env1.release": 0.1,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 1.5, "lfo1.phase": 0.75,
        "lfo2.shape": "Smooth random", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.5, "lfo2.unipolar": 0,
        "voice.mode": "Mono", "voice.glide": 0.05,
        **mods(("lfo1", "filter.cutoff", 0.5), ("lfo1", "a.position", 0.4), ("modWheel", "lfo1.rate", 0.33),
               ("aftertouch", "lfo1.rate", 0.16), ("lfo2", "a.detune", 0.08), ("velocity", "filter.cutoff", 0.06),
               ("macro1", "filter.resonance", 0.3), ("macro2", "filter.drive", 0.5)),
    },
    # A sine with a touch of asymmetry and a triangle, a little pitch thump, warm saturation;
    # harder notes and the wheel bring out harmonics for small speakers.
    "Round Warm Sub": {
        "a.position": 0.0, "a.octave": -1, "a.random": 0.0, "a.warp": "Asym +", "a.warpAmount": 0.15, "a.level": 0.85,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": 0.33, "b.octave": -1, "b.random": 0.0, "b.level": 0.2,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 260, "filter.resonance": 0.05,
        "filter.drive": 0.25, "filter.keytrack": 0.6,
        "env1.attack": 0.002, "env1.release": 0.1,
        "env2.attack": 0.0, "env2.decay": 0.06, "env2.sustain": 0.0,
        "voice.mode": "Mono", "voice.glide": 0.06, "voice.master": 0.8,
        **mods(("env2", "a.coarse", 0.04), ("env2", "b.coarse", 0.04), ("velocity", "a.warpAmount", 0.2),
               ("modWheel", "filter.cutoff", 0.2), ("modWheel", "a.warpAmount", 0.3), ("macro1", "filter.drive", 0.5),
               ("aftertouch", "b.level", 0.3)),
    },
    # A saw into a resonant ladder snapped by a short envelope; accents (velocity) drive
    # it harder, overlapping notes slide without a new attack.
    "Squelchy Acid Line": {
        "a.position": SAW, "a.octave": -1, "a.level": 0.75,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": 1.0, "b.octave": -1, "b.level": 0.0,
        "filter.on": 1, "filter.type": "Ladder", "filter.cutoff": 260, "filter.resonance": 0.72, "filter.drive": 0.4,
        "filter.keytrack": 0.3,
        "env1.attack": 0.001, "env1.decay": 0.5, "env1.sustain": 0.6, "env1.release": 0.06,
        "env2.attack": 0.0, "env2.decay": 0.22, "env2.sustain": 0.0,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 1, "lfo1.division": "4/1", "lfo1.unipolar": 0,
        "voice.mode": "Legato", "voice.glide": 0.06, "voice.master": 0.6,
        **mods(("env2", "filter.cutoff", 0.42), ("velocity", "filter.cutoff", 0.12), ("velocity", "filter.drive", 0.3),
               ("velocity", "filter.resonance", 0.1), ("modWheel", "filter.cutoff", 0.35),
               ("aftertouch", "filter.resonance", 0.15), ("lfo1", "filter.cutoff", 0.06), ("macro1", "b.level", 0.7),
               ("macro1", "a.level", -0.5)),
    },
    # A growl through a formant filter whose vowel follows a drawn half-bar gesture:
    # the bass "talks"; a synced saw adds bite on eighths.
    "Talking Growl": {
        "a.table": "Growl", "a.position": 0.35, "a.octave": -1, "a.warp": "FM (B)", "a.warpAmount": 0.2,
        "a.unison": 2, "a.detune": 0.07, "a.level": 0.7,
        "b.on": 1, "b.table": "Formant Vowels", "b.position": 0.5, "b.octave": -1, "b.level": 0.4,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": SAW, "c.octave": -1, "c.level": 0.3, "c.warp": "Sync",
        "c.warpAmount": 0.3,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.5, "sub.direct": 1,
        "filter.on": 1, "filter.type": "Formant", "filter.cutoff": 900, "filter.resonance": 0.45,
        "filter.variation": 0.2, "filter.mix": 0.85, "filter.drive": 0.4,
        "env1.release": 0.1,
        "env2.attack": 0.0, "env2.decay": 0.3, "env2.sustain": 0.2,
        "lfo1.shape": "Drawn", "lfo1.sync": 1, "lfo1.division": "1/2", "lfo1.smooth": 0.1, **drawn(1, VOWEL_CONTOUR),
        "lfo2.shape": "Triangle", "lfo2.sync": 1, "lfo2.division": "1/8", "lfo2.unipolar": 0,
        "voice.mode": "Mono", "voice.glide": 0.04,
        **mods(("lfo1", "filter.variation", 0.7), ("lfo1", "a.position", 0.3), ("lfo2", "c.warpAmount", 0.25),
               ("env2", "filter.cutoff", 0.15), ("modWheel", "a.warpAmount", 0.4), ("velocity", "filter.drive", 0.2),
               ("aftertouch", "filter.cutoff", 0.12), ("macro1", "filter.mix", -0.5)),
    },
    # Wide detuned pulses that dip in pitch and rise into each note, always gliding:
    # the old rave siren, for trance and hardcore.
    "Rave Siren": {
        "a.table": "Analog PWM", "a.position": 0.15, "a.unison": 7, "a.detune": 0.5, "a.blend": 0.55, "a.width": 1.0,
        "a.octave": -1, "a.level": 0.6,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.unison": 5, "b.detune": 0.45, "b.blend": 0.6,
        "b.level": 0.45,
        "c.on": 1, "c.table": "Analog PWM", "c.position": 0.6, "c.octave": -2, "c.level": 0.3,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 3_200, "filter.resonance": 0.15,
        "filter.keytrack": 0.3,
        "env1.attack": 0.01, "env1.release": 0.3,
        "env3.attack": 0.0, "env3.decay": 0.35, "env3.sustain": 0.0,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.6, "lfo1.unipolar": 0,
        "lfo2.shape": "Sine", "lfo2.sync": 0, "lfo2.rate": 5.5, "lfo2.delay": 0.3, "lfo2.rise": 0.4, "lfo2.unipolar": 0,
        "voice.mode": "Legato", "voice.glide": 0.12, "voice.glideAlways": 1, "voice.bendRange": 12, "voice.master": 0.5,
        **mods(("env3", "a.coarse", -0.035), ("env3", "b.coarse", -0.035), ("env3", "c.coarse", -0.035),
               ("lfo1", "a.position", 0.12), ("lfo2", "a.fine", 0.04), ("lfo2", "b.fine", 0.04),
               ("modWheel", "a.detune", 0.3), ("aftertouch", "filter.cutoff", 0.15), ("velocity", "filter.cutoff", 0.08)),
    },
    # Two-operator FM: a silent sine an octave up (A) modulates the carrier (C) and a growl
    # (B); the index follows an envelope, velocity, a stepped quarter-note pattern and the wheel.
    "Metallic FM Bass": {
        "a.position": 0.0, "a.random": 0.0, "a.level": 0.0,
        "b.on": 1, "b.table": "Growl", "b.position": 0.2, "b.octave": -1, "b.level": 0.25, "b.warp": "FM (A)",
        "b.warpAmount": 0.2,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -1, "c.random": 0.0, "c.level": 0.8,
        "c.warp": "FM (A)", "c.warpAmount": 0.35,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.45, "sub.direct": 1,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 2_500, "filter.resonance": 0.1,
        "filter.drive": 0.3, "filter.keytrack": 0.5,
        "env1.attack": 0.002, "env1.release": 0.1,
        "env2.attack": 0.0, "env2.decay": 0.35, "env2.sustain": 0.2,
        "lfo1.shape": "Drawn", "lfo1.sync": 1, "lfo1.division": "1/4", "lfo1.unipolar": 1, "lfo1.smooth": 0.1,
        **drawn(1, steps([1.0, -1.0, 0.2, -0.6, 0.7, -1.0, -0.2, 0.4])),
        "voice.mode": "Mono", "voice.glide": 0.03,
        **mods(("env2", "c.warpAmount", 0.35), ("env2", "b.warpAmount", 0.25), ("lfo1", "c.warpAmount", 0.2),
               ("velocity", "c.warpAmount", 0.2), ("modWheel", "c.warpAmount", 0.4), ("macro1", "a.coarse", 7 / 72),
               ("aftertouch", "filter.cutoff", 0.15)),
    },
    # A saw and a pulse an octave below through a snappy ladder: sixteenth-note lines.
    "Synthwave Driving Bass": {
        "a.position": SAW, "a.octave": -1, "a.unison": 2, "a.detune": 0.06, "a.level": 0.7,
        "b.on": 1, "b.table": "Analog PWM", "b.position": 0.2, "b.octave": -2, "b.level": 0.5,
        "filter.on": 1, "filter.type": "Ladder", "filter.cutoff": 420, "filter.resonance": 0.3, "filter.drive": 0.3,
        "filter.keytrack": 0.4,
        "env1.attack": 0.001, "env1.decay": 0.3, "env1.sustain": 0.55, "env1.release": 0.08,
        "env2.attack": 0.0, "env2.decay": 0.18, "env2.sustain": 0.15,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.3, "lfo1.unipolar": 0,
        "voice.mode": "Mono",
        **mods(("env2", "filter.cutoff", 0.3), ("velocity", "filter.cutoff", 0.1), ("lfo1", "b.position", 0.2),
               ("modWheel", "filter.cutoff", 0.25), ("macro1", "filter.resonance", 0.4),
               ("aftertouch", "filter.drive", 0.3)),
    },

    # ---- Leads ----------------------------------------------------------------------------
    # Nine plus seven plus five unison voices, an octave up and down, air on top; the
    # detune breathes, a delayed vibrato, the wheel widens it.
    "Trance Supersaw": {
        "a.position": SAW, "a.unison": 9, "a.detune": 0.42, "a.blend": 0.85, "a.width": 1.0, "a.level": 0.6,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": 1, "b.unison": 7, "b.detune": 0.35,
        "b.blend": 0.8, "b.level": 0.35,
        "c.on": 1, "c.table": "Square Build", "c.position": 0.8, "c.octave": -1, "c.unison": 5, "c.detune": 0.3,
        "c.level": 0.3,
        "sub.on": 1, "sub.octave": -1, "sub.level": 0.25,
        "noise.on": 1, "noise.type": "White", "noise.tone": 0.75, "noise.level": 0.06,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 7_000, "filter.resonance": 0.12,
        "filter.keytrack": 0.3,
        "env1.attack": 0.004, "env1.decay": 0.8, "env1.sustain": 0.85, "env1.release": 0.45,
        "env2.attack": 0.001, "env2.decay": 0.6, "env2.sustain": 0.4,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.25, "lfo1.unipolar": 0,
        "lfo2.shape": "Sine", "lfo2.sync": 0, "lfo2.rate": 5.2, "lfo2.delay": 0.5, "lfo2.rise": 0.5, "lfo2.unipolar": 0,
        "voice.voices": 10, "voice.master": 0.65,
        **mods(("env2", "filter.cutoff", 0.1), ("lfo1", "a.detune", 0.06), ("lfo1", "b.detune", 0.06),
               ("lfo2", "a.fine", 0.025), ("lfo2", "b.fine", 0.025), ("velocity", "filter.cutoff", 0.08),
               ("modWheel", "a.detune", 0.2), ("aftertouch", "filter.cutoff", 0.1), ("macro1", "filter.cutoff", 0.3)),
    },
    # Hard sync swept by an envelope, a vowel table FM'd by it, a driven resonant band
    # pass; unstable stereo jitter, a delayed vibrato, bends of an octave.
    "Screaming Lead": {
        "a.table": "Sync Sweep", "a.position": 0.35, "a.warp": "Sync", "a.warpAmount": 0.25, "a.unison": 3,
        "a.detune": 0.12, "a.level": 0.8,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": 1, "b.level": 0.25, "b.warp": "Bend +",
        "b.warpAmount": 0.3,
        "c.on": 1, "c.table": "Formant Vowels", "c.position": 0.6, "c.warp": "FM (A)", "c.warpAmount": 0.3,
        "c.level": 0.3, "c.spectral": "Formant", "c.spectralAmount": 0.3,
        "filter.on": 1, "filter.type": "Band pass 24", "filter.cutoff": 2_200, "filter.resonance": 0.55,
        "filter.drive": 0.6, "filter.mix": 0.65, "filter.keytrack": 0.5,
        "env1.attack": 0.005, "env1.sustain": 0.9, "env1.release": 0.2,
        "env2.attack": 0.01, "env2.decay": 0.4, "env2.sustain": 0.5,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 3.0, "lfo1.unipolar": 0,
        "lfo1.stereo": 0.5,
        "lfo2.shape": "Sine", "lfo2.sync": 0, "lfo2.rate": 6.0, "lfo2.delay": 0.25, "lfo2.rise": 0.3, "lfo2.unipolar": 0,
        "voice.mode": "Legato", "voice.glide": 0.07, "voice.bendRange": 12, "voice.master": 1.0,
        **mods(("env2", "a.warpAmount", 0.3), ("env2", "filter.cutoff", 0.12), ("lfo1", "a.position", 0.08),
               ("lfo1", "filter.cutoff", 0.05), ("lfo2", "a.fine", 0.05), ("lfo2", "b.fine", 0.05),
               ("lfo2", "c.fine", 0.05), ("modWheel", "a.warpAmount", 0.5), ("aftertouch", "filter.resonance", 0.2),
               ("velocity", "filter.drive", 0.25)),
    },
    # A plucked attack (filter and wavetable envelopes) that settles into a sung tone
    # with a delayed vibrato; mono, sliding between overlapping notes.
    "Singing Pluck Lead": {
        "a.table": "Harmonic Sweep", "a.position": 0.5, "a.unison": 3, "a.detune": 0.12, "a.level": 0.7,
        "b.on": 1, "b.table": "Square Build", "b.position": 0.6, "b.octave": 1, "b.level": 0.3,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -1, "c.level": 0.3,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 900, "filter.resonance": 0.25,
        "filter.keytrack": 0.5,
        "env1.attack": 0.001, "env1.decay": 0.9, "env1.sustain": 0.4, "env1.release": 0.3,
        "env2.attack": 0.001, "env2.decay": 0.22, "env2.sustain": 0.15,
        "env3.attack": 0.0, "env3.decay": 0.5, "env3.sustain": 0.3,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 5.5, "lfo1.delay": 0.35, "lfo1.rise": 0.4, "lfo1.unipolar": 0,
        "voice.mode": "Mono", "voice.glide": 0.05,
        **mods(("env2", "filter.cutoff", 0.4), ("env3", "a.position", 0.4), ("lfo1", "a.fine", 0.04),
               ("lfo1", "b.fine", 0.04), ("velocity", "filter.cutoff", 0.15), ("modWheel", "filter.cutoff", 0.2),
               ("aftertouch", "filter.resonance", 0.15), ("macro1", "b.level", 0.4)),
    },
    # A bent square with an octave of even harmonics and a sparkle two octaves up, each
    # note chirping down an octave into place, always gliding, a fast vibrato.
    "Sugary Hyperpop Lead": {
        "a.position": 1.0, "a.unison": 2, "a.detune": 0.1, "a.warp": "Bend +", "a.warpAmount": 0.2, "a.level": 0.6,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": 1, "b.level": 0.3, "b.spectral": "Even",
        "b.spectralAmount": 0.4,
        "c.on": 1, "c.table": "Overtones", "c.position": harmonic(2), "c.octave": 1, "c.level": 0.2,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 6_000, "filter.resonance": 0.2,
        "filter.drive": 0.3, "filter.keytrack": 0.3,
        "env1.attack": 0.002, "env1.decay": 0.6, "env1.sustain": 0.8, "env1.release": 0.15,
        "env3.attack": 0.0, "env3.decay": 0.08, "env3.sustain": 0.0,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 7.0, "lfo1.delay": 0.15, "lfo1.rise": 0.1, "lfo1.unipolar": 0,
        "voice.mode": "Legato", "voice.glide": 0.05, "voice.glideAlways": 1, "voice.bendRange": 12,
        **mods(("env3", "a.coarse", 12 / 72), ("env3", "b.coarse", 12 / 72), ("env3", "c.coarse", 12 / 72),
               ("lfo1", "a.fine", 0.06), ("lfo1", "b.fine", 0.06), ("modWheel", "a.warpAmount", 0.5),
               ("velocity", "filter.cutoff", 0.1), ("aftertouch", "lfo1.rate", 0.1), ("macro1", "b.spectralAmount", 0.6)),
    },
    # A drifting pulse width beside a slightly sharp saw, a soft filter swell, a slow
    # delayed vibrato and a gentle glide.
    "Nostalgic Synthwave Lead": {
        "a.table": "Analog PWM", "a.position": 0.2, "a.unison": 3, "a.detune": 0.15, "a.level": 0.6,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.fine": 8, "b.unison": 2, "b.detune": 0.1,
        "b.level": 0.45,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 2_600, "filter.resonance": 0.2,
        "filter.drive": 0.15, "filter.keytrack": 0.4,
        "env1.attack": 0.01, "env1.decay": 1.0, "env1.sustain": 0.85, "env1.release": 0.35,
        "env2.attack": 0.02, "env2.decay": 0.6, "env2.sustain": 0.5,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.8, "lfo1.unipolar": 0,
        "lfo1.stereo": 0.4,
        "lfo2.shape": "Sine", "lfo2.sync": 0, "lfo2.rate": 5.0, "lfo2.delay": 0.4, "lfo2.rise": 0.6, "lfo2.unipolar": 0,
        "voice.mode": "Legato", "voice.glide": 0.06,
        **mods(("lfo1", "a.position", 0.25), ("lfo1", "filter.cutoff", 0.03), ("lfo2", "a.fine", 0.035),
               ("lfo2", "b.fine", 0.035), ("env2", "filter.cutoff", 0.15), ("velocity", "filter.cutoff", 0.06),
               ("modWheel", "filter.cutoff", 0.2), ("aftertouch", "filter.cutoff", 0.1), ("macro1", "b.detune", 0.3)),
    },

    # ---- Keys and plucks ------------------------------------------------------------------
    # Wide saw and square chords pumping on every quarter (a drawn sidechain shape on the
    # volume and the filter); the wheel opens them.
    "Pumping Future Bass Chords": {
        "a.position": SAW, "a.unison": 7, "a.detune": 0.4, "a.width": 1.0, "a.level": 0.55,
        "b.on": 1, "b.table": "Square Build", "b.position": 0.75, "b.unison": 5, "b.detune": 0.3, "b.level": 0.35,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -1, "c.level": 0.25,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 1_800, "filter.resonance": 0.2,
        "filter.keytrack": 0.3,
        "env1.attack": 0.003, "env1.decay": 1.2, "env1.sustain": 0.7, "env1.release": 0.35,
        "env2.attack": 0.001, "env2.decay": 0.35, "env2.sustain": 0.4,
        "lfo1.shape": "Drawn", "lfo1.sync": 1, "lfo1.division": "1/4", "lfo1.unipolar": 1, **drawn(1, PUMP),
        "lfo2.shape": "Sine", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.3, "lfo2.unipolar": 0,
        "voice.voices": 12, "voice.master": 0.8,
        **mods(("lfo1", "voice.master", -0.55), ("lfo1", "filter.cutoff", -0.08), ("env2", "filter.cutoff", 0.18),
               ("lfo2", "a.detune", 0.05), ("modWheel", "filter.cutoff", 0.3), ("velocity", "filter.cutoff", 0.06),
               ("macro1", "filter.resonance", 0.3), ("aftertouch", "b.position", 0.25)),
    },
    # FM bells whose brightness falls fast, a pure fundamental and a high harmonic, the
    # lows thinned; the partials drift across the stereo field.
    "Glass Pluck": {
        "a.table": "FM Bells", "a.position": 0.2, "a.unison": 2, "a.detune": 0.06, "a.width": 1.0, "a.level": 0.6,
        "b.on": 1, "b.table": "Overtones", "b.position": 0.0, "b.level": 0.5,
        "c.on": 1, "c.table": "Overtones", "c.position": harmonic(3), "c.octave": 1, "c.level": 0.2,
        "filter.on": 1, "filter.type": "High pass 12", "filter.cutoff": 180, "filter.resonance": 0.1,
        "filter.keytrack": 0.5,
        "env1.attack": 0.001, "env1.decay": 1.4, "env1.sustain": 0.0, "env1.release": 1.0, "env1.decayCurve": 0.7,
        "env2.attack": 0.0, "env2.decay": 0.3, "env2.sustain": 0.0,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.4, "lfo1.unipolar": 0,
        "voice.velocity": 0.8, "voice.master": 0.6,
        **mods(("env2", "a.position", 0.4), ("velocity", "a.position", 0.25), ("lfo1", "a.pan", 0.25),
               ("lfo1", "c.pan", -0.25), ("env2", "c.level", 0.3), ("modWheel", "c.level", 0.4),
               ("macro1", "a.position", 0.3), ("random", "b.fine", 0.02)),
    },
    # Two-operator FM (a silent sine modulating C) for the body, a 14th harmonic for the
    # tine's ding; harder notes bark, a stereo tremolo the wheel speeds up.
    "Soft Electric Piano": {
        "a.position": 0.0, "a.random": 0.0, "a.level": 0.0,
        "b.on": 1, "b.table": "Overtones", "b.position": harmonic(14), "b.random": 0.0, "b.level": 0.0,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.random": 0.0, "c.level": 0.75, "c.warp": "FM (A)",
        "c.warpAmount": 0.12,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 5_000, "filter.resonance": 0.05,
        "filter.keytrack": 0.4,
        "env1.attack": 0.002, "env1.decay": 4.0, "env1.sustain": 0.15, "env1.release": 0.4,
        "env2.attack": 0.0, "env2.decay": 1.2, "env2.sustain": 0.15,
        "env3.attack": 0.0, "env3.decay": 0.12, "env3.sustain": 0.0,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 4.5, "lfo1.stereo": 1.0,
        "voice.velocity": 0.85,
        **mods(("env2", "c.warpAmount", 0.3), ("env3", "b.level", 0.5), ("velocity", "c.warpAmount", 0.3),
               ("lfo1", "voice.master", -0.18), ("macro1", "c.warpAmount", 0.3), ("modWheel", "lfo1.rate", 0.1)),
    },
    # Drawbars with a percussive upper octave and a key click, a rotating speaker (pan,
    # pitch and level from a stereo LFO) the wheel speeds up, an overdrive macro.
    "Rotary Organ": {
        "a.table": "Drawbar Organ", "a.position": 0.4, "a.level": 0.7,
        "b.on": 1, "b.table": "Drawbar Organ", "b.position": 0.85, "b.octave": 1, "b.level": 0.1,
        "noise.on": 1, "noise.type": "White", "noise.tone": 0.8, "noise.level": 0.0,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 6_000, "filter.resonance": 0.05,
        "filter.drive": 0.25, "filter.keytrack": 0.2,
        "env1.attack": 0.004, "env1.sustain": 1.0, "env1.release": 0.06,
        "env3.attack": 0.0, "env3.decay": 0.25, "env3.sustain": 0.0,
        "env4.attack": 0.0, "env4.decay": 0.02, "env4.sustain": 0.0,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.8, "lfo1.unipolar": 0,
        "lfo1.stereo": 0.5,
        "voice.velocity": 0.0,
        **mods(("env3", "b.level", 0.35), ("env4", "noise.level", 0.25), ("lfo1", "a.pan", 0.3), ("lfo1", "a.fine", 0.02),
               ("lfo1", "voice.master", 0.06), ("modWheel", "lfo1.rate", 0.24), ("macro1", "filter.drive", 0.6),
               ("macro2", "a.position", 0.5)),
    },
    # A triangle with a short octave and a fourth harmonic that die first, a mallet
    # click of noise, each note placed a little elsewhere in the stereo field.
    "Digital Marimba": {
        "a.position": 0.33, "a.random": 0.0, "a.level": 0.7, "a.spectral": "Octave", "a.spectralAmount": 0.1,
        "a.pan": -0.15,
        "c.on": 1, "c.table": "Overtones", "c.position": harmonic(4), "c.random": 0.0, "c.level": 0.05,
        "noise.on": 1, "noise.type": "White", "noise.tone": 0.65, "noise.level": 0.0,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 3_500, "filter.resonance": 0.1,
        "filter.keytrack": 0.6,
        "env1.attack": 0.001, "env1.decay": 0.7, "env1.sustain": 0.0, "env1.release": 0.5,
        "env2.attack": 0.0, "env2.decay": 0.12, "env2.sustain": 0.0,
        "env3.attack": 0.0, "env3.decay": 0.015, "env3.sustain": 0.0,
        **mods(("env2", "c.level", 0.5), ("env2", "a.spectralAmount", 0.4), ("env3", "noise.level", 0.4),
               ("velocity", "filter.cutoff", 0.15), ("velocity", "c.level", 0.15), ("random", "a.pan", 0.15),
               ("modWheel", "a.spectralAmount", 0.4)),
    },

    # ---- Pads -----------------------------------------------------------------------------
    # Vowels and a harmonic sweep with a slowly travelling comb, a breath of noise, a
    # filter that blooms over seconds and swells on an eleven-second cycle.
    "Breathing Pad": {
        "a.table": "Formant Vowels", "a.position": 0.3, "a.unison": 6, "a.detune": 0.25, "a.width": 1.0, "a.level": 0.5,
        "b.on": 1, "b.table": "Harmonic Sweep", "b.position": 0.5, "b.unison": 5, "b.detune": 0.3, "b.level": 0.4,
        "b.spectral": "Comb", "b.spectralAmount": 0.25,
        "c.on": 1, "c.table": "Spectral Noise", "c.position": 0.4, "c.octave": 1, "c.unison": 3, "c.detune": 0.4,
        "c.level": 0.12,
        "noise.on": 1, "noise.type": "Pink", "noise.tone": 0.7, "noise.level": 0.05,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 2_000, "filter.resonance": 0.15,
        "filter.keytrack": 0.3,
        "env1.attack": 1.4, "env1.attackCurve": 0.3, "env1.decay": 2.0, "env1.sustain": 0.85, "env1.release": 3.0,
        "env2.attack": 3.5, "env2.decay": 4.0, "env2.sustain": 0.6,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.09, "lfo1.unipolar": 0,
        "lfo1.stereo": 0.25,
        "lfo2.shape": "Smooth random", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.25, "lfo2.unipolar": 0,
        "lfo3.shape": "Triangle", "lfo3.mode": "Free", "lfo3.sync": 0, "lfo3.rate": 0.05, "lfo3.unipolar": 0,
        "voice.voices": 12, "voice.master": 0.7,
        **mods(("env2", "filter.cutoff", 0.15), ("lfo1", "filter.cutoff", 0.12), ("lfo1", "voice.master", 0.06),
               ("lfo1", "noise.level", 0.05), ("lfo2", "a.position", 0.25), ("lfo3", "b.spectralAmount", 0.3),
               ("modWheel", "filter.cutoff", 0.3), ("aftertouch", "c.level", 0.3), ("velocity", "filter.cutoff", 0.05),
               ("macro1", "b.spectralAmount", 0.5)),
    },
    # Growl and hollow squares an octave down, brown rumble, a filter that takes six
    # seconds to open; the wheel brings it forward, aftertouch thickens the rumble.
    "Dark Cinematic Pad": {
        "a.table": "Growl", "a.position": 0.15, "a.unison": 4, "a.detune": 0.2, "a.octave": -1, "a.level": 0.5,
        "b.on": 1, "b.table": "Square Build", "b.position": 0.3, "b.unison": 3, "b.detune": 0.25, "b.level": 0.35,
        "b.spectral": "Odd", "b.spectralAmount": 0.5,
        "c.on": 1, "c.table": "Spectral Noise", "c.position": 0.6, "c.octave": -1, "c.level": 0.12,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.3,
        "noise.on": 1, "noise.type": "Brown", "noise.tone": 0.3, "noise.level": 0.1,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 380, "filter.resonance": 0.3,
        "filter.drive": 0.2, "filter.keytrack": 0.25,
        "env1.attack": 2.0, "env1.decay": 3.0, "env1.sustain": 0.9, "env1.release": 4.0,
        "env3.attack": 6.0, "env3.decay": 6.0, "env3.sustain": 0.6,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.12, "lfo1.unipolar": 0,
        "lfo1.stereo": 0.7,
        "lfo2.shape": "Sine", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.035, "lfo2.unipolar": 0,
        "voice.voices": 10,
        **mods(("env3", "filter.cutoff", 0.28), ("lfo1", "a.position", 0.2), ("lfo1", "filter.cutoff", 0.06),
               ("lfo2", "b.spectralAmount", 0.35), ("modWheel", "filter.cutoff", 0.25),
               ("modWheel", "filter.resonance", 0.15), ("aftertouch", "noise.level", 0.2), ("macro1", "filter.drive", 0.5),
               ("velocity", "filter.cutoff", 0.05)),
    },
    # Single harmonics climbing slowly, a thinned saw stack, a bell an octave up that
    # wanders between the speakers, hiss; no lows, a vibrato that comes late.
    "Icy Pad": {
        "a.table": "Overtones", "a.position": harmonic(4), "a.unison": 4, "a.detune": 0.15, "a.width": 1.0,
        "a.level": 0.35,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.unison": 7, "b.detune": 0.35, "b.level": 0.45,
        "b.spectral": "High pass", "b.spectralAmount": 0.35,
        "c.on": 1, "c.table": "FM Bells", "c.position": 0.6, "c.octave": 1, "c.level": 0.2,
        "noise.on": 1, "noise.type": "Hiss", "noise.tone": 0.85, "noise.level": 0.05,
        "filter.on": 1, "filter.type": "High pass 12", "filter.cutoff": 250, "filter.resonance": 0.1,
        "filter.keytrack": 0.3,
        "env1.attack": 1.2, "env1.decay": 2.0, "env1.sustain": 0.8, "env1.release": 3.5,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.07, "lfo1.unipolar": 1,
        "lfo2.shape": "Smooth random", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.6, "lfo2.unipolar": 0,
        "lfo3.shape": "Sine", "lfo3.sync": 0, "lfo3.rate": 4.5, "lfo3.delay": 1.0, "lfo3.rise": 2.0, "lfo3.unipolar": 0,
        "voice.voices": 10,
        **mods(("lfo1", "a.position", 0.25), ("lfo2", "c.pan", 0.4), ("lfo2", "b.spectralAmount", 0.15),
               ("lfo3", "a.fine", 0.03), ("lfo3", "b.fine", 0.03), ("modWheel", "b.spectralAmount", 0.3),
               ("aftertouch", "c.level", 0.3), ("velocity", "c.level", 0.15), ("macro1", "noise.level", 0.3)),
    },
    # Two vowel stacks through a formant filter whose vowel drifts by itself (and with
    # the wheel), a choral vibrato, a little breath.
    "Vowel Choir": {
        "a.table": "Formant Vowels", "a.position": 0.1, "a.unison": 7, "a.detune": 0.22, "a.width": 1.0, "a.level": 0.7,
        "b.on": 1, "b.table": "Formant Vowels", "b.position": 0.45, "b.unison": 5, "b.detune": 0.3, "b.level": 0.3,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.33, "c.octave": -1, "c.level": 0.25,
        "noise.on": 1, "noise.type": "Pink", "noise.tone": 0.6, "noise.level": 0.04,
        "filter.on": 1, "filter.type": "Formant", "filter.variation": 0.0, "filter.cutoff": 1_000,
        "filter.resonance": 0.35, "filter.mix": 0.5,
        "env1.attack": 0.6, "env1.decay": 1.0, "env1.sustain": 0.9, "env1.release": 1.6,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.2, "lfo1.unipolar": 1,
        "lfo1.stereo": 0.4,
        "lfo2.shape": "Sine", "lfo2.sync": 0, "lfo2.rate": 5.0, "lfo2.delay": 0.5, "lfo2.rise": 0.8, "lfo2.unipolar": 0,
        "lfo3.shape": "Sine", "lfo3.mode": "Free", "lfo3.sync": 0, "lfo3.rate": 0.13, "lfo3.unipolar": 0,
        "voice.voices": 12, "voice.master": 1.0,
        **mods(("lfo1", "filter.variation", 0.5), ("lfo2", "a.fine", 0.03), ("lfo2", "b.fine", 0.035),
               ("lfo3", "a.position", 0.15), ("modWheel", "filter.variation", 0.5), ("aftertouch", "filter.cutoff", 0.1),
               ("macro1", "filter.mix", 0.4), ("velocity", "noise.level", 0.04)),
    },
    # Detuned saws, a chorused pulse and a square below, a filter swell, analog drift.
    "Warm Synthwave Pad": {
        "a.position": SAW, "a.unison": 5, "a.detune": 0.3, "a.width": 0.8, "a.level": 0.5,
        "b.on": 1, "b.table": "Analog PWM", "b.position": 0.25, "b.unison": 3, "b.detune": 0.2, "b.fine": -6,
        "b.level": 0.45,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 1.0, "c.octave": -1, "c.level": 0.2,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 1_100, "filter.resonance": 0.2,
        "filter.drive": 0.15, "filter.keytrack": 0.35,
        "env1.attack": 0.35, "env1.decay": 1.5, "env1.sustain": 0.85, "env1.release": 1.4,
        "env2.attack": 0.6, "env2.decay": 1.8, "env2.sustain": 0.45,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.35, "lfo1.unipolar": 0,
        "lfo1.stereo": 0.6,
        "lfo2.shape": "Sine", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.18, "lfo2.unipolar": 0,
        "lfo2.stereo": 0.5,
        "lfo3.shape": "Smooth random", "lfo3.mode": "Free", "lfo3.sync": 0, "lfo3.rate": 0.5, "lfo3.unipolar": 0,
        "voice.voices": 10, "voice.master": 0.6,
        **mods(("env2", "filter.cutoff", 0.18), ("lfo1", "b.position", 0.25), ("lfo2", "filter.cutoff", 0.04),
               ("lfo3", "a.fine", 0.02), ("lfo3", "b.fine", 0.02), ("modWheel", "filter.cutoff", 0.25),
               ("velocity", "filter.cutoff", 0.06), ("aftertouch", "filter.cutoff", 0.1),
               ("macro1", "filter.resonance", 0.35)),
    },

    # ---- Motion and sequences -------------------------------------------------------------
    # Held chords chopped by a drawn sixteenth-note gate, the filter rising over four bars.
    "Trance Gate": {
        "a.position": SAW, "a.unison": 7, "a.detune": 0.35, "a.level": 0.55,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": 1, "b.unison": 5, "b.detune": 0.3,
        "b.level": 0.3,
        "c.on": 1, "c.table": "Harmonic Sweep", "c.position": 0.5, "c.octave": -1, "c.level": 0.25,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 1_800, "filter.resonance": 0.2,
        "filter.keytrack": 0.3,
        "env1.attack": 0.005, "env1.sustain": 1.0, "env1.release": 0.25,
        "lfo1.shape": "Drawn", "lfo1.sync": 1, "lfo1.division": "1/2", "lfo1.unipolar": 1,
        **drawn(1, gate([1, 0, 1, 1, 0, 1, 1, 0])),
        "lfo2.shape": "Triangle", "lfo2.sync": 1, "lfo2.division": "4/1", "lfo2.unipolar": 1,
        "voice.voices": 10, "voice.master": 0.8,
        **mods(("lfo1", "voice.master", -0.8), ("lfo2", "filter.cutoff", 0.25), ("lfo1", "filter.cutoff", -0.05),
               ("modWheel", "filter.cutoff", 0.2), ("macro1", "filter.resonance", 0.35), ("velocity", "filter.cutoff", 0.05),
               ("aftertouch", "a.detune", 0.2)),
    },
    # A held note made into a sequence: a pluck on every sixteenth, the cutoff and the
    # wavetable picked at random each step, the octave above and the pan each eighth.
    "Random Sixteenths": {
        "a.table": "Digital Steps", "a.position": 0.4, "a.unison": 2, "a.detune": 0.1, "a.level": 0.65,
        "b.on": 1, "b.table": "Sync Sweep", "b.position": 0.3, "b.octave": 1, "b.level": 0.3,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 900, "filter.resonance": 0.45,
        "filter.keytrack": 0.4,
        "env1.attack": 0.002, "env1.sustain": 1.0, "env1.release": 0.3,
        "lfo1.shape": "Sample & hold", "lfo1.sync": 1, "lfo1.division": "1/16", "lfo1.unipolar": 0, "lfo1.smooth": 0.1,
        "lfo2.shape": "Sample & hold", "lfo2.sync": 1, "lfo2.division": "1/8", "lfo2.unipolar": 0,
        "lfo3.shape": "Exponential", "lfo3.sync": 1, "lfo3.division": "1/16", "lfo3.unipolar": 1,
        "voice.master": 0.35,
        **mods(("lfo1", "filter.cutoff", 0.18), ("lfo1", "a.position", 0.3), ("lfo2", "b.position", 0.3),
               ("lfo2", "a.pan", 0.25), ("lfo3", "voice.master", 0.4), ("lfo3", "filter.cutoff", 0.15),
               ("modWheel", "filter.cutoff", 0.25), ("macro1", "filter.resonance", 0.3), ("velocity", "filter.cutoff", 0.06)),
    },
    # Three clocks against each other: triplet eighths on the cutoff, dotted sixteenths
    # (stereo) on cutoff and resonance, dotted quarters bringing in a bright octave.
    "Polyrhythmic Filter": {
        "a.position": SAW, "a.unison": 3, "a.detune": 0.18, "a.level": 0.6,
        "b.on": 1, "b.table": "Square Build", "b.position": 0.5, "b.octave": -1, "b.level": 0.4,
        "c.on": 1, "c.table": "Harmonic Sweep", "c.position": 0.7, "c.octave": 1, "c.level": 0.0,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 600, "filter.resonance": 0.5,
        "filter.keytrack": 0.4,
        "env1.attack": 0.003, "env1.sustain": 1.0, "env1.release": 0.2,
        "lfo1.shape": "Saw down", "lfo1.sync": 1, "lfo1.division": "1/8T", "lfo1.unipolar": 1,
        "lfo2.shape": "Triangle", "lfo2.sync": 1, "lfo2.division": "1/16.", "lfo2.unipolar": 1, "lfo2.stereo": 0.5,
        "lfo3.shape": "Square", "lfo3.sync": 1, "lfo3.division": "1/4.", "lfo3.unipolar": 1,
        "voice.master": 0.55,
        **mods(("lfo1", "filter.cutoff", 0.3), ("lfo2", "filter.cutoff", 0.12), ("lfo2", "filter.resonance", 0.1),
               ("lfo3", "c.level", 0.5), ("lfo3", "a.position", 0.15), ("modWheel", "filter.cutoff", 0.2),
               ("macro1", "filter.resonance", 0.35), ("velocity", "filter.cutoff", 0.06)),
    },

    # ---- Risers, falls and effects --------------------------------------------------------
    # Hold it: over eight seconds the pitch climbs an octave, the filter opens, the sync
    # sweep and the noise rise, the tremolo speeds up to a buzz.
    "Tension Riser": {
        "a.position": SAW, "a.unison": 9, "a.detune": 0.3, "a.width": 1.0, "a.level": 0.5,
        "b.on": 1, "b.table": "Sync Sweep", "b.position": 0.0, "b.unison": 3, "b.detune": 0.2, "b.level": 0.3,
        "noise.on": 1, "noise.type": "White", "noise.tone": 0.6, "noise.level": 0.15,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 300, "filter.resonance": 0.35,
        "env1.attack": 0.5, "env1.sustain": 1.0, "env1.release": 1.5,
        "env3.attack": 8.0, "env3.attackCurve": -0.3, "env3.decay": 1.0, "env3.sustain": 1.0,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 2.0, "lfo1.unipolar": 1,
        "lfo2.shape": "Smooth random", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.8, "lfo2.unipolar": 0,
        "voice.master": 0.55,
        **mods(("env3", "filter.cutoff", 0.55), ("env3", "a.coarse", 12 / 72), ("env3", "b.coarse", 12 / 72),
               ("env3", "b.position", 1.0), ("env3", "a.detune", 0.4), ("env3", "noise.level", 0.5),
               ("env3", "lfo1.rate", 0.3), ("lfo1", "voice.master", -0.3), ("lfo2", "noise.pan", 0.35),
               ("modWheel", "filter.cutoff", 0.3)),
    },
    # A one-shot fall of three octaves and a half over three and a half seconds, the
    # filter closing with it, a wobble that slows down, a slow stereo sway.
    "Dive Downlifter": {
        "a.position": SAW, "a.unison": 7, "a.detune": 0.4, "a.width": 1.0, "a.level": 0.5, "a.coarse": -12,
        "b.on": 1, "b.table": "Growl", "b.position": 0.6, "b.level": 0.3, "b.coarse": -12,
        "noise.on": 1, "noise.type": "White", "noise.tone": 0.55, "noise.level": 0.05,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 400, "filter.resonance": 0.25,
        "env1.attack": 0.01, "env1.decay": 4.0, "env1.sustain": 0.0, "env1.release": 1.0, "env1.decayCurve": 0.2,
        "env3.attack": 0.0, "env3.decay": 3.5, "env3.sustain": 0.0, "env3.decayCurve": 0.0,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 6.0, "lfo1.unipolar": 0,
        "lfo2.shape": "Sine", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.5, "lfo2.unipolar": 0, "lfo2.stereo": 1.0,
        **mods(("env3", "a.coarse", 0.5), ("env3", "b.coarse", 0.5), ("env3", "filter.cutoff", 0.5),
               ("env3", "noise.level", 0.3), ("env3", "lfo1.rate", 0.25), ("lfo1", "a.fine", 0.1),
               ("lfo2", "voice.master", 0.1), ("modWheel", "filter.resonance", 0.4)),
    },
    # A sine boom dropping two octaves, a burst of brown rumble and spectral noise, a
    # driven filter closing, the sub kept clean; a slow stereo movement in the tail.
    "Sub Drop Impact": {
        "a.position": 0.0, "a.random": 0.0, "a.octave": -1, "a.level": 0.9,
        "b.on": 1, "b.table": "Spectral Noise", "b.position": 0.5, "b.level": 0.0,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.4, "sub.direct": 1,
        "noise.on": 1, "noise.type": "Brown", "noise.tone": 0.35, "noise.level": 0.0,
        "filter.on": 1, "filter.type": "Low pass 24", "filter.cutoff": 200, "filter.resonance": 0.15,
        "filter.drive": 0.5,
        "env1.attack": 0.001, "env1.decay": 3.5, "env1.sustain": 0.0, "env1.release": 2.0, "env1.decayCurve": 0.4,
        "env2.attack": 0.0, "env2.decay": 0.25, "env2.sustain": 0.0,
        "env3.attack": 0.0, "env3.decay": 0.4, "env3.sustain": 0.0,
        "lfo1.shape": "Sine", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.6, "lfo1.unipolar": 0,
        "lfo1.stereo": 1.0,
        "voice.mode": "Mono", "voice.velocity": 0.5,
        **mods(("env2", "a.coarse", 24 / 72), ("env3", "b.level", 0.5), ("env3", "noise.level", 0.6),
               ("env3", "filter.cutoff", 0.35), ("velocity", "filter.cutoff", 0.1), ("velocity", "noise.level", 0.1),
               ("lfo1", "voice.master", 0.08), ("macro1", "filter.drive", 0.5)),
    },
    # A square and a sync sweep falling three octaves in a blink through a resonant filter,
    # with a fast trill; each shot a little different.
    "Laser Zap": {
        "a.position": 1.0, "a.random": 0.0, "a.level": 0.6,
        "b.on": 1, "b.table": "Sync Sweep", "b.position": 0.5, "b.level": 0.3, "b.warp": "Sync", "b.warpAmount": 0.0,
        "filter.on": 1, "filter.type": "Low pass 12", "filter.cutoff": 900, "filter.resonance": 0.35,
        "filter.keytrack": 0.5,
        "env1.attack": 0.001, "env1.decay": 0.6, "env1.sustain": 0.0, "env1.release": 0.1, "env1.decayCurve": 0.3,
        "voice.master": 0.4,
        "env2.attack": 0.0, "env2.decay": 0.18, "env2.sustain": 0.0, "env2.decayCurve": 0.8,
        "lfo1.shape": "Sine", "lfo1.sync": 0, "lfo1.rate": 18.0, "lfo1.unipolar": 0,
        **mods(("env2", "a.coarse", 0.5), ("env2", "b.coarse", 0.5), ("env2", "b.warpAmount", 0.6),
               ("env2", "filter.cutoff", 0.35), ("lfo1", "a.coarse", 0.02), ("random", "a.coarse", 0.05),
               ("velocity", "filter.resonance", 0.2), ("modWheel", "lfo1.rate", 0.2)),
    },

    # ---- Ambient textures -----------------------------------------------------------------
    # Noise through a resonant band pass that follows the keys (a playable whistle),
    # gusts of level and pitch differing left and right, a faint tonal noise table.
    "Swirling Wind": {
        "a.on": 0,
        "b.on": 1, "b.table": "Spectral Noise", "b.position": 0.5, "b.unison": 5, "b.detune": 0.6, "b.width": 1.0,
        "b.level": 0.4,
        "noise.on": 1, "noise.type": "White", "noise.tone": 0.4, "noise.level": 1.0,
        "filter.on": 1, "filter.type": "Band pass 12", "filter.cutoff": 900, "filter.resonance": 0.2,
        "filter.keytrack": 0.8, "filter.mix": 0.85,
        "env1.attack": 1.5, "env1.release": 3.0,
        "lfo1.shape": "Smooth random", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.3, "lfo1.unipolar": 0,
        "lfo1.stereo": 1.0,
        "lfo2.shape": "Smooth random", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.15, "lfo2.unipolar": 1,
        "lfo2.stereo": 0.7,
        "lfo3.shape": "Sine", "lfo3.mode": "Free", "lfo3.sync": 0, "lfo3.rate": 0.05, "lfo3.unipolar": 0,
        "voice.master": 1.0,
        **mods(("lfo1", "filter.cutoff", 0.15), ("lfo2", "voice.master", 0.25), ("lfo3", "filter.resonance", 0.15),
               ("lfo3", "noise.tone", 0.1), ("modWheel", "filter.resonance", 0.4), ("aftertouch", "lfo1.rate", 0.2),
               ("velocity", "b.level", 0.15)),
    },
    # Crackles and high spectral noise rung through a comb tuned to the note, random
    # harmonics flickering every sixteenth and wandering across the field.
    "Glittering Dust": {
        "a.table": "Spectral Noise", "a.position": 0.5, "a.octave": 2, "a.unison": 3, "a.detune": 0.5, "a.level": 0.25,
        "a.spectral": "High pass", "a.spectralAmount": 0.4,
        "c.on": 1, "c.table": "Overtones", "c.position": 0.3, "c.octave": 1, "c.level": 0.3,
        "noise.on": 1, "noise.type": "Crackle", "noise.pitch": 0.6, "noise.tone": 0.7, "noise.level": 0.4,
        "filter.on": 1, "filter.type": "Comb +", "filter.cutoff": 261.6, "filter.resonance": 0.6,
        "filter.keytrack": 1.0, "filter.mix": 0.7,
        "env1.attack": 0.8, "env1.decay": 2.0, "env1.sustain": 0.8, "env1.release": 3.0,
        "lfo1.shape": "Sample & hold", "lfo1.sync": 1, "lfo1.division": "1/16", "lfo1.unipolar": 1, "lfo1.smooth": 0.05,
        "lfo2.shape": "Smooth random", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 2.0, "lfo2.unipolar": 0,
        "lfo3.shape": "Sine", "lfo3.mode": "Free", "lfo3.sync": 0, "lfo3.rate": 0.1, "lfo3.unipolar": 1,
        **mods(("lfo1", "c.position", 0.5), ("lfo2", "c.pan", 0.45), ("lfo2", "a.pan", -0.3), ("lfo3", "a.position", 0.4),
               ("modWheel", "noise.level", 0.4), ("aftertouch", "filter.resonance", 0.3), ("velocity", "c.level", 0.2)),
    },
    # The harmonic series climbing over half a minute, a saw with a drifting comb, a
    # fifth and a sub, a resonant peak sweeping through them in stereo.
    "Harmonic Drone": {
        "a.table": "Overtones", "a.position": 0.0, "a.octave": -1, "a.unison": 3, "a.detune": 0.1, "a.level": 0.45,
        "b.on": 1, "b.table": "Basic Shapes", "b.position": SAW, "b.octave": -1, "b.unison": 5, "b.detune": 0.2,
        "b.level": 0.35, "b.spectral": "Comb", "b.spectralAmount": 0.3,
        "c.on": 1, "c.table": "Basic Shapes", "c.position": 0.0, "c.octave": -1, "c.semi": 7, "c.level": 0.25,
        "sub.on": 1, "sub.octave": -2, "sub.level": 0.3,
        "filter.on": 1, "filter.type": "Peak", "filter.cutoff": 800, "filter.resonance": 0.45, "filter.variation": 0.7,
        "filter.keytrack": 0.5,
        "env1.attack": 2.5, "env1.decay": 1.0, "env1.sustain": 1.0, "env1.release": 5.0,
        "lfo1.shape": "Triangle", "lfo1.mode": "Free", "lfo1.sync": 0, "lfo1.rate": 0.03, "lfo1.unipolar": 1,
        "lfo2.shape": "Sine", "lfo2.mode": "Free", "lfo2.sync": 0, "lfo2.rate": 0.07, "lfo2.unipolar": 0,
        "lfo2.stereo": 0.5,
        "lfo3.shape": "Smooth random", "lfo3.mode": "Free", "lfo3.sync": 0, "lfo3.rate": 0.2, "lfo3.unipolar": 0,
        "voice.voices": 8, "voice.master": 0.6,
        **mods(("lfo1", "a.position", 0.4), ("lfo2", "filter.cutoff", 0.2), ("lfo3", "b.spectralAmount", 0.3),
               ("lfo3", "c.fine", 0.02), ("modWheel", "filter.variation", 0.1), ("aftertouch", "a.level", 0.3),
               ("velocity", "filter.cutoff", 0.05), ("macro1", "b.spectralAmount", 0.5)),
    },
}

FORBIDDEN = set('/\\:,*?"<>|')


def check_names() -> None:
    seen = set()
    for name in SOUNDS:
        if FORBIDDEN & set(name) or name != name.strip() or not name:
            raise SystemExit(f"bad name: {name!r}")
        if name.casefold() in seen:
            raise SystemExit(f"two sounds named {name!r}")
        seen.add(name.casefold())


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
    check_names()
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-prism-presets-"))
    presets = scratch / "Presets"
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {"presets": str(presets)}}),
                         encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([args.exe, f"--control-port={port}", "--silent", "--window-size=1400x900"], env=environment,
                               stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    failures = 0
    # Every setting of each sound as the card has it: a device preset only
    # gathers the settings the card lists (not oscillator C, the spectral
    # morphs, the LFOs' stereo and drawn points); the files get them all.
    whole: dict[str, dict] = {}
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
            whole[name] = answer.get("values", {})
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
            knobs = document.get("state", {}).get("parameters")
            if isinstance(knobs, dict):
                for key, value in whole.get(document.get("name", ""), {}).items():
                    knobs.setdefault("prism." + key, value)
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
