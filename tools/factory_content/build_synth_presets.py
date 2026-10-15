"""The synth family's factory sounds (Ambre and the models after it): each set
through CyDAW 2's control port on its card, measured offline (no sound out:
the synth command renders it in memory), its volume brought so that a held C4
sits near the same loudness, saved as a device preset and copied into
content/factory/Presets/device/<its type>.

    python tools/factory_content/build_synth_presets.py [--exe "<CyDAW 2.exe>"] [--port 47838] [--only ambre]

A sound is a few settings over the defaults ("Init" first): keys as in the
model's table ("osc1.wave", "filter.cutoff"); a choice by its English name
("Pulse", "Ladder"). Its name is English and becomes the preset's file name
(none of / \\ : , * ? " < > |). The folder is emptied before the new presets
are copied into it.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path
import importlib

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
PRESETS = ROOT / "content" / "factory" / "Presets" / "device"
port = 47838

# Where a held C4 lands (its loudness while held), the most its peak may reach, and the most a chord of four
# (C3 G3 C4 E4) may reach.
RMS_DB = -18.0
PEAK_CAP_DB = -3.0
CHORD_PEAK_CAP_DB = 1.0

def _presets(model: str) -> dict:
    """A later model's sounds, from synth_presets/<model>.py."""
    return importlib.import_module(f"synth_presets.{model}").PRESETS


AMBRE = {
    "Init": {},
    "Classic Ladder Bass": {
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Pulse", "osc2.octave": -1,
        "osc2.fine": 4, "osc2.level": 0.5, "osc3.wave": "Saw", "osc3.octave": -2, "osc3.level": 0.45,
        "filter.model": "Ladder", "filter.cutoff": 260, "filter.resonance": 0.25, "filter.env": 0.55, "filter.drive": 0.35,
        "fenv.attack": 0.001, "fenv.decay": 0.32, "fenv.sustain": 0.15, "amp.attack": 0.002, "amp.decay": 0.6,
        "amp.sustain": 0.85, "amp.release": 0.12, "drift": 0.25,
    },
    "Acid Bass": {
        "voice.mode": "Legato", "voice.glide": 0.07, "osc1.wave": "Saw", "osc1.octave": -1, "osc2.level": 0.0,
        "filter.model": "Ladder", "filter.cutoff": 320, "filter.resonance": 0.82, "filter.env": 0.7, "filter.drive": 0.55,
        "filter.key": 0.3, "fenv.attack": 0.001, "fenv.decay": 0.18, "fenv.sustain": 0.0, "fenv.release": 0.1,
        "amp.attack": 0.001, "amp.decay": 0.4, "amp.sustain": 0.9, "amp.release": 0.06, "drift": 0.1,
    },
    "Round Bass": {
        "voice.mode": "Mono", "osc1.wave": "Triangle", "osc1.octave": -1, "osc2.wave": "Sine", "osc2.octave": -2,
        "osc2.fine": 0, "osc2.level": 0.7, "filter.model": "Cascade", "filter.cutoff": 600, "filter.resonance": 0.1,
        "filter.env": 0.2, "amp.attack": 0.003, "amp.decay": 1.0, "amp.sustain": 0.8, "amp.release": 0.15, "drift": 0.15,
    },
    "Pulse Bass": {
        "voice.mode": "Mono", "osc1.wave": "Pulse", "osc1.octave": -1, "osc1.pw": 0.3, "osc2.wave": "Pulse",
        "osc2.octave": -1, "osc2.pw": 0.6, "osc2.fine": -6, "lfo1.pwm": 0.35, "lfo1.rate": 0.6, "lfo1.delay": 0.0,
        "filter.model": "Cascade", "filter.cutoff": 450, "filter.resonance": 0.35, "filter.env": 0.45,
        "fenv.decay": 0.25, "fenv.sustain": 0.2, "amp.release": 0.1,
    },
    "Sine Sub": {
        "voice.mode": "Mono", "osc1.wave": "Sine", "osc1.octave": -1, "osc2.level": 0.0, "filter.cutoff": 2_000,
        "filter.resonance": 0.0, "filter.env": 0.0, "filter.drive": 0.1, "amp.attack": 0.004, "amp.sustain": 1.0,
        "amp.release": 0.1, "drift": 0.0,
    },
    "Mono Lead": {
        "voice.mode": "Legato", "voice.glide": 0.06, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 9,
        "osc3.wave": "Pulse", "osc3.octave": -1, "osc3.level": 0.35, "filter.model": "Ladder", "filter.cutoff": 2_400,
        "filter.resonance": 0.3, "filter.env": 0.35, "filter.drive": 0.4, "fenv.decay": 0.5, "fenv.sustain": 0.4,
        "amp.sustain": 0.9, "amp.release": 0.2, "lfo1.rate": 5.5, "lfo1.delay": 0.4, "mod.wheel": 0.6,
    },
    "Sync Lead": {
        "voice.mode": "Legato", "voice.glide": 0.04, "osc1.wave": "Saw", "osc1.level": 0.4, "osc2.wave": "Saw",
        "osc2.sync": 1, "osc2.semi": 7, "osc2.fine": 0, "osc2.level": 0.9, "filter.model": "Multimode",
        "filter.cutoff": 3_500, "filter.resonance": 0.2, "filter.env": 0.3, "fenv.decay": 0.8, "fenv.sustain": 0.5,
        "lfo1.pitch": 0.0, "amp.release": 0.18,
    },
    "Biting Lead": {
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.fine": -8, "filter.model": "Biting",
        "filter.mode": "Low-pass", "filter.cutoff": 1_500, "filter.resonance": 0.75, "filter.drive": 0.8,
        "filter.env": 0.4, "fenv.decay": 0.35, "fenv.sustain": 0.35, "amp.release": 0.15,
    },
    "Warm Pad": {
        "voice.count": 12, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 11, "unison.voices": 2,
        "unison.detune": 14, "unison.spread": 0.8, "filter.model": "Cascade", "filter.cutoff": 1_100,
        "filter.resonance": 0.15, "filter.env": 0.2, "fenv.attack": 0.9, "fenv.decay": 2.0, "fenv.sustain": 0.5,
        "fenv.release": 1.6, "amp.attack": 0.7, "amp.decay": 1.5, "amp.sustain": 0.85, "amp.release": 1.6,
        "lfo2.rate": 0.25, "lfo2.cutoff": 0.15, "drift": 0.45,
    },
    "String Pad": {
        "voice.count": 12, "osc1.wave": "Pulse", "osc1.pw": 0.45, "osc2.wave": "Pulse", "osc2.pw": 0.55,
        "osc2.fine": 8, "lfo1.pwm": 0.6, "lfo1.rate": 0.8, "lfo1.delay": 0.0, "filter.model": "Multimode",
        "filter.cutoff": 2_600, "filter.resonance": 0.1, "filter.env": 0.15, "fenv.attack": 0.4, "fenv.sustain": 0.6,
        "amp.attack": 0.45, "amp.sustain": 0.9, "amp.release": 1.2, "hpf": 120, "drift": 0.35,
    },
    "Swept Pad": {
        "voice.count": 12, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.octave": 1, "osc2.fine": 6,
        "osc2.level": 0.4, "filter.model": "Ladder", "filter.cutoff": 500, "filter.resonance": 0.55, "filter.env": 0.0,
        "lfo2.shape": "Triangle", "lfo2.rate": 0.12, "lfo2.cutoff": 0.6, "amp.attack": 1.0, "amp.sustain": 0.9,
        "amp.release": 2.0, "unison.voices": 2, "unison.detune": 10,
    },
    "Brass": {
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 6, "filter.model": "Ladder",
        "filter.cutoff": 700, "filter.resonance": 0.12, "filter.env": 0.6, "filter.velocity": 0.6,
        "fenv.attack": 0.07, "fenv.decay": 0.5, "fenv.sustain": 0.45, "fenv.release": 0.3, "amp.attack": 0.03,
        "amp.decay": 0.6, "amp.sustain": 0.85, "amp.release": 0.25, "lfo1.rate": 5.0, "lfo1.delay": 0.5,
        "lfo1.pitch": 0.08,
    },
    "Pluck": {
        "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.fine": 5, "osc2.level": 0.5, "filter.model": "Ladder",
        "filter.cutoff": 450, "filter.resonance": 0.35, "filter.env": 0.75, "fenv.attack": 0.001, "fenv.decay": 0.22,
        "fenv.sustain": 0.0, "amp.attack": 0.001, "amp.decay": 0.45, "amp.sustain": 0.0, "amp.release": 0.3,
    },
    "Resonant Arp": {
        "osc1.wave": "Saw", "osc2.level": 0.0, "filter.model": "Multimode", "filter.cutoff": 600,
        "filter.resonance": 0.7, "filter.env": 0.6, "fenv.decay": 0.15, "fenv.sustain": 0.0, "amp.decay": 0.3,
        "amp.sustain": 0.0, "amp.release": 0.2, "lfo2.rate": 0.2, "lfo2.cutoff": 0.2,
    },
    "Poly Keys": {
        "voice.count": 12, "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.pw": 0.35, "osc2.fine": 4,
        "osc2.level": 0.5, "filter.model": "Cascade", "filter.cutoff": 1_600, "filter.env": 0.35,
        "fenv.decay": 0.8, "fenv.sustain": 0.25, "amp.decay": 1.4, "amp.sustain": 0.35, "amp.release": 0.5,
    },
    "Analog Organ": {
        "osc1.wave": "Pulse", "osc1.pw": 0.5, "osc1.level": 0.6, "osc2.wave": "Sine", "osc2.octave": 1,
        "osc2.fine": 0, "osc2.level": 0.5, "osc3.wave": "Sine", "osc3.octave": -1, "osc3.fine": 0, "osc3.level": 0.4,
        "filter.model": "Multimode", "filter.cutoff": 4_000, "filter.resonance": 0.0, "filter.env": 0.0,
        "amp.attack": 0.005, "amp.sustain": 1.0, "amp.release": 0.08, "lfo1.rate": 6.5, "lfo1.delay": 0.0,
        "lfo1.pitch": 0.05, "drift": 0.2,
    },
    "FM Bell": {
        "osc1.wave": "Sine", "osc2.level": 0.0, "osc3.wave": "Sine", "osc3.octave": 1, "osc3.semi": 7,
        "osc3.fine": 0, "osc3.level": 0.0, "fm.amount": 0.55, "filter.model": "Multimode", "filter.cutoff": 9_000,
        "filter.resonance": 0.0, "filter.env": 0.0, "amp.attack": 0.001, "amp.decay": 2.5, "amp.sustain": 0.0,
        "amp.release": 2.0, "drift": 0.0,
    },
    "Wind": {
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.level": 1.0, "filter.model": "Multimode", "filter.mode": "Band-pass",
        "filter.cutoff": 900, "filter.resonance": 0.6, "filter.env": 0.0, "filter.key": 0.8, "lfo2.rate": 0.15,
        "lfo2.cutoff": 0.5, "amp.attack": 1.2, "amp.sustain": 1.0, "amp.release": 2.0,
    },
    "Wide Unison": {
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 13, "unison.voices": 4,
        "unison.detune": 26, "unison.spread": 1.0, "filter.model": "Multimode", "filter.cutoff": 5_000,
        "filter.resonance": 0.1, "filter.env": 0.15, "amp.attack": 0.01, "amp.sustain": 0.9, "amp.release": 0.5,
        "hpf": 90,
    },
    "Wobble Bass": {
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Pulse", "osc2.octave": -1,
        "osc2.fine": -10, "filter.model": "Ladder", "filter.cutoff": 300, "filter.resonance": 0.6, "filter.drive": 0.6,
        "filter.env": 0.0, "lfo2.shape": "Sine", "lfo2.sync": 6, "lfo2.cutoff": 0.75, "amp.sustain": 1.0,
        "amp.release": 0.1,
    },
    "Vintage Drift": {
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 3, "drift": 1.0,
        "filter.model": "Cascade", "filter.cutoff": 1_900, "filter.resonance": 0.2, "filter.env": 0.25,
        "amp.attack": 0.02, "amp.release": 0.6, "lfo2.rate": 0.3, "lfo2.amp": 0.1,
    },
    # ---- Basses -------------------------------------------------------------------------------
    "Squelching Acid Bass": {
        # One saw (a square under it) into the biting filter: its resonance bites, the velocity is the accent.
        "voice.mode": "Legato", "voice.glide": 0.09, "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Pulse",
        "osc2.octave": -1, "osc2.pw": 0.5, "osc2.fine": 0, "osc2.level": 0.25, "drift": 0.05, "hpf": 35,
        "filter.model": "Biting", "filter.mode": "Low-pass", "filter.cutoff": 260, "filter.resonance": 0.86,
        "filter.drive": 0.7, "filter.env": 0.72, "filter.key": 0.45, "filter.velocity": 0.85, "fenv.attack": 0.001,
        "fenv.decay": 0.14, "fenv.sustain": 0.0, "fenv.release": 0.08, "amp.attack": 0.001, "amp.decay": 0.5,
        "amp.sustain": 0.85, "amp.release": 0.05, "amp.velocity": 0.35, "lfo2.shape": "Triangle", "lfo2.sync": 12,
        "lfo2.cutoff": 0.06, "mod.wheel": 0.0,
    },
    "Warm Saturated Sub": {
        # A sine and a triangle an octave down, warmed by the cascade's drive: a sub that speaks on small speakers.
        "voice.mode": "Mono", "voice.glide": 0.03, "osc1.wave": "Sine", "osc1.octave": -1, "osc1.level": 0.9,
        "osc2.wave": "Triangle", "osc2.octave": -1, "osc2.fine": 0, "osc2.level": 0.3, "drift": 0.04,
        "filter.model": "Cascade", "filter.cutoff": 700, "filter.resonance": 0.0, "filter.drive": 0.45, "filter.env": 0.15,
        "filter.key": 0.6, "filter.velocity": 0.5, "fenv.decay": 0.12, "fenv.sustain": 0.0, "amp.attack": 0.003,
        "amp.decay": 0.8, "amp.sustain": 1.0, "amp.release": 0.09, "amp.velocity": 0.25, "mod.wheel": 0.0,
    },
    "Plucked Funk Bass": {
        # Saw, narrow pulse and a sine two octaves down; a ladder snapped open by the velocity.
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Pulse", "osc2.octave": -1, "osc2.pw": 0.3,
        "osc2.fine": -4, "osc2.level": 0.55, "osc3.wave": "Sine", "osc3.octave": -2, "osc3.fine": 0, "osc3.level": 0.35,
        "drift": 0.12, "filter.model": "Ladder", "filter.cutoff": 170, "filter.resonance": 0.45, "filter.drive": 0.4,
        "filter.env": 0.78, "filter.key": 0.5, "filter.velocity": 0.75, "fenv.attack": 0.001, "fenv.decay": 0.16,
        "fenv.sustain": 0.0, "fenv.release": 0.1, "amp.attack": 0.001, "amp.decay": 0.35, "amp.sustain": 0.45,
        "amp.release": 0.08, "amp.velocity": 0.6, "mod.wheel": 0.0,
    },
    "Night Drive Synthwave Bass": {
        # Two saws apart, a square sub, two unison voices barely open: the eighth-note bass of night drives.
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Saw", "osc2.octave": -1, "osc2.fine": 8,
        "osc2.level": 0.7, "osc3.wave": "Pulse", "osc3.octave": -2, "osc3.fine": 0, "osc3.level": 0.4, "unison.voices": 2,
        "unison.detune": 6, "unison.spread": 0.15, "drift": 0.3, "filter.model": "Ladder", "filter.cutoff": 330,
        "filter.resonance": 0.28, "filter.drive": 0.3, "filter.env": 0.55, "filter.key": 0.5, "filter.velocity": 0.5,
        "fenv.decay": 0.22, "fenv.sustain": 0.12, "fenv.release": 0.15, "amp.attack": 0.002, "amp.decay": 0.3,
        "amp.sustain": 0.65, "amp.release": 0.1, "mod.wheel": 0.0,
    },
    "Growling FM Bass": {
        # A sine frequency-modulated by its twin (1:1), a narrow pulse, the biting filter wobbling in eighths.
        "voice.mode": "Mono", "osc1.wave": "Sine", "osc1.octave": -1, "osc1.level": 0.85, "osc2.wave": "Pulse",
        "osc2.octave": -1, "osc2.pw": 0.25, "osc2.fine": -7, "osc2.level": 0.35, "osc3.wave": "Sine", "osc3.octave": -1,
        "osc3.semi": 0, "osc3.fine": 0, "osc3.level": 0.15, "fm.amount": 0.42, "drift": 0.1, "filter.model": "Biting",
        "filter.mode": "Low-pass", "filter.cutoff": 420, "filter.resonance": 0.55, "filter.drive": 0.85, "filter.env": 0.3,
        "filter.key": 0.4, "fenv.decay": 0.3, "fenv.sustain": 0.2, "lfo2.shape": "Sine", "lfo2.sync": 6, "lfo2.cutoff": 0.45,
        "amp.attack": 0.002, "amp.sustain": 1.0, "amp.release": 0.1, "mod.wheel": 0.0,
    },
    "Dark Rolling Reese": {
        # Two saws a fifth of a semitone apart, doubled in unison, a sub; the cascade slowly breathing.
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Saw", "osc2.octave": -1, "osc2.fine": 19,
        "osc2.level": 0.8, "osc3.wave": "Sine", "osc3.octave": -2, "osc3.fine": 0, "osc3.level": 0.4, "unison.voices": 2,
        "unison.detune": 9, "unison.spread": 0.2, "drift": 0.3, "hpf": 30, "filter.model": "Cascade", "filter.cutoff": 650,
        "filter.resonance": 0.22, "filter.drive": 0.55, "filter.env": 0.08, "filter.key": 0.3, "lfo2.shape": "Sine",
        "lfo2.rate": 0.17, "lfo2.cutoff": 0.18, "amp.attack": 0.004, "amp.sustain": 1.0, "amp.release": 0.15, "mod.wheel": 0.0,
    },
    "Triplet Wobble Bass": {
        # Saw, narrow pulse and sub into the biting filter, wobbled in eighth triplets; the pulse width ticks in sixteenths.
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Pulse", "osc2.octave": -1, "osc2.pw": 0.2,
        "osc2.fine": 9, "osc2.level": 0.7, "osc3.wave": "Sine", "osc3.octave": -2, "osc3.fine": 0, "osc3.level": 0.5,
        "drift": 0.1, "filter.model": "Biting", "filter.mode": "Low-pass", "filter.cutoff": 240, "filter.resonance": 0.7,
        "filter.drive": 0.9, "filter.env": 0.0, "filter.key": 0.3, "lfo2.shape": "Sine", "lfo2.sync": 4, "lfo2.cutoff": 0.7,
        "lfo1.shape": "Square", "lfo1.sync": 2, "lfo1.delay": 0.0, "lfo1.pwm": 0.3, "amp.sustain": 1.0, "amp.release": 0.1,
        "mod.wheel": 0.0,
    },
    "Opening Wah Bass": {
        # An inverted filter envelope: the cascade starts closed and opens as it decays.
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Saw", "osc2.octave": -1, "osc2.fine": -6,
        "osc2.level": 0.6, "osc3.wave": "Sine", "osc3.octave": -2, "osc3.fine": 0, "osc3.level": 0.35, "drift": 0.2,
        "filter.model": "Cascade", "filter.cutoff": 1_400, "filter.resonance": 0.5, "filter.drive": 0.4, "filter.env": -0.45,
        "filter.key": 0.3, "filter.velocity": 0.5, "fenv.attack": 0.001, "fenv.decay": 0.45, "fenv.sustain": 0.0,
        "fenv.release": 0.2, "amp.attack": 0.002, "amp.decay": 0.6, "amp.sustain": 0.9, "amp.release": 0.12, "mod.wheel": 0.0,
    },
    # ---- Leads --------------------------------------------------------------------------------
    "Singing Glide Lead": {
        # Saw, pulse and a saw an octave down; the glide always there, a vibrato that comes late, the wheel deepens it.
        "voice.mode": "Legato", "voice.glide": 0.12, "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.pw": 0.4, "osc2.fine": 5,
        "osc2.level": 0.55, "osc3.wave": "Saw", "osc3.octave": -1, "osc3.fine": -6, "osc3.level": 0.3, "drift": 0.25,
        "filter.model": "Ladder", "filter.cutoff": 1_700, "filter.resonance": 0.35, "filter.drive": 0.45, "filter.env": 0.38,
        "filter.key": 0.6, "filter.velocity": 0.6, "fenv.attack": 0.01, "fenv.decay": 0.6, "fenv.sustain": 0.45,
        "fenv.release": 0.3, "amp.attack": 0.005, "amp.decay": 0.8, "amp.sustain": 0.95, "amp.release": 0.25,
        "amp.velocity": 0.3, "lfo1.shape": "Sine", "lfo1.rate": 5.8, "lfo1.delay": 0.6, "lfo1.pitch": 0.12, "mod.wheel": 0.8,
    },
    "Nasal Square Lead": {
        # Narrow pulses whose width a triangle LFO moves, through a band-pass that follows the keys.
        "voice.mode": "Legato", "voice.glide": 0.05, "osc1.wave": "Pulse", "osc1.pw": 0.22, "osc2.wave": "Pulse",
        "osc2.pw": 0.3, "osc2.fine": -6, "osc2.level": 0.5, "osc3.wave": "Pulse", "osc3.octave": -1, "osc3.fine": 3,
        "osc3.level": 0.25, "lfo1.shape": "Triangle", "lfo1.rate": 3.2, "lfo1.delay": 0.0, "lfo1.pwm": 0.35,
        "filter.model": "Multimode", "filter.mode": "Band-pass", "filter.cutoff": 1_300, "filter.resonance": 0.45,
        "filter.drive": 0.35, "filter.env": 0.25, "filter.key": 0.7, "fenv.decay": 0.4, "fenv.sustain": 0.5,
        "amp.attack": 0.008, "amp.sustain": 0.9, "amp.release": 0.18, "mod.wheel": 0.6,
    },
    "Grinding Sync Lead": {
        # The second oscillator synced an octave and a third up, the first one's pitch shaken by FM: the sync grinds.
        "voice.mode": "Legato", "voice.glide": 0.04, "osc1.wave": "Saw", "osc1.level": 0.3, "osc2.wave": "Saw",
        "osc2.sync": 1, "osc2.octave": 1, "osc2.semi": 3, "osc2.fine": 0, "osc2.level": 0.9, "osc3.wave": "Sine",
        "osc3.octave": -1, "osc3.fine": 0, "osc3.level": 0.0, "fm.amount": 0.22, "filter.model": "Biting",
        "filter.mode": "Low-pass", "filter.cutoff": 2_800, "filter.resonance": 0.3, "filter.drive": 0.5, "filter.env": 0.25,
        "filter.key": 0.5, "fenv.decay": 0.7, "fenv.sustain": 0.5, "amp.attack": 0.003, "amp.sustain": 0.9,
        "amp.release": 0.2, "lfo1.rate": 5.5, "lfo1.delay": 0.5, "lfo1.pitch": 0.1, "mod.wheel": 0.7,
    },
    "Breathy Analog Flute": {
        # A triangle and its octave, breath noise, a soft attack, a vibrato and a tremolo that come in together.
        "voice.mode": "Legato", "voice.glide": 0.08, "osc1.wave": "Triangle", "osc1.level": 0.85, "osc2.wave": "Sine",
        "osc2.octave": 1, "osc2.fine": 0, "osc2.level": 0.22, "noise.level": 0.14, "drift": 0.2, "filter.model": "Multimode",
        "filter.mode": "Low-pass", "filter.cutoff": 2_200, "filter.resonance": 0.1, "filter.drive": 0.05, "filter.env": 0.18,
        "filter.key": 0.8, "filter.velocity": 0.6, "fenv.attack": 0.05, "fenv.decay": 0.4, "fenv.sustain": 0.5,
        "amp.attack": 0.06, "amp.decay": 0.5, "amp.sustain": 0.9, "amp.release": 0.3, "amp.velocity": 0.5,
        "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.delay": 0.35, "lfo1.pitch": 0.14, "lfo2.shape": "Sine",
        "lfo2.rate": 5.2, "lfo2.amp": 0.08, "mod.wheel": 0.4,
    },
    "Bright Synthwave Brass": {
        # Three saws in two unison voices, the ladder swept by a slow-attack envelope the velocity pushes.
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 9, "osc2.level": 0.75, "osc3.wave": "Saw",
        "osc3.octave": -1, "osc3.fine": -4, "osc3.level": 0.35, "unison.voices": 2, "unison.detune": 10,
        "unison.spread": 0.6, "drift": 0.35, "filter.model": "Ladder", "filter.cutoff": 480, "filter.resonance": 0.18,
        "filter.drive": 0.35, "filter.env": 0.62, "filter.key": 0.6, "filter.velocity": 0.75, "fenv.attack": 0.09,
        "fenv.decay": 0.7, "fenv.sustain": 0.4, "fenv.release": 0.4, "amp.attack": 0.04, "amp.decay": 0.8,
        "amp.sustain": 0.85, "amp.release": 0.35, "amp.velocity": 0.4, "lfo1.rate": 5.2, "lfo1.delay": 0.7,
        "lfo1.pitch": 0.06, "mod.wheel": 0.5,
    },
    # ---- Keys, bells, plucks ------------------------------------------------------------------
    "Tremolo Electric Piano": {
        # A sine bent by its twin (FM 1:1) for the bark, the filter envelope and the velocity shaping it, a tremolo.
        "voice.count": 12, "osc1.wave": "Sine", "osc1.level": 0.85, "osc2.wave": "Triangle", "osc2.fine": 3,
        "osc2.level": 0.25, "osc3.wave": "Sine", "osc3.octave": 0, "osc3.semi": 0, "osc3.fine": 0, "osc3.level": 0.0,
        "fm.amount": 0.25, "drift": 0.1, "filter.model": "Multimode", "filter.mode": "Low-pass", "filter.cutoff": 900,
        "filter.resonance": 0.05, "filter.drive": 0.25, "filter.env": 0.45, "filter.key": 0.7, "filter.velocity": 0.8,
        "fenv.attack": 0.001, "fenv.decay": 0.6, "fenv.sustain": 0.1, "fenv.release": 0.4, "amp.attack": 0.001,
        "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 0.5, "amp.velocity": 0.7, "lfo2.shape": "Sine",
        "lfo2.rate": 4.5, "lfo2.amp": 0.25, "mod.wheel": 0.2,
    },
    "Swirling Drawbar Organ": {
        # Three sines as drawbars (the note, its octave, its twelfth), a little drive, a fast vibrato and a swirl.
        "voice.count": 12, "osc1.wave": "Sine", "osc1.level": 0.7, "osc2.wave": "Sine", "osc2.octave": 1, "osc2.fine": 0,
        "osc2.level": 0.55, "osc3.wave": "Sine", "osc3.octave": 1, "osc3.semi": 7, "osc3.fine": 0, "osc3.level": 0.4,
        "fm.amount": 0.03, "drift": 0.05, "filter.model": "Cascade", "filter.cutoff": 3_500, "filter.resonance": 0.0,
        "filter.drive": 0.35, "filter.env": 0.0, "filter.key": 0.3, "amp.attack": 0.004, "amp.sustain": 1.0,
        "amp.release": 0.06, "amp.velocity": 0.0, "lfo1.shape": "Sine", "lfo1.rate": 6.8, "lfo1.delay": 0.0,
        "lfo1.pitch": 0.05, "lfo2.shape": "Sine", "lfo2.rate": 6.0, "lfo2.amp": 0.08, "mod.wheel": 0.3,
    },
    "Crystal Chimes": {
        # A high sine frequency-modulated at an inharmonic ratio (about 1.92), two unison voices wide apart.
        "voice.count": 12, "osc1.wave": "Sine", "osc1.octave": 1, "osc1.level": 0.8, "osc2.wave": "Triangle",
        "osc2.octave": 1, "osc2.fine": 4, "osc2.level": 0.15, "osc3.wave": "Sine", "osc3.octave": 1, "osc3.semi": 11,
        "osc3.fine": 31, "osc3.level": 0.0, "fm.amount": 0.48, "drift": 0.0, "unison.voices": 2, "unison.detune": 5,
        "unison.spread": 1.0, "hpf": 180, "filter.model": "Multimode", "filter.mode": "Low-pass", "filter.cutoff": 6_000,
        "filter.resonance": 0.0, "filter.drive": 0.0, "filter.env": 0.25, "filter.key": 0.5, "filter.velocity": 0.6,
        "fenv.decay": 1.2, "fenv.sustain": 0.0, "amp.attack": 0.001, "amp.decay": 3.2, "amp.sustain": 0.0,
        "amp.release": 2.5, "amp.velocity": 0.6, "mod.wheel": 0.0,
    },
    "Wobbly Lo-Fi Keys": {
        # Triangle and sine, a slow wow on the pitch and a random flutter on the volume, full drift, a little hiss.
        "voice.count": 10, "osc1.wave": "Triangle", "osc2.wave": "Sine", "osc2.octave": 1, "osc2.fine": 0, "osc2.level": 0.3,
        "osc3.wave": "Saw", "osc3.octave": 0, "osc3.fine": -8, "osc3.level": 0.12, "noise.level": 0.04, "drift": 1.0,
        "hpf": 120, "filter.model": "Cascade", "filter.cutoff": 1_600, "filter.resonance": 0.05, "filter.drive": 0.45,
        "filter.env": 0.2, "filter.key": 0.5, "filter.velocity": 0.5, "fenv.decay": 0.8, "fenv.sustain": 0.2,
        "amp.attack": 0.002, "amp.decay": 1.6, "amp.sustain": 0.4, "amp.release": 0.5, "amp.velocity": 0.6,
        "lfo1.shape": "Sine", "lfo1.rate": 0.7, "lfo1.delay": 0.0, "lfo1.pitch": 0.12, "lfo2.shape": "Random",
        "lfo2.rate": 9.0, "lfo2.amp": 0.06, "mod.wheel": 0.0,
    },
    "Shimmering Synthwave Pluck": {
        # Two saws and a narrow octave pulse, two unison voices; a quick ladder pluck that dies away.
        "voice.count": 12, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 12, "osc2.level": 0.7, "osc3.wave": "Pulse",
        "osc3.octave": 1, "osc3.fine": 0, "osc3.level": 0.22, "unison.voices": 2, "unison.detune": 14, "unison.spread": 0.9,
        "drift": 0.2, "filter.model": "Ladder", "filter.cutoff": 380, "filter.resonance": 0.3, "filter.drive": 0.25,
        "filter.env": 0.68, "filter.key": 0.6, "filter.velocity": 0.6, "fenv.attack": 0.001, "fenv.decay": 0.2,
        "fenv.sustain": 0.0, "fenv.release": 0.2, "amp.attack": 0.001, "amp.decay": 0.6, "amp.sustain": 0.0,
        "amp.release": 0.35, "amp.velocity": 0.5, "mod.wheel": 0.0,
    },
    "Bouncing Square Arp": {
        # A pulse and its octave, the multimode resonating, a sweep over two bars and a slow width motion.
        "voice.count": 8, "osc1.wave": "Pulse", "osc1.pw": 0.35, "osc2.wave": "Pulse", "osc2.octave": 1, "osc2.pw": 0.5,
        "osc2.fine": 0, "osc2.level": 0.35, "drift": 0.15, "filter.model": "Multimode", "filter.mode": "Low-pass",
        "filter.cutoff": 650, "filter.resonance": 0.55, "filter.drive": 0.3, "filter.env": 0.5, "filter.key": 0.6,
        "fenv.decay": 0.12, "fenv.sustain": 0.05, "amp.decay": 0.22, "amp.sustain": 0.15, "amp.release": 0.12,
        "lfo2.shape": "Triangle", "lfo2.sync": 11, "lfo2.cutoff": 0.22, "lfo1.delay": 0.0, "lfo1.rate": 0.3,
        "lfo1.pwm": 0.25, "mod.wheel": 0.0,
    },
    "Hypnotic Techno Sequence": {
        # A saw over a thin pulse, the ladder ringing on short envelopes; a bar-long sweep keeps it turning.
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.octave": -1, "osc2.pw": 0.15, "osc2.fine": 0,
        "osc2.level": 0.4, "drift": 0.08, "filter.model": "Ladder", "filter.cutoff": 210, "filter.resonance": 0.66,
        "filter.drive": 0.45, "filter.env": 0.5, "filter.key": 0.5, "filter.velocity": 0.6, "fenv.attack": 0.001,
        "fenv.decay": 0.09, "fenv.sustain": 0.0, "fenv.release": 0.05, "amp.attack": 0.001, "amp.decay": 0.18,
        "amp.sustain": 0.0, "amp.release": 0.05, "lfo2.shape": "Triangle", "lfo2.sync": 12, "lfo2.cutoff": 0.32,
        "mod.wheel": 0.0,
    },
    "Minor Dub Chord": {
        # The three oscillators a minor triad (root, minor third, fifth): one key, a whole dub chord, short and filtered.
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.semi": 3, "osc2.fine": 4, "osc2.level": 0.65,
        "osc3.wave": "Saw", "osc3.octave": 0, "osc3.semi": 7, "osc3.fine": -4, "osc3.level": 0.6, "drift": 0.25,
        "hpf": 150, "filter.model": "Cascade", "filter.cutoff": 550, "filter.resonance": 0.35, "filter.drive": 0.3,
        "filter.env": 0.5, "filter.key": 0.5, "filter.velocity": 0.7, "fenv.decay": 0.25, "fenv.sustain": 0.05,
        "fenv.release": 0.3, "amp.attack": 0.002, "amp.decay": 0.4, "amp.sustain": 0.0, "amp.release": 0.4,
        "amp.velocity": 0.5, "lfo2.shape": "Triangle", "lfo2.sync": 12, "lfo2.cutoff": 0.12, "mod.wheel": 0.0,
    },
    # ---- Pads, drones, effects ----------------------------------------------------------------
    "Silky PWM Pad": {
        # Two pulses whose width a slow triangle sweeps, a faint saw an octave up, the cascade opening slowly.
        "voice.count": 12, "osc1.wave": "Pulse", "osc1.pw": 0.5, "osc2.wave": "Pulse", "osc2.pw": 0.5, "osc2.fine": -9,
        "osc2.level": 0.7, "osc3.wave": "Saw", "osc3.octave": 1, "osc3.fine": 5, "osc3.level": 0.12,
        "lfo1.shape": "Triangle", "lfo1.rate": 0.45, "lfo1.delay": 0.0, "lfo1.pwm": 0.75, "unison.voices": 2,
        "unison.detune": 8, "unison.spread": 0.9, "drift": 0.5, "hpf": 80, "filter.model": "Cascade",
        "filter.cutoff": 1_400, "filter.resonance": 0.15, "filter.drive": 0.15, "filter.env": 0.15, "filter.key": 0.4,
        "fenv.attack": 1.2, "fenv.decay": 2.5, "fenv.sustain": 0.6, "fenv.release": 2.0, "amp.attack": 0.9,
        "amp.decay": 2.0, "amp.sustain": 0.9, "amp.release": 2.4, "lfo2.rate": 0.12, "lfo2.cutoff": 0.1, "mod.wheel": 0.3,
    },
    "Dark Cinematic Pad": {
        # Saws an octave apart and a deep triangle, three unison voices, the ladder opening over seconds.
        "voice.count": 10, "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Saw", "osc2.fine": 5, "osc2.level": 0.55,
        "osc3.wave": "Triangle", "osc3.octave": -2, "osc3.fine": 0, "osc3.level": 0.4, "unison.voices": 3,
        "unison.detune": 18, "unison.spread": 1.0, "noise.level": 0.06, "drift": 0.6, "filter.model": "Ladder",
        "filter.cutoff": 240, "filter.resonance": 0.35, "filter.drive": 0.4, "filter.env": 0.28, "filter.key": 0.3,
        "filter.velocity": 0.4, "fenv.attack": 3.0, "fenv.decay": 4.0, "fenv.sustain": 0.3, "fenv.release": 3.0,
        "amp.attack": 1.8, "amp.decay": 3.0, "amp.sustain": 0.95, "amp.release": 3.5, "lfo2.shape": "Sine",
        "lfo2.rate": 0.07, "lfo2.cutoff": 0.2, "mod.wheel": 0.2,
    },
    "Breathing Drone": {
        # Root and fifth saws, a deep sine, the biting band-pass swept by a very slow sine; the pitch wanders.
        "voice.count": 6, "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Saw", "osc2.octave": -1, "osc2.semi": 7,
        "osc2.fine": 3, "osc2.level": 0.5, "osc3.wave": "Sine", "osc3.octave": -2, "osc3.fine": 0, "osc3.level": 0.45,
        "noise.level": 0.05, "unison.voices": 2, "unison.detune": 6, "unison.spread": 0.8, "drift": 0.8,
        "filter.model": "Biting", "filter.mode": "Band-pass", "filter.cutoff": 520, "filter.resonance": 0.62,
        "filter.drive": 0.35, "filter.env": 0.0, "filter.key": 0.2, "amp.attack": 2.0, "amp.decay": 1.0, "amp.sustain": 1.0,
        "amp.release": 3.5, "lfo2.shape": "Sine", "lfo2.rate": 0.09, "lfo2.cutoff": 0.35, "lfo1.shape": "Random",
        "lfo1.rate": 0.3, "lfo1.delay": 0.0, "lfo1.pitch": 0.03, "mod.wheel": 0.0,
    },
    "Filtered Noise Riser": {
        # Noise and wide saws, the low-pass climbing over six seconds of filter attack: a riser.
        "voice.count": 4, "osc1.wave": "Saw", "osc1.level": 0.5, "osc2.wave": "Saw", "osc2.fine": 25, "osc2.level": 0.5,
        "noise.level": 0.8, "unison.voices": 4, "unison.detune": 30, "unison.spread": 1.0, "drift": 0.4,
        "filter.model": "Multimode", "filter.mode": "Low-pass", "filter.cutoff": 120, "filter.resonance": 0.55,
        "filter.drive": 0.2, "filter.env": 0.95, "filter.key": 0.0, "filter.velocity": 0.0, "fenv.attack": 6.0,
        "fenv.decay": 4.0, "fenv.sustain": 1.0, "fenv.release": 1.0, "amp.attack": 2.5, "amp.decay": 1.0, "amp.sustain": 1.0,
        "amp.release": 0.8, "mod.wheel": 0.0,
    },
    "Saturated Low Impact": {
        # A deep sine frequency-modulated at an odd ratio, noise, the saturated ladder slamming shut: a boom.
        "voice.mode": "Mono", "osc1.wave": "Sine", "osc1.octave": -2, "osc1.level": 1.0, "osc2.wave": "Triangle",
        "osc2.octave": -1, "osc2.semi": 0, "osc2.fine": -15, "osc2.level": 0.4, "osc3.wave": "Sine", "osc3.octave": -1,
        "osc3.semi": 5, "osc3.fine": 23, "osc3.level": 0.0, "fm.amount": 0.5, "noise.level": 0.5, "drift": 0.0,
        "filter.model": "Ladder", "filter.cutoff": 70, "filter.resonance": 0.15, "filter.drive": 0.85, "filter.env": 0.85,
        "filter.key": 0.0, "filter.velocity": 0.6, "fenv.attack": 0.001, "fenv.decay": 0.9, "fenv.sustain": 0.0,
        "fenv.release": 1.0, "amp.attack": 0.001, "amp.decay": 2.5, "amp.sustain": 0.0, "amp.release": 2.5,
        "amp.velocity": 0.4, "mod.wheel": 0.0,
    },
}

SAW = 0.57  # Classic: the saw's frame


def mod(slot: int, source: str, destination: str, amount: float) -> dict:
    return {f"mod{slot}.source": source, f"mod{slot}.dest": destination, f"mod{slot}.amount": amount}


def steps(values: list[float]) -> dict:
    return {f"perf.step{index + 1}": value for index, value in enumerate(values)}


COLOSSE = {
    "Init": {},
    "Colossal Bass": {
        "voice.mode": "Mono", "osc1.position": SAW, "osc2.position": 0.85, "osc2.octave": -1, "osc2.fine": 0,
        "osc2.level": 0.5, "sub.level": 0.6, "f1.cutoff": 380, "f1.resonance": 0.3, "drive.amount": 0.4,
        "env2.decay": 0.3, "env2.sustain": 0.1, **mod(1, "Env 2", "Filters · cutoff", 0.45), "amp.release": 0.12,
    },
    "Crushed Bass": {
        "voice.mode": "Mono", "osc1.table": "Teeth", "osc1.position": 0.3, "osc1.octave": -1, "osc2.table": "Teeth",
        "osc2.position": 0.3, "osc2.octave": -1, "osc2.fine": 12, "osc2.level": 0.8, "unison.voices": 2,
        "unison.detune": 18, "f1.type": "Low-pass", "f1.cutoff": 900, "f1.resonance": 0.25, "drive.type": "Crush",
        "drive.amount": 0.3, "drive.mix": 0.5, "lfo1.rate": 0.3, **mod(1, "LFO 1", "Osc 1 · position", 0.2),
        **mod(2, "Env 2", "Filter 1 · cutoff", 0.3), "amp.release": 0.1,
    },
    "Rumble": {
        "voice.mode": "Mono", "osc1.table": "Voices", "osc1.position": 0.2, "osc1.octave": -1, "osc1.mode": "Formant",
        "osc1.warp": 0.4, "osc2.level": 0.0, "sub.level": 0.4, "f1.type": "Vowel", "f1.cutoff": 400,
        "f1.resonance": 0.6, "lfo1.sync": 6, **mod(1, "LFO 1", "Osc 1 · warp", 0.5), **mod(2, "LFO 1", "Filter 1 · cutoff", 0.35),
        "drive.type": "Fold", "drive.amount": 0.5, "drive.mix": 0.7, "amp.release": 0.1,
    },
    "Digital Wobble": {
        "voice.mode": "Mono", "osc1.table": "Digital", "osc1.position": 0.4, "osc1.octave": -1, "osc2.level": 0.3,
        "osc2.octave": -1, "sub.level": 0.5, "f1.cutoff": 250, "f1.resonance": 0.7, "lfo1.shape": "Sine", "lfo1.sync": 9,
        **mod(1, "LFO 1", "Filters · cutoff", 0.6), "drive.amount": 0.5, "amp.release": 0.1,
    },
    "Cutting Lead": {
        "voice.mode": "Legato", "voice.glide": 0.05, "osc1.table": "Sync", "osc1.position": 0.6, "osc1.mode": "Sync",
        "osc1.warp": 0.3, "osc2.position": SAW, "osc2.fine": 7, "unison.voices": 3, "unison.detune": 12,
        "f1.type": "Low-pass", "f1.cutoff": 4_000, "f1.resonance": 0.15, **mod(1, "Env 2", "Osc 1 · warp", 0.5),
        "env2.decay": 0.8, "env2.sustain": 0.3, "amp.release": 0.2,
    },
    "Metal Lead": {
        "voice.mode": "Legato", "voice.glide": 0.03, "osc1.table": "Metal", "osc1.position": 0.5, "pm.ratio": "2",
        "pm.osc1": 0.4, "osc2.position": SAW, "osc2.level": 0.3, "f1.cutoff": 3_000, "f1.resonance": 0.25,
        "drive.type": "Hard", "drive.amount": 0.3, **mod(1, "Env 2", "Phase modulation", 0.3), "amp.release": 0.2,
    },
    "Colossal Supersaw": {
        "voice.count": 8, "osc1.position": SAW, "osc2.position": SAW, "osc2.fine": 10, "osc2.level": 0.7,
        "unison.voices": 8, "unison.detune": 25, "unison.spread": 1.0, "f1.type": "Low-pass", "f1.cutoff": 6_000,
        "f1.resonance": 0.1, **mod(1, "Env 2", "Filters · cutoff", 0.15), "amp.release": 0.6,
    },
    "Moving Pad": {
        "voice.count": 10, "osc1.table": "Harmonic", "osc1.position": 0.3, "osc2.table": "Voices", "osc2.position": 0.5,
        "osc2.level": 0.6, "unison.voices": 3, "unison.detune": 14, "f1.type": "Low-pass", "f1.cutoff": 1_800,
        "env3.attack": 2.0, "env3.decay": 3.0, "env3.sustain": 0.6, **mod(1, "Env 3", "Osc 1 · position", 0.6),
        "lfo2.rate": 0.1, **mod(2, "LFO 2", "Osc 2 · position", 0.3), "amp.attack": 0.8, "amp.sustain": 0.9,
        "amp.release": 2.0,
    },
    "Vowel Pad": {
        "voice.count": 10, "osc1.position": SAW, "osc2.position": 0.85, "osc2.fine": 9, "unison.voices": 2,
        "f1.type": "Vowel", "f1.cutoff": 300, "f1.resonance": 0.5, "lfo2.rate": 0.12, "lfo2.shape": "Triangle",
        **mod(1, "LFO 2", "Filter 1 · cutoff", 0.45), "amp.attack": 0.6, "amp.sustain": 0.9, "amp.release": 1.5,
    },
    "Harmonic Pluck": {
        "osc1.table": "Harmonic", "osc1.position": 0.85, "osc2.level": 0.0, "env2.decay": 0.25, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Osc 1 · position", -0.8), "f1.cutoff": 8_000, "amp.decay": 0.45, "amp.sustain": 0.0,
        "amp.release": 0.3,
    },
    "Phase Bell": {
        "osc1.position": 0.0, "osc2.level": 0.0, "pm.ratio": "3", "pm.osc1": 0.6, "env2.decay": 1.5, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Phase modulation", 0.4), "f1.cutoff": 12_000, "amp.decay": 2.5, "amp.sustain": 0.0,
        "amp.release": 1.5,
    },
    "Filtered Sequence": {
        "osc1.position": SAW, "osc2.position": 0.85, "f1.cutoff": 300, "f1.resonance": 0.6, "perf.steps": 16,
        "perf.sync": 3, **mod(1, "Sequence", "Filter 1 · cutoff", 0.6), "amp.sustain": 1.0, "amp.release": 0.2,
    },
    "Step Arp": {
        "osc1.table": "Sync", "osc1.position": 0.3, "osc2.level": 0.0, "f1.cutoff": 3_000, "perf.steps": 8, "perf.sync": 3,
        "perf.smooth": 0.0, **steps([0.0, 0.583, 1.0, 0.583, 0.25, 0.583, 1.0, 0.833]),
        **mod(1, "Sequence", "Pitch", 0.5), **mod(2, "Env 2", "Filters · cutoff", 0.2), "amp.release": 0.15,
    },
    "Comb String": {
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.colour": "White", "noise.level": 0.0, "f1.type": "Comb",
        "f1.cutoff": 261.63, "f1.resonance": 0.97, "filter.key": 1.0, "filter.velocity": 0.0, "env2.attack": 0.001,
        "env2.decay": 0.06, "env2.sustain": 0.0, **mod(1, "Env 2", "Noise", 1.0), "drive.amount": 0.6,
        "amp.attack": 0.001, "amp.sustain": 1.0, "amp.release": 0.4,
    },
    "Scream": {
        "voice.mode": "Mono", "osc1.position": SAW, "osc2.position": 0.85, "f1.type": "Scream", "f1.cutoff": 900,
        "f1.resonance": 0.7, "lfo1.rate": 3.0, **mod(1, "LFO 1", "Filter 1 · cutoff", 0.3), "drive.amount": 0.4,
        "amp.release": 0.2,
    },
    "Deep Sub": {
        "voice.mode": "Mono", "osc1.position": 0.0, "osc1.octave": -1, "osc2.level": 0.0, "sub.level": 0.7,
        "f1.cutoff": 2_000, "mod1.amount": 0.0, "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Modern Brass": {
        "voice.count": 8, "osc1.position": SAW, "osc2.position": 0.7, "osc2.fine": 6, "f1.cutoff": 600,
        "f1.resonance": 0.15, "env2.attack": 0.06, "env2.decay": 0.5, "env2.sustain": 0.4,
        **mod(1, "Env 2", "Filters · cutoff", 0.55), "amp.attack": 0.03, "amp.release": 0.3,
    },
    "Digital Texture": {
        "osc1.table": "Digital", "osc1.position": 0.5, "osc1.mode": "Steps", "osc1.warp": 0.5, "lfo2.shape": "Random",
        "lfo2.rate": 6.0, **mod(1, "LFO 2", "Osc 1 · position", 0.4), "noise.colour": "Crackle", "noise.level": 0.3,
        "f1.type": "Low-pass", "f1.cutoff": 5_000, "amp.attack": 0.2, "amp.release": 0.8,
    },
    "Formant Choir": {
        "voice.count": 10, "osc1.table": "Voices", "osc1.position": 0.0, "osc2.table": "Voices", "osc2.position": 1.0,
        "osc2.fine": 8, "osc2.level": 0.7, "unison.voices": 4, "unison.detune": 10, "f1.type": "Low-pass",
        "f1.cutoff": 5_000, "env3.attack": 1.5, "env3.sustain": 1.0, **mod(1, "Env 3", "Osc 1 · position", 0.5),
        "amp.attack": 0.3, "amp.sustain": 0.9, "amp.release": 1.2,
    },
    "Rectified": {
        "osc1.position": SAW, "osc2.level": 0.0, "drive.type": "Rectify", "drive.amount": 0.7, "f1.type": "Band-pass",
        "f1.cutoff": 1_400, "f1.resonance": 0.4, **mod(1, "Env 2", "Filter 1 · cutoff", 0.3), "amp.release": 0.25,
    },
    "Folded Bass": {
        "voice.mode": "Mono", "osc1.table": "Folded", "osc1.position": 0.3, "osc1.octave": -1, "osc2.level": 0.0,
        "sub.level": 0.3, "env2.decay": 0.25, "env2.sustain": 0.1, **mod(1, "Env 2", "Osc 1 · position", 0.5),
        "f1.cutoff": 2_500, "amp.release": 0.1,
    },
    # ---- Basses -------------------------------------------------------------------------------
    "Massive Rolling Reese": {
        # Two saws far apart in four unison voices, a sub; LFO 2 moves the shape and the detune, LFO 1 the filter.
        "voice.mode": "Mono", "osc1.position": SAW, "osc1.octave": -1, "osc2.table": "Classic", "osc2.position": SAW,
        "osc2.octave": -1, "osc2.fine": 22, "osc2.level": 0.8, "unison.voices": 4, "unison.detune": 18,
        "unison.spread": 0.4, "sub.level": 0.5, "f1.type": "Ladder", "f1.cutoff": 600, "f1.resonance": 0.25,
        "drive.type": "Soft", "drive.amount": 0.45, "drive.mix": 0.8, "env2.decay": 0.4, "env2.sustain": 0.2,
        **mod(1, "Env 2", "Filters · cutoff", 0.15), "lfo2.shape": "Sine", "lfo2.rate": 0.18,
        **mod(2, "LFO 2", "Osc 1 · position", 0.12), "lfo1.shape": "Triangle", "lfo1.rate": 0.31,
        **mod(3, "LFO 1", "Filter 1 · cutoff", 0.08), **mod(4, "LFO 2", "Detune", 0.5),
        **mod(5, "Mod wheel", "Filter 1 · cutoff", 0.4), "amp.attack": 0.004, "amp.sustain": 1.0, "amp.release": 0.15,
    },
    "Chewing Dubstep Bass": {
        # A formant-bent voice table and a metal one, phase-modulated; a vowel filter beside a low ladder that keeps
        # the body; LFO 1 in eighths chews the vowel, the warp and the modulation together.
        "voice.mode": "Mono", "osc1.table": "Voices", "osc1.position": 0.3, "osc1.mode": "Formant", "osc1.warp": 0.35,
        "osc1.octave": -1, "osc2.table": "Metal", "osc2.position": 0.4, "osc2.mode": "Bend", "osc2.warp": 0.2,
        "osc2.octave": -1, "osc2.fine": 0, "osc2.level": 0.5, "pm.ratio": "1", "pm.osc1": 0.25, "sub.level": 0.45,
        "f1.type": "Vowel", "f1.cutoff": 500, "f1.resonance": 0.55, "f2.type": "Ladder", "f2.cutoff": 240,
        "f2.resonance": 0.1, "filter.routing": "Parallel", "filter.blend": 0.4, "drive.type": "Fold", "drive.amount": 0.55,
        "drive.mix": 0.6, "lfo1.shape": "Sine", "lfo1.sync": 6, **mod(1, "LFO 1", "Filter 1 · cutoff", 0.25),
        **mod(2, "LFO 1", "Osc 1 · warp", 0.3), **mod(3, "LFO 1", "Phase modulation", 0.2), **mod(4, "Mod wheel", "Drive", 0.4),
        **mod(5, "Pressure", "Filter 1 · cutoff", 0.2), "amp.attack": 0.003, "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Metallic FM Bass": {
        # A sine phase-modulated at twice the note, the modulation struck by envelope 2 and pushed by the velocity,
        # a folded table under it, a hard drive half mixed.
        "voice.mode": "Mono", "osc1.position": 0.0, "osc1.octave": -1, "pm.ratio": "2", "pm.osc1": 0.55,
        "osc2.table": "Folded", "osc2.position": 0.5, "osc2.octave": -1, "osc2.fine": 0, "osc2.level": 0.3, "pm.osc2": 0.2,
        "sub.level": 0.4, "f1.type": "Ladder", "f1.cutoff": 1_400, "f1.resonance": 0.3, "drive.type": "Hard",
        "drive.amount": 0.35, "drive.mix": 0.5, "env2.decay": 0.35, "env2.sustain": 0.15,
        **mod(1, "Env 2", "Phase modulation", 0.45), **mod(2, "Env 2", "Filter 1 · cutoff", 0.25), "lfo1.shape": "Triangle",
        "lfo1.sync": 5, **mod(3, "LFO 1", "Phase modulation", 0.15), **mod(4, "Velocity", "Phase modulation", 0.15),
        "amp.attack": 0.002, "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Drawn Sequence Wobble": {
        # The step sequence, eased, wobbles the ladder and the table position at its own uneven pace.
        "voice.mode": "Mono", "osc1.table": "Teeth", "osc1.position": 0.4, "osc1.octave": -1, "osc2.table": "Classic",
        "osc2.position": SAW, "osc2.octave": -1, "osc2.fine": 10, "osc2.level": 0.6, "sub.level": 0.5, "f1.type": "Ladder",
        "f1.cutoff": 450, "f1.resonance": 0.55, "drive.type": "Hard", "drive.amount": 0.45, "drive.mix": 0.6,
        "perf.steps": 8, "perf.sync": 6, "perf.smooth": 0.6, **steps([1.0, -1.0, 0.5, -1.0, 1.0, 0.2, -0.5, 1.0]),
        **mod(1, "Sequence", "Filter 1 · cutoff", 0.35), **mod(2, "Sequence", "Osc 1 · position", 0.2),
        **mod(3, "Mod wheel", "Drive", 0.4), "amp.attack": 0.003, "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Clicky Deep Sub": {
        # A sine and the sub, a burst of noise and a pitch blip from a 25 ms envelope: a sub that is heard on the beat.
        "voice.mode": "Mono", "osc1.position": 0.0, "osc1.octave": -1, "osc2.level": 0.0, "sub.level": 0.6,
        "noise.colour": "White", "noise.level": 0.0, "env2.attack": 0.001, "env2.decay": 0.025, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Noise", 0.7), **mod(2, "Env 2", "Pitch", 0.15), "f1.type": "Low-pass", "f1.cutoff": 1_500,
        "f1.resonance": 0.0, "filter.key": 0.0, "drive.type": "Soft", "drive.amount": 0.2, "drive.mix": 0.5,
        "amp.attack": 0.001, "amp.sustain": 1.0, "amp.release": 0.09,
    },
    # ---- Leads --------------------------------------------------------------------------------
    "Epic Trance Supersaw": {
        # Eight unison saws and their octave, a high-pass in series cleaning the lows; the wheel widens the detune.
        "voice.count": 10, "osc1.position": SAW, "osc2.table": "Classic", "osc2.position": SAW, "osc2.octave": 1,
        "osc2.fine": 0, "osc2.level": 0.35, "unison.voices": 8, "unison.detune": 32, "unison.spread": 1.0,
        "f1.type": "Low-pass", "f1.cutoff": 6_500, "f1.resonance": 0.12, "f2.type": "High-pass", "f2.cutoff": 120,
        "f2.resonance": 0.1, "filter.routing": "Serial", "filter.blend": 1.0, "filter.key": 0.2, "env2.attack": 0.001,
        "env2.decay": 0.8, "env2.sustain": 0.4, **mod(1, "Env 2", "Filter 1 · cutoff", 0.12),
        **mod(2, "Mod wheel", "Detune", 0.6), **mod(3, "Velocity", "Filter 1 · cutoff", 0.1), "lfo2.rate": 0.3,
        **mod(4, "LFO 2", "Osc 2 · position", 0.1), "amp.attack": 0.005, "amp.decay": 1.0, "amp.sustain": 0.9,
        "amp.release": 0.7,
    },
    "Giant Glide Lead": {
        # Six unison saws and a synced table, legato glide; the pressure opens the filter, the wheel bends the sync.
        "voice.mode": "Legato", "voice.glide": 0.08, "osc1.position": SAW, "osc2.table": "Sync", "osc2.position": 0.4,
        "osc2.mode": "Sync", "osc2.warp": 0.25, "osc2.fine": 5, "osc2.level": 0.4, "unison.voices": 6, "unison.detune": 20,
        "unison.spread": 0.9, "f1.type": "Low-pass", "f1.cutoff": 3_200, "f1.resonance": 0.2, "drive.type": "Soft",
        "drive.amount": 0.25, "drive.mix": 0.5, "env2.attack": 0.005, "env2.decay": 0.6, "env2.sustain": 0.4,
        **mod(1, "Env 2", "Filters · cutoff", 0.18), "lfo1.shape": "Sine", "lfo1.rate": 5.5, **mod(2, "LFO 1", "Pitch", 0.003),
        **mod(3, "Pressure", "Filter 1 · cutoff", 0.25), **mod(4, "Mod wheel", "Osc 2 · warp", 0.5),
        "amp.attack": 0.004, "amp.sustain": 0.95, "amp.release": 0.3,
    },
    "Diving Sync Lead": {
        # Envelope 3 throws the sync table to its top and lets it dive back, position and warp together.
        "voice.mode": "Legato", "voice.glide": 0.04, "osc1.table": "Sync", "osc1.position": 0.0, "osc1.mode": "Sync",
        "osc1.warp": 0.0, "osc2.table": "Classic", "osc2.position": SAW, "osc2.fine": -6, "osc2.level": 0.4,
        "unison.voices": 2, "unison.detune": 8, "f1.type": "Low-pass", "f1.cutoff": 5_000, "f1.resonance": 0.15,
        "drive.type": "Hard", "drive.amount": 0.25, "drive.mix": 0.4, "env3.attack": 0.001, "env3.decay": 1.2,
        "env3.sustain": 0.2, "env3.release": 0.5, **mod(1, "Env 3", "Osc 1 · position", 0.9),
        **mod(2, "Env 3", "Osc 1 · warp", 0.6), "lfo1.rate": 6.0, **mod(3, "LFO 1", "Pitch", 0.002),
        **mod(4, "Mod wheel", "Osc 1 · warp", 0.4), **mod(5, "Velocity", "Filter 1 · cutoff", 0.1), "amp.release": 0.25,
    },
    "Screaming Vibrato Lead": {
        # A saw and a square into the scream filter, which swells with envelope 2; the pressure pushes it further.
        "voice.mode": "Legato", "voice.glide": 0.06, "osc1.position": SAW, "osc2.table": "Classic", "osc2.position": 0.857,
        "osc2.fine": -7, "osc2.level": 0.5, "unison.voices": 3, "unison.detune": 9, "f1.type": "Scream", "f1.cutoff": 1_400,
        "f1.resonance": 0.65, "env2.attack": 0.3, "env2.decay": 1.0, "env2.sustain": 0.6,
        **mod(1, "Env 2", "Filter 1 · cutoff", 0.2), "lfo1.rate": 5.5, **mod(2, "LFO 1", "Pitch", 0.004),
        **mod(3, "Pressure", "Filter 1 · cutoff", 0.3), **mod(4, "Mod wheel", "Resonance", 0.3), "drive.type": "Rectify",
        "drive.amount": 0.3, "drive.mix": 0.4, "amp.release": 0.25,
    },
    "Massive Cinematic Brass": {
        # Four unison saw pairs, the ladder opened by a slow envelope 2, the velocity and the pressure.
        "voice.count": 8, "osc1.position": SAW, "osc2.table": "Classic", "osc2.position": SAW, "osc2.fine": 8,
        "osc2.level": 0.7, "unison.voices": 4, "unison.detune": 10, "unison.spread": 0.8, "sub.level": 0.15,
        "f1.type": "Ladder", "f1.cutoff": 380, "f1.resonance": 0.15, "filter.key": 0.6, "env2.attack": 0.12,
        "env2.decay": 0.8, "env2.sustain": 0.5, "env2.release": 0.5, **mod(1, "Env 2", "Filters · cutoff", 0.5),
        **mod(2, "Velocity", "Filter 1 · cutoff", 0.12), **mod(3, "Pressure", "Filter 1 · cutoff", 0.2), "lfo1.rate": 5.0,
        **mod(4, "LFO 1", "Pitch", 0.002), "drive.type": "Soft", "drive.amount": 0.25, "drive.mix": 0.7,
        "amp.attack": 0.06, "amp.decay": 1.0, "amp.sustain": 0.9, "amp.release": 0.4,
    },
    # ---- Keys, bells, plucks, sequences -------------------------------------------------------
    "Crystal Bells": {
        # A sine phase-modulated at seven times the note, struck by envelope 2, less in the highs; a metal table above.
        "osc1.position": 0.0, "osc2.table": "Metal", "osc2.position": 0.7, "osc2.octave": 1, "osc2.fine": 3,
        "osc2.level": 0.2, "pm.ratio": "7", "pm.osc1": 0.25, "env2.attack": 0.001, "env2.decay": 2.5, "env2.sustain": 0.0,
        "env2.release": 2.0, **mod(1, "Env 2", "Phase modulation", 0.25), **mod(2, "Velocity", "Phase modulation", 0.1),
        **mod(3, "Key", "Phase modulation", -0.2), "unison.voices": 3, "unison.detune": 5, "unison.spread": 1.0,
        "f1.type": "Low-pass", "f1.cutoff": 9_000, "f1.resonance": 0.0, "amp.attack": 0.002, "amp.decay": 4.0,
        "amp.sustain": 0.0, "amp.release": 3.0,
    },
    "Stereo Electric Piano": {
        # A sine phase-modulated at the note for the bark (envelope, velocity, wheel), LFO 2 panning it side to side.
        "voice.count": 12, "osc1.position": 0.0, "osc2.table": "Harmonic", "osc2.position": 0.1, "osc2.octave": 1,
        "osc2.fine": 2, "osc2.level": 0.12, "pm.ratio": "1", "pm.osc1": 0.2, "env2.attack": 0.001, "env2.decay": 1.2,
        "env2.sustain": 0.05, **mod(1, "Env 2", "Phase modulation", 0.35), **mod(2, "Velocity", "Phase modulation", 0.25),
        "lfo2.shape": "Sine", "lfo2.rate": 4.5, **mod(3, "LFO 2", "Pan", 0.35), **mod(4, "Mod wheel", "Phase modulation", 0.3),
        "f1.type": "Low-pass", "f1.cutoff": 6_000, "f1.resonance": 0.05, "filter.key": 0.5, "amp.attack": 0.001,
        "amp.decay": 3.5, "amp.sustain": 0.0, "amp.release": 0.6, "amp.velocity": 0.7,
    },
    "Glittering Digital Pluck": {
        # A stepped digital table and a bright harmonic one, envelope 2 snapping the filter and the position.
        "voice.count": 12, "osc1.table": "Digital", "osc1.position": 0.6, "osc1.mode": "Steps", "osc1.warp": 0.3,
        "osc2.table": "Harmonic", "osc2.position": 0.8, "osc2.octave": 1, "osc2.fine": 5, "osc2.level": 0.3,
        "unison.voices": 4, "unison.detune": 12, "unison.spread": 1.0, "f1.type": "Low-pass", "f1.cutoff": 500,
        "f1.resonance": 0.35, "env2.attack": 0.001, "env2.decay": 0.18, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Filters · cutoff", 0.5), **mod(2, "Env 2", "Osc 1 · position", -0.4),
        **mod(3, "Velocity", "Filters · cutoff", 0.12), **mod(4, "Random", "Osc 2 · position", 0.15),
        "amp.attack": 0.001, "amp.decay": 0.45, "amp.sustain": 0.0, "amp.release": 0.3,
    },
    "Kaleidoscope Pluck": {
        # Each note its own colour and place: the per-note random moves both positions and the pan.
        "voice.count": 12, "osc1.table": "Harmonic", "osc1.position": 0.5, "osc2.table": "Metal", "osc2.position": 0.3,
        "osc2.octave": 1, "osc2.fine": 0, "osc2.level": 0.25, "env2.decay": 0.25, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Filters · cutoff", 0.45), **mod(2, "Random", "Osc 1 · position", 0.35),
        **mod(3, "Random", "Pan", 0.5), **mod(4, "Random", "Osc 2 · position", 0.3), "f1.type": "Low-pass",
        "f1.cutoff": 700, "f1.resonance": 0.25, "unison.voices": 2, "unison.detune": 7, "amp.decay": 0.5,
        "amp.sustain": 0.0, "amp.release": 0.4,
    },
    "House Organ Stab": {
        # A few harmonics and an octave square, a touch of phase modulation; envelope 2 kicks the ladder, a high-pass after.
        "voice.count": 10, "osc1.table": "Harmonic", "osc1.position": 0.3, "osc2.table": "Classic", "osc2.position": 0.857,
        "osc2.octave": 1, "osc2.fine": 0, "osc2.level": 0.3, "pm.ratio": "2", "pm.osc1": 0.15, "f1.type": "Ladder",
        "f1.cutoff": 1_100, "f1.resonance": 0.2, "f2.type": "High-pass", "f2.cutoff": 220, "f2.resonance": 0.1,
        "filter.routing": "Serial", "filter.blend": 1.0, "env2.attack": 0.001, "env2.decay": 0.25, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Filter 1 · cutoff", 0.35), **mod(2, "Env 2", "Phase modulation", 0.2),
        **mod(3, "Velocity", "Filter 1 · cutoff", 0.12), "amp.attack": 0.002, "amp.decay": 0.6, "amp.sustain": 0.3,
        "amp.release": 0.25,
    },
    "Cascading Fifths Arp": {
        # One held key plays an arpeggio: the sequence steps the pitch through fifths and octaves in sixteenths,
        # a sixteenth-note saw LFO plucks the volume and the filter on each step.
        "osc1.table": "Harmonic", "osc1.position": 0.55, "osc2.table": "Classic", "osc2.position": 0.857, "osc2.octave": -1,
        "osc2.fine": 0, "osc2.level": 0.3, "f1.type": "Low-pass", "f1.cutoff": 1_800, "f1.resonance": 0.3, "perf.steps": 8,
        "perf.sync": 3, "perf.smooth": 0.0, **steps([0.0, 7 / 24, 12 / 24, 19 / 24, 1.0, 19 / 24, 12 / 24, 7 / 24]),
        **mod(1, "Sequence", "Pitch", 1.0), "lfo1.shape": "Saw", "lfo1.sync": 3, **mod(2, "LFO 1", "Volume", 0.6),
        **mod(3, "LFO 1", "Filter 1 · cutoff", 0.2), **mod(4, "Mod wheel", "Osc 1 · position", 0.4), "amp.sustain": 1.0,
        "amp.release": 0.2,
    },
    "Dark Techno Sequence": {
        # Accents drawn in the sixteen steps open the resonant ladder and the drive; LFO 2 over a bar rides the resonance.
        "voice.mode": "Mono", "osc1.table": "Teeth", "osc1.position": 0.6, "osc1.octave": -1, "osc2.table": "Classic",
        "osc2.position": 0.857, "osc2.octave": -1, "osc2.fine": 0, "osc2.level": 0.3, "f1.type": "Ladder", "f1.cutoff": 200,
        "f1.resonance": 0.6, "perf.steps": 16, "perf.sync": 3, "perf.smooth": 0.0,
        **steps([1.0, 0.2, 0.6, 0.0, 1.0, 0.3, 0.8, 0.1, 1.0, 0.2, 0.5, 0.0, 0.9, 0.6, 0.3, 0.7]),
        **mod(1, "Sequence", "Filter 1 · cutoff", 0.45), **mod(2, "Sequence", "Drive", 0.3), "drive.type": "Fold",
        "drive.amount": 0.2, "drive.mix": 0.5, "lfo2.shape": "Triangle", "lfo2.sync": 12, **mod(3, "LFO 2", "Resonance", 0.15),
        **mod(4, "Mod wheel", "Filter 1 · cutoff", 0.3), "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Chopped Trance Gate": {
        # Seven unison saw pairs cut by a sixteen-step gate (a step at -1 silences it), the filter riding a bar.
        "voice.count": 8, "osc1.position": SAW, "osc2.table": "Classic", "osc2.position": SAW, "osc2.fine": 12,
        "osc2.level": 0.6, "unison.voices": 7, "unison.detune": 24, "unison.spread": 1.0, "f1.type": "Low-pass",
        "f1.cutoff": 4_500, "f1.resonance": 0.15, "perf.steps": 16, "perf.sync": 3, "perf.smooth": 0.05,
        **steps([0.0, 0.0, -1.0, 0.0, 0.0, -1.0, 0.0, -1.0, 0.0, 0.0, -1.0, 0.0, 0.0, -1.0, 0.0, 0.0]),
        **mod(1, "Sequence", "Volume", 1.0), "lfo2.shape": "Triangle", "lfo2.sync": 12,
        **mod(2, "LFO 2", "Filter 1 · cutoff", 0.12), **mod(3, "Mod wheel", "Filter 1 · cutoff", -0.3),
        "amp.attack": 0.01, "amp.sustain": 1.0, "amp.release": 0.4,
    },
    # ---- Pads, textures, effects --------------------------------------------------------------
    "Colossal Evolving Pad": {
        # Harmonic and mirrored voice tables, six unison voices; two slow LFOs scan them, envelope 3 opens the low-pass,
        # a vowel filter beside it; each note its own place in the stereo.
        "voice.count": 10, "osc1.table": "Harmonic", "osc1.position": 0.2, "osc2.table": "Voices", "osc2.position": 0.6,
        "osc2.mode": "Mirror", "osc2.warp": 0.2, "osc2.fine": 9, "osc2.level": 0.6, "unison.voices": 6, "unison.detune": 16,
        "unison.spread": 1.0, "f1.type": "Low-pass", "f1.cutoff": 1_100, "f1.resonance": 0.2, "f2.type": "Vowel",
        "f2.cutoff": 600, "f2.resonance": 0.3, "filter.routing": "Parallel", "filter.blend": 0.35, "env3.attack": 3.5,
        "env3.decay": 4.0, "env3.sustain": 0.7, "env3.release": 3.0, **mod(1, "Env 3", "Filter 1 · cutoff", 0.25),
        "lfo1.shape": "Triangle", "lfo1.rate": 0.09, **mod(2, "LFO 1", "Osc 1 · position", 0.35), "lfo2.shape": "Sine",
        "lfo2.rate": 0.13, **mod(3, "LFO 2", "Osc 2 · position", 0.3), **mod(4, "LFO 2", "Filter 2 · cutoff", 0.2),
        **mod(5, "Mod wheel", "Detune", 0.8), **mod(6, "Random", "Pan", 0.3), "amp.attack": 1.5, "amp.decay": 2.0,
        "amp.sustain": 0.95, "amp.release": 3.5,
    },
    "Digital Frost Pad": {
        # A mirrored metal table and a bright harmonic octave, a breath of noise, between a high-pass and a low-pass;
        # envelope 3 brings in a phase modulation at four times the note.
        "voice.count": 10, "osc1.table": "Metal", "osc1.position": 0.2, "osc1.mode": "Mirror", "osc1.warp": 0.3,
        "osc1.level": 1.0, "osc2.table": "Harmonic", "osc2.position": 0.9, "osc2.octave": 1, "osc2.fine": 4,
        "osc2.level": 0.45,
        "pm.ratio": "4", "pm.osc1": 0.15, "unison.voices": 4, "unison.detune": 10, "unison.spread": 1.0,
        "noise.colour": "Breath", "noise.level": 0.08, "f1.type": "High-pass", "f1.cutoff": 350, "f1.resonance": 0.15,
        "f2.type": "Low-pass", "f2.cutoff": 9_000, "f2.resonance": 0.1, "filter.routing": "Serial", "filter.blend": 1.0,
        "filter.key": 0.3, "lfo2.shape": "Sine", "lfo2.rate": 0.2, **mod(1, "LFO 2", "Osc 1 · position", 0.2),
        "env3.attack": 2.0, "env3.decay": 3.0, "env3.sustain": 0.4, **mod(2, "Env 3", "Phase modulation", 0.3),
        "lfo1.shape": "Triangle", "lfo1.rate": 0.33, **mod(3, "LFO 1", "Osc 1 · warp", 0.2),
        **mod(4, "Mod wheel", "Filter 2 · cutoff", -0.3), "amp.attack": 1.0, "amp.decay": 2.0, "amp.sustain": 0.9,
        "amp.release": 3.0,
    },
    "Abyssal Pad": {
        # A folded table and a saw an octave down, a sub and pink noise; the ladder opens over seconds, a band-pass
        # beside it wanders at random.
        "voice.count": 8, "osc1.table": "Folded", "osc1.position": 0.15, "osc1.octave": -1, "osc1.level": 1.0,
        "osc2.table": "Classic", "osc2.position": SAW, "osc2.octave": -1, "osc2.fine": 7, "osc2.level": 0.7, "sub.level": 0.3, "unison.voices": 5,
        "unison.detune": 12, "unison.spread": 1.0, "noise.colour": "Pink", "noise.level": 0.08, "f1.type": "Ladder",
        "f1.cutoff": 400, "f1.resonance": 0.4, "f2.type": "Band-pass", "f2.cutoff": 700, "f2.resonance": 0.5,
        "filter.routing": "Parallel", "filter.blend": 0.25, "filter.key": 0.2, "env3.attack": 4.0, "env3.decay": 5.0,
        "env3.sustain": 0.5, "env3.release": 3.0, **mod(1, "Env 3", "Filter 1 · cutoff", 0.3), "lfo2.shape": "Sine",
        "lfo2.rate": 0.05, **mod(2, "LFO 2", "Osc 1 · position", 0.2), "lfo1.shape": "Random", "lfo1.rate": 0.4,
        **mod(3, "LFO 1", "Filter 2 · cutoff", 0.15), **mod(4, "Mod wheel", "Filter 1 · cutoff", 0.3), "drive.type": "Soft",
        "drive.amount": 0.2, "amp.attack": 2.5, "amp.decay": 2.0, "amp.sustain": 1.0, "amp.release": 3.5,
    },
    "Crumbling Lo-Fi Texture": {
        # A stepped voice table crushed to fewer bits and samples, crackle; a random LFO picks its position.
        "voice.count": 8, "osc1.table": "Voices", "osc1.position": 0.4, "osc1.mode": "Steps", "osc1.warp": 0.3,
        "osc2.table": "Harmonic", "osc2.position": 0.3, "osc2.octave": -1, "osc2.level": 0.4, "drive.type": "Crush",
        "drive.amount": 0.55, "drive.mix": 0.6, "noise.colour": "Crackle", "noise.level": 0.25, "lfo2.shape": "Random",
        "lfo2.rate": 3.0, **mod(1, "LFO 2", "Osc 1 · position", 0.25), "lfo1.shape": "Sine", "lfo1.rate": 0.4,
        **mod(2, "LFO 1", "Drive", 0.2), **mod(3, "LFO 1", "Pitch", 0.002), "f1.type": "Low-pass", "f1.cutoff": 3_500,
        "f1.resonance": 0.15, "amp.attack": 0.3, "amp.decay": 1.5, "amp.sustain": 0.85, "amp.release": 1.5,
    },
    "Giant Drop Riser": {
        # Envelope 3 over six seconds lifts the pitch an octave, opens the filter, widens the detune, adds noise and level.
        "voice.count": 4, "osc1.position": SAW, "osc2.table": "Teeth", "osc2.position": 0.5, "osc2.fine": 15,
        "osc2.level": 0.5, "unison.voices": 8, "unison.detune": 25, "unison.spread": 1.0, "noise.colour": "White",
        "noise.level": 0.2, "f1.type": "Low-pass", "f1.cutoff": 300, "f1.resonance": 0.45, "filter.key": 0.0,
        "filter.velocity": 0.0, "env3.attack": 6.0, "env3.decay": 2.0, "env3.sustain": 1.0, "env3.release": 1.0,
        **mod(1, "Env 3", "Pitch", 0.5), **mod(2, "Env 3", "Filter 1 · cutoff", 0.55), **mod(3, "Env 3", "Detune", 1.0),
        **mod(4, "Env 3", "Noise", 0.4), "lfo1.shape": "Sine", "lfo1.sync": 3, **mod(5, "LFO 1", "Filter 1 · cutoff", 0.06),
        **mod(6, "Env 3", "Volume", 0.5), "amp.attack": 1.5, "amp.decay": 1.0, "amp.sustain": 1.0, "amp.release": 1.0,
    },
    "Collapsing Impact": {
        # Envelope 2 drops the pitch a tenth and a half, closes the filter and the noise, folds the table back.
        "voice.mode": "Mono", "osc1.table": "Folded", "osc1.position": 0.6, "osc1.octave": -1, "osc2.table": "Classic",
        "osc2.position": 0.0, "osc2.octave": -2, "osc2.fine": 0, "osc2.level": 0.6, "sub.level": 0.6,
        "noise.colour": "Pink", "noise.level": 0.0, "env2.attack": 0.001, "env2.decay": 1.6, "env2.sustain": 0.0,
        "env2.release": 1.5, **mod(1, "Env 2", "Pitch", 0.75), **mod(2, "Env 2", "Noise", 0.6),
        **mod(3, "Env 2", "Filter 1 · cutoff", 0.55), **mod(4, "Env 2", "Osc 1 · position", 0.4), "f1.type": "Low-pass",
        "f1.cutoff": 180, "f1.resonance": 0.2, "drive.type": "Soft", "drive.amount": 0.6, "drive.mix": 0.8,
        "amp.attack": 0.001, "amp.decay": 2.5, "amp.sustain": 0.0, "amp.release": 2.5,
    },
}


def points(values: list[float]) -> dict:
    return {f"fn.point{index + 1}": value for index, value in enumerate(values)}


SILENT_B = {"b.level": 0.0}

MOSAIQUE = {
    "Init": {},
    "Marimba": {
        "a.type": "Modal", "a.material": "Wood", "a.exciter": "Mallet", "a.tone": 0.45, "a.strike": 0.25, "a.decay": 1.2,
        **SILENT_B, "f1.cutoff": 12_000, "mod1.amount": 0.0, "amp.sustain": 1.0, "amp.release": 0.6,
    },
    "Vibraphone": {
        "a.type": "Modal", "a.material": "Metal", "a.exciter": "Mallet", "a.tone": 0.55, "a.strike": 0.2, "a.decay": 4.0,
        **SILENT_B, "f1.cutoff": 12_000, "mod1.amount": 0.0, "lfo2.rate": 5.0, "lfo2.shape": "Sine",
        **mod(7, "LFO 2", "Volume", -0.3), "amp.sustain": 1.0, "amp.release": 1.5,
    },
    "Glass Bells": {
        "a.type": "Modal", "a.material": "Glass", "a.exciter": "Mallet", "a.tone": 0.8, "a.decay": 5.0, "b.type": "Harmonic",
        "b.partials": 6, "b.bright": 0.3, "b.octave": 1, "b.level": 0.15, "f1.cutoff": 14_000, "mod1.amount": 0.0,
        "amp.decay": 4.0, "amp.sustain": 0.0, "amp.release": 3.0,
    },
    "Struck String": {
        "a.type": "Modal", "a.material": "String", "a.exciter": "Mallet", "a.tone": 0.7, "a.strike": 0.15, "a.decay": 3.0,
        "b.type": "Analog", "b.level": 0.12, "f1.cutoff": 3_000, "mod1.amount": 0.2, "amp.sustain": 1.0, "amp.release": 0.8,
    },
    "Skin Drum": {
        "a.type": "Modal", "a.material": "Skin", "a.exciter": "Mallet", "a.tone": 0.4, "a.decay": 0.6, "a.octave": -1,
        **SILENT_B, "f1.cutoff": 8_000, "mod1.amount": 0.0, "amp.decay": 0.8, "amp.sustain": 0.0, "amp.release": 0.4,
    },
    "Bowed Glass": {
        "a.type": "Modal", "a.material": "Glass", "a.exciter": "Bow", "a.tone": 0.5, "a.decay": 3.0, **SILENT_B,
        "f1.cutoff": 9_000, "mod1.amount": 0.0, "amp.attack": 0.3, "amp.sustain": 1.0, "amp.release": 1.5,
    },
    "Bowed Cello": {
        "voice.mode": "Legato", "voice.glide": 0.05, "a.type": "Modal", "a.material": "String", "a.exciter": "Bow",
        "a.tone": 0.45, "a.decay": 2.0, "a.octave": -1, **SILENT_B, "f1.cutoff": 5_000, "mod1.amount": 0.0,
        "lfo1.rate": 5.0, **mod(7, "LFO 1", "Pitch", 0.01), "amp.attack": 0.15, "amp.sustain": 1.0, "amp.release": 0.6,
    },
    "Harmonic Organ": {
        "a.type": "Harmonic", "a.partials": 8, "a.bright": 0.2, "a.parity": -0.6, "b.type": "Harmonic", "b.partials": 4,
        "b.octave": 1, "b.fine": 0, "b.level": 0.4, "f1.cutoff": 10_000, "mod1.amount": 0.0, "amp.attack": 0.01,
        "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Additive Pad": {
        "voice.count": 10, "a.type": "Harmonic", "a.partials": 32, "a.bright": -0.2, "a.stretch": 0.2, "b.table": "Voices",
        "b.position": 0.5, "b.level": 0.35, "env3.attack": 2.0, "env3.sustain": 0.8, **mod(7, "Env 3", "A · timbre 1", 0.6),
        "lfo2.rate": 0.15, **mod(8, "LFO 2", "B · timbre 1", 0.3), "f1.type": "Low-pass", "f1.cutoff": 6_000,
        "mod1.amount": 0.0, "unison.voices": 2, "amp.attack": 0.8, "amp.sustain": 0.9, "amp.release": 2.0,
    },
    "Stretched Spectrum": {
        "a.type": "Harmonic", "a.partials": 24, "a.bright": 0.1, "a.stretch": 0.8, **SILENT_B, "f1.cutoff": 12_000,
        "mod1.amount": 0.0, "env2.decay": 1.5, "env2.sustain": 0.0, **mod(7, "Env 2", "A · timbre 1", 0.5),
        "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 2.0,
    },
    "Fat Analog": {
        "a.type": "Analog", "a.wave": 0.67, "b.type": "Analog", "b.wave": 1.0, "b.octave": -1, "b.fine": 0, "b.level": 0.5,
        "f1.cutoff": 900, "f1.resonance": 0.3, **mod(1, "Env 2", "Filters · cutoff", 0.4), "amp.release": 0.25,
    },
    "Screaming Sync": {
        "voice.mode": "Mono", "a.type": "Analog", "a.wave": 0.67, "a.sync": 19, **SILENT_B, "f1.type": "Scream",
        "f1.cutoff": 1_200, "f1.resonance": 0.6, "lfo1.rate": 2.0, **mod(1, "LFO 1", "Filter 1 · cutoff", 0.25),
        "drive.amount": 0.3, "amp.release": 0.2,
    },
    "Moving Wavetables": {
        "voice.count": 10, "a.type": "Wavetable", "a.table": "Metal", "a.position": 0.2, "lfo1.rate": 0.2,
        **mod(7, "LFO 1", "A · timbre 1", 0.4), "b.table": "Voices", "b.position": 0.6, "b.level": 0.6, "lfo2.rate": 0.13,
        **mod(8, "LFO 2", "B · timbre 1", 0.3), "unison.voices": 3, "unison.detune": 14, "f1.type": "Low-pass",
        "f1.cutoff": 4_000, "mod1.amount": 0.0, "amp.attack": 0.5, "amp.sustain": 0.9, "amp.release": 1.8,
    },
    "Looping Random Arp": {
        "a.type": "Analog", "a.wave": 0.67, **SILENT_B, "f1.cutoff": 400, "f1.resonance": 0.5, "turing.sync": 3,
        "turing.change": 0.15, **mod(1, "Turing", "Filters · cutoff", 0.5), "amp.decay": 0.25, "amp.sustain": 0.3,
        "amp.release": 0.2,
    },
    "Melodic Random": {
        "a.type": "Wavetable", "a.table": "Sync", "a.position": 0.3, **SILENT_B, "turing.sync": 5, "turing.change": 0.08,
        "turing.length": 8, **mod(7, "Turing", "Pitch", 0.5), "f1.cutoff": 3_500, "mod1.amount": 0.2, "amp.release": 0.2,
    },
    "Pulsing Function": {
        "voice.count": 10, "a.type": "Analog", "a.wave": 0.67, "b.type": "Analog", "b.wave": 0.67, "b.fine": 9, "b.level": 0.7,
        "unison.voices": 2, "f1.type": "Low-pass", "f1.cutoff": 5_000, "mod1.amount": 0.0, "fn.points": 8, "fn.sync": 9,
        "fn.smooth": 0.05, **points([0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0]), **mod(7, "Function", "Volume", -1.0),
        "amp.sustain": 1.0, "amp.release": 0.3,
    },
    "Filtered Function": {
        "a.type": "Analog", "a.wave": 0.67, "b.level": 0.4, "f1.cutoff": 250, "f1.resonance": 0.55, "fn.sync": 12,
        "fn.smooth": 1.0, **mod(1, "Function", "Filters · cutoff", 0.6), "amp.sustain": 1.0, "amp.release": 0.4,
    },
    "Hybrid Pluck": {
        "a.type": "Wavetable", "a.table": "Harmonic", "a.position": 0.8, "env2.decay": 0.3, "env2.sustain": 0.0,
        **mod(7, "Env 2", "A · timbre 1", -0.7), "b.type": "Modal", "b.material": "String", "b.exciter": "Mallet",
        "b.tone": 0.6, "b.level": 0.5, "f1.cutoff": 6_000, "mod1.amount": 0.1, "amp.decay": 0.5, "amp.sustain": 0.0,
        "amp.release": 0.35,
    },
    "Hybrid Bass": {
        "voice.mode": "Mono", "a.type": "Analog", "a.wave": 0.67, "a.octave": -1, "b.type": "Harmonic", "b.partials": 2,
        "b.octave": -1, "b.fine": 0, "b.level": 0.6, "f1.cutoff": 300, "f1.resonance": 0.25,
        **mod(1, "Env 2", "Filters · cutoff", 0.5), "env2.decay": 0.3, "env2.sustain": 0.1, "amp.release": 0.12,
    },
    "Wind Voices": {
        "voice.count": 10, "a.type": "Harmonic", "a.partials": 12, "a.level": 0.3, "a.send": 1.0, **SILENT_B,
        "noise.colour": "Breath", "noise.level": 0.6, "noise.send": 1.0, "f2.type": "Vowel", "f2.cutoff": 400,
        "f2.resonance": 0.6, "lfo2.rate": 0.2, **mod(7, "LFO 2", "Filter 2 · cutoff", 0.3), "mod1.amount": 0.0,
        "amp.attack": 0.6, "amp.sustain": 1.0, "amp.release": 1.5,
    },
    "Morphing Macros": {
        "a.type": "Analog", "a.wave": 0.0, "b.type": "Wavetable", "b.table": "Classic", "b.position": 0.0, "b.level": 0.6,
        "macro1": 0.5, "macro2": 0.3, "f1.cutoff": 1_500, "amp.attack": 0.05, "amp.sustain": 0.9, "amp.release": 0.8,
    },
    # ---- Basses -------------------------------------------------------------------------------
    "Improvising Acid Bass": {
        # A saw in the resonant ladder, a square sent to its own low-pass beside it; the random sequence nudges
        # the cutoff every sixteenth so the line never plays twice the same.
        "voice.mode": "Legato", "voice.glide": 0.07, "a.type": "Analog", "a.wave": 0.67, "a.octave": -1, "b.type": "Analog",
        "b.wave": 1.0, "b.pw": 0.5, "b.octave": -1, "b.fine": 0, "b.level": 0.35, "b.send": 1.0, "f1.type": "Ladder",
        "f1.cutoff": 280, "f1.resonance": 0.8, "f2.type": "Low-pass", "f2.cutoff": 700, "f2.resonance": 0.2,
        "filter.routing": "Parallel", "filter.key": 0.4, "filter.velocity": 0.6, "env2.attack": 0.001, "env2.decay": 0.18,
        "env2.sustain": 0.0, "mod1.amount": 0.55, "turing.sync": 3, "turing.change": 0.15, "turing.length": 16,
        **mod(7, "Turing", "Filter 1 · cutoff", 0.18), **mod(8, "Velocity", "Resonance", 0.1), "drive.type": "Soft",
        "drive.amount": 0.35, "amp.attack": 0.001, "amp.decay": 0.4, "amp.sustain": 0.9, "amp.release": 0.06,
    },
    "Growling Harmonic Bass": {
        # Thirty-two partials whose slope and odd/even balance LFO 1 swings in eighths (the growl), a formant voice
        # table sent to a vowel filter that moves with them; the ladder keeps the low end.
        "voice.mode": "Mono", "a.type": "Harmonic", "a.partials": 32, "a.bright": 0.2, "a.parity": 0.0, "a.octave": -1,
        "b.type": "Wavetable", "b.table": "Voices", "b.position": 0.3, "b.mode": "Formant", "b.warp": 0.3, "b.octave": -1,
        "b.fine": 0, "b.level": 0.45, "b.send": 1.0, "f1.type": "Ladder", "f1.cutoff": 900, "f1.resonance": 0.3,
        "f2.type": "Vowel", "f2.cutoff": 500, "f2.resonance": 0.5, "filter.routing": "Parallel", "lfo1.shape": "Sine",
        "lfo1.sync": 6, **mod(7, "LFO 1", "A · timbre 1", 0.6), **mod(8, "LFO 1", "A · timbre 2", 0.8),
        **mod(2, "LFO 1", "B · timbre 1", 0.3), **mod(3, "LFO 1", "Filter 2 · cutoff", 0.2), "mod1.amount": 0.2,
        "env2.decay": 0.3, "env2.sustain": 0.1, "drive.type": "Fold", "drive.amount": 0.3, "amp.attack": 0.003,
        "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Struck Wooden Bass": {
        # A wooden bar struck an octave down over a pure sine: the attack of a mallet, the body of a sub.
        "voice.mode": "Mono", "a.type": "Modal", "a.material": "Wood", "a.exciter": "Mallet", "a.tone": 0.3,
        "a.strike": 0.2, "a.decay": 0.8, "a.octave": -1, "b.type": "Analog", "b.wave": 0.0, "b.octave": -1, "b.fine": 0,
        "b.level": 0.6, "f1.type": "Low-pass", "f1.cutoff": 1_500, "f1.resonance": 0.1, "filter.key": 0.5,
        "env2.decay": 0.15, "env2.sustain": 0.0, "mod1.amount": 0.3, "drive.type": "Soft", "drive.amount": 0.25,
        **mod(7, "Velocity", "A · level", 0.3), "amp.attack": 0.002, "amp.decay": 0.8, "amp.sustain": 0.4,
        "amp.release": 0.15,
    },
    "Undulating Hybrid Reese": {
        # An analog saw against a bent wavetable saw twenty cents apart; two slow LFOs bend one and reshape the other.
        "voice.mode": "Mono", "a.type": "Analog", "a.wave": 0.67, "a.octave": -1, "b.type": "Wavetable", "b.table": "Classic",
        "b.position": 0.57, "b.mode": "Bend", "b.warp": 0.1, "b.octave": -1, "b.fine": 20, "b.level": 0.8,
        "unison.voices": 2, "unison.detune": 10, "unison.spread": 0.3, "f1.type": "Ladder", "f1.cutoff": 700,
        "f1.resonance": 0.25, "drive.type": "Soft", "drive.amount": 0.4, "mod1.amount": 0.1, "lfo2.shape": "Sine",
        "lfo2.rate": 0.2, **mod(7, "LFO 2", "B · timbre 2", 0.25), "lfo1.shape": "Triangle", "lfo1.rate": 0.13,
        **mod(8, "LFO 1", "A · timbre 1", 0.1), "amp.attack": 0.004, "amp.sustain": 1.0, "amp.release": 0.15,
    },
    # ---- Leads --------------------------------------------------------------------------------
    "Synthetic Clarinet Lead": {
        # Odd partials only (a clarinet's hollow tone) over a quiet saw and breath; the wheel fills in the even ones,
        # the pressure brightens it.
        "voice.mode": "Legato", "voice.glide": 0.06, "a.type": "Harmonic", "a.partials": 16, "a.bright": 0.1,
        "a.parity": -0.85, "b.type": "Analog", "b.wave": 0.67, "b.fine": 7, "b.level": 0.3, "f1.type": "Low-pass",
        "f1.cutoff": 3_500, "f1.resonance": 0.15, "noise.colour": "Breath", "noise.level": 0.06, "env2.attack": 0.03,
        "env2.decay": 0.5, "env2.sustain": 0.5, "mod1.amount": 0.2, "lfo1.shape": "Sine", "lfo1.rate": 5.3,
        **mod(7, "LFO 1", "Pitch", 0.004), **mod(8, "Mod wheel", "A · timbre 2", 1.0), **mod(2, "Pressure", "A · timbre 1", 0.4),
        "amp.attack": 0.04, "amp.decay": 0.5, "amp.sustain": 0.95, "amp.release": 0.2,
    },
    "Drawn Function Sync Lead": {
        # A synced table whose warp follows a drawn function played once (rise, fall, settle): an envelope in four points.
        "voice.mode": "Legato", "voice.glide": 0.05, "a.type": "Wavetable", "a.table": "Sync", "a.position": 0.2,
        "a.mode": "Sync", "a.warp": 0.0, "b.type": "Analog", "b.wave": 0.67, "b.fine": -7, "b.level": 0.35,
        "unison.voices": 2, "unison.detune": 8, "unison.spread": 0.6, "fn.points": 4, "fn.sync": 0, "fn.rate": 0.8,
        "fn.loop": False, "fn.smooth": 1.0, **points([0.0, 1.0, 0.6, 0.4]), **mod(7, "Function", "A · timbre 2", 0.7),
        **mod(8, "Function", "A · timbre 1", 0.4), "lfo1.rate": 5.5, **mod(2, "LFO 1", "Pitch", 0.003),
        "f1.type": "Low-pass", "f1.cutoff": 5_000, "f1.resonance": 0.15, "mod1.amount": 0.1, "drive.type": "Hard",
        "drive.amount": 0.2, "amp.attack": 0.003, "amp.sustain": 0.9, "amp.release": 0.25,
    },
    "Hybrid Brass": {
        # An analog saw and twenty bright partials; envelope 2 swells the ladder, the velocity and pressure brighten.
        "voice.count": 8, "a.type": "Analog", "a.wave": 0.67, "b.type": "Harmonic", "b.partials": 20, "b.bright": 0.2,
        "b.fine": 5, "b.level": 0.5, "unison.voices": 2, "unison.detune": 8, "f1.type": "Ladder", "f1.cutoff": 600,
        "f1.resonance": 0.15, "env2.attack": 0.08, "env2.decay": 0.6, "env2.sustain": 0.4, "mod1.amount": 0.45,
        **mod(7, "Velocity", "Filters · cutoff", 0.15), **mod(8, "Pressure", "B · timbre 1", 0.4), "lfo1.rate": 5.0,
        **mod(2, "LFO 1", "Pitch", 0.002), "amp.attack": 0.04, "amp.decay": 0.8, "amp.sustain": 0.85, "amp.release": 0.35,
    },
    "Chattering Robot Voice": {
        # Thirty-two partials through a vowel filter that LFO 2 and a quarter-note random sequence keep moving.
        "voice.count": 8, "a.type": "Harmonic", "a.partials": 32, "a.bright": 0.3, **SILENT_B, "noise.colour": "Breath",
        "noise.level": 0.15, "noise.send": 1.0, "f1.type": "Vowel", "f1.cutoff": 300, "f1.resonance": 0.6,
        "f2.type": "Band-pass", "f2.cutoff": 2_500, "f2.resonance": 0.3, "filter.routing": "Parallel", "mod1.amount": 0.0,
        "lfo2.shape": "Triangle", "lfo2.rate": 0.25, **mod(7, "LFO 2", "Filter 1 · cutoff", 0.25), "turing.sync": 9,
        "turing.change": 0.3, "turing.length": 6, **mod(8, "Turing", "Filter 1 · cutoff", 0.15), "amp.attack": 0.05,
        "amp.sustain": 0.95, "amp.release": 0.4,
    },
    # ---- Keys, mallets, percussion ------------------------------------------------------------
    "Hybrid Electric Piano": {
        # A struck metal bar for the tine, six soft partials for the body; the velocity brings out the tine, LFO 2 pans.
        "voice.count": 12, "a.type": "Modal", "a.material": "Metal", "a.exciter": "Mallet", "a.tone": 0.45,
        "a.strike": 0.15, "a.decay": 2.5, "a.level": 0.7, "b.type": "Harmonic", "b.partials": 6, "b.bright": -0.5,
        "b.parity": -0.3, "b.fine": 0, "b.level": 0.9, "f1.type": "Low-pass", "f1.cutoff": 3_500, "f1.resonance": 0.05,
        "filter.key": 0.5, "filter.velocity": 0.5, "env2.decay": 0.8, "env2.sustain": 0.1, "mod1.amount": 0.3,
        **mod(7, "Velocity", "A · level", 0.3), "lfo2.shape": "Sine", "lfo2.rate": 4.5, **mod(8, "LFO 2", "Pan", 0.3),
        "amp.attack": 0.001, "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 0.5, "amp.velocity": 0.6,
    },
    "Metal Kalimba": {
        # A small metal tine struck halfway, a short wooden knock with it; harder playing, brighter tine.
        "a.type": "Modal", "a.material": "Metal", "a.exciter": "Mallet", "a.tone": 0.35, "a.strike": 0.5, "a.decay": 1.1,
        "b.type": "Modal", "b.material": "Wood", "b.exciter": "Mallet", "b.tone": 0.6, "b.strike": 0.3, "b.decay": 0.4,
        "b.fine": 0, "b.level": 0.35, "f1.type": "Low-pass", "f1.cutoff": 8_000, "f1.resonance": 0.0, "mod1.amount": 0.0,
        **mod(7, "Velocity", "A · timbre 1", 0.3), "amp.attack": 0.001, "amp.decay": 2.0, "amp.sustain": 0.0,
        "amp.release": 0.8,
    },
    "Glass Harp": {
        # A string struck near its end by a hard mallet, a glass bowl an octave up ringing with it, two voices apart.
        "voice.count": 12, "a.type": "Modal", "a.material": "String", "a.exciter": "Mallet", "a.tone": 0.6, "a.strike": 0.12,
        "a.decay": 3.0, "b.type": "Modal", "b.material": "Glass", "b.exciter": "Mallet", "b.tone": 0.6, "b.decay": 2.5,
        "b.octave": 1, "b.fine": 0, "b.level": 0.3, "f1.type": "Low-pass", "f1.cutoff": 9_000, "mod1.amount": 0.0,
        **mod(7, "Velocity", "A · timbre 1", 0.3), **mod(8, "Key", "B · level", -0.2), "unison.voices": 2,
        "unison.detune": 4, "unison.spread": 1.0, "amp.decay": 4.0, "amp.sustain": 0.0, "amp.release": 2.0,
    },
    "Tube Chimes": {
        # A long metal bar struck near its end, stretched partials around it; softer and darker up the keyboard.
        "voice.count": 10, "a.type": "Modal", "a.material": "Metal", "a.exciter": "Mallet", "a.tone": 0.7, "a.strike": 0.1,
        "a.decay": 6.0, "b.type": "Harmonic", "b.partials": 12, "b.stretch": 0.6, "b.bright": -0.2, "b.fine": 0,
        "b.level": 0.2, "f1.type": "Low-pass", "f1.cutoff": 10_000, "f1.resonance": 0.0, "mod1.amount": 0.0,
        **mod(7, "Velocity", "A · timbre 1", 0.25), **mod(8, "Key", "A · timbre 1", -0.2), "amp.attack": 0.001,
        "amp.decay": 5.0, "amp.sustain": 0.0, "amp.release": 3.5,
    },
    "Island Steel Drum": {
        # A string-tuned resonator struck in the middle (harmonic), a metal bar an octave up; a slight pitch fall.
        "a.type": "Modal", "a.material": "String", "a.exciter": "Mallet", "a.tone": 0.45, "a.strike": 0.45, "a.decay": 1.0,
        "b.type": "Modal", "b.material": "Metal", "b.exciter": "Mallet", "b.tone": 0.5, "b.decay": 0.6, "b.octave": 1,
        "b.fine": 0, "b.level": 0.2, "f1.type": "Low-pass", "f1.cutoff": 6_000, "mod1.amount": 0.0, "env2.decay": 0.3,
        "env2.sustain": 0.0, **mod(7, "Env 2", "B · level", 0.3), **mod(8, "Env 2", "Pitch", 0.01), "amp.attack": 0.001,
        "amp.decay": 1.5, "amp.sustain": 0.0, "amp.release": 0.8,
    },
    "Tuned Skin Tom": {
        # A skin struck an octave down, its pitch falling in eighty milliseconds, a breath of pink noise on the hit.
        "voice.count": 8, "a.type": "Modal", "a.material": "Skin", "a.exciter": "Mallet", "a.tone": 0.3, "a.strike": 0.25,
        "a.decay": 0.5, "a.octave": -1, **SILENT_B, "noise.colour": "Pink", "noise.level": 0.0, "env2.attack": 0.001,
        "env2.decay": 0.08, "env2.sustain": 0.0, "mod1.amount": 0.0, **mod(7, "Env 2", "Pitch", 0.1),
        **mod(8, "Env 2", "Noise", 0.4), "f1.type": "Low-pass", "f1.cutoff": 5_000, "drive.type": "Soft",
        "drive.amount": 0.2, "amp.attack": 0.001, "amp.decay": 0.7, "amp.sustain": 0.0, "amp.release": 0.4,
    },
    "Hybrid Synthwave Pluck": {
        # An analog saw that envelope 2 rounds to a triangle as it fades, an octave square; three wide voices.
        "voice.count": 12, "a.type": "Analog", "a.wave": 0.67, "b.type": "Wavetable", "b.table": "Classic",
        "b.position": 0.857, "b.octave": 1, "b.fine": 0, "b.level": 0.3, "unison.voices": 3, "unison.detune": 14,
        "unison.spread": 1.0, "f1.type": "Ladder", "f1.cutoff": 450, "f1.resonance": 0.3, "env2.decay": 0.2,
        "env2.sustain": 0.0, "mod1.amount": 0.5, "filter.key": 0.6, **mod(7, "Env 2", "A · timbre 1", 0.3),
        **mod(8, "Velocity", "Filters · cutoff", 0.1), "amp.attack": 0.001, "amp.decay": 0.5, "amp.sustain": 0.0,
        "amp.release": 0.3,
    },
    # ---- Pads, sequences, effects -------------------------------------------------------------
    "Breathing Hybrid Pad": {
        # Mirrored voice tables beside a soft additive octave sent to a vowel filter; a slow drawn function moves both
        # filters, envelope 3 fades the additive layer in.
        "voice.count": 10, "a.type": "Wavetable", "a.table": "Voices", "a.position": 0.3, "a.mode": "Mirror",
        "a.warp": 0.2, "b.type": "Harmonic", "b.partials": 24, "b.bright": -0.3, "b.stretch": 0.1, "b.octave": 1,
        "b.fine": 5, "b.level": 0.4, "b.send": 0.7, "unison.voices": 3, "unison.detune": 12, "unison.spread": 1.0,
        "f1.type": "Low-pass", "f1.cutoff": 1_800, "f1.resonance": 0.2, "f2.type": "Vowel", "f2.cutoff": 500,
        "f2.resonance": 0.4, "filter.routing": "Parallel", "fn.points": 8, "fn.sync": 0, "fn.rate": 0.15, "fn.smooth": 1.0,
        **points([0.2, 0.6, 1.0, 0.7, 0.3, 0.5, 0.9, 0.4]), **mod(7, "Function", "Filter 1 · cutoff", 0.25),
        **mod(2, "Function", "Filter 2 · cutoff", 0.25), "lfo2.rate": 0.1, **mod(8, "LFO 2", "A · timbre 1", 0.3),
        "env3.attack": 3.0, "env3.decay": 3.0, "env3.sustain": 0.8, **mod(1, "Env 3", "B · level", 0.4),
        "amp.attack": 1.2, "amp.decay": 2.0, "amp.sustain": 0.9, "amp.release": 3.0,
    },
    "Singing Bowl": {
        # A metal bowl bowed as long as the key is held, struck glass an octave up, slowly circling the stereo.
        "voice.count": 8, "a.type": "Modal", "a.material": "Metal", "a.exciter": "Bow", "a.tone": 0.5, "a.strike": 0.3,
        "a.decay": 6.0, "b.type": "Modal", "b.material": "Glass", "b.exciter": "Mallet", "b.tone": 0.55, "b.decay": 4.0,
        "b.octave": 1, "b.fine": 0, "b.level": 0.3, "f1.type": "Low-pass", "f1.cutoff": 7_000, "mod1.amount": 0.0,
        "lfo1.shape": "Sine", "lfo1.rate": 0.3, **mod(7, "LFO 1", "Pan", 0.4), "lfo2.shape": "Sine", "lfo2.rate": 3.5,
        **mod(8, "LFO 2", "Volume", 0.12), "amp.attack": 0.05, "amp.decay": 2.0, "amp.sustain": 1.0, "amp.release": 3.5,
    },
    "Hybrid String Ensemble": {
        # Four unison saws for the section, a bowed string resonator for the grain; envelope 3 opens the filter slowly.
        "voice.count": 12, "a.type": "Analog", "a.wave": 0.67, "b.type": "Modal", "b.material": "String",
        "b.exciter": "Bow", "b.tone": 0.5, "b.strike": 0.2, "b.decay": 2.0, "b.fine": 0, "b.level": 0.45,
        "unison.voices": 4, "unison.detune": 14, "unison.spread": 1.0, "f1.type": "Low-pass", "f1.cutoff": 2_200,
        "f1.resonance": 0.1, "filter.key": 0.5, "mod1.amount": 0.0, "env3.attack": 1.5, "env3.decay": 2.0,
        "env3.sustain": 0.7, **mod(7, "Env 3", "Filters · cutoff", 0.2), "lfo1.rate": 5.0, **mod(8, "LFO 1", "Pitch", 0.002),
        **mod(2, "Mod wheel", "Filters · cutoff", 0.25), "amp.attack": 0.5, "amp.decay": 1.0, "amp.sustain": 0.9,
        "amp.release": 1.5,
    },
    "Dusty Lo-Fi Pad": {
        # Ten soft partials and a detuned triangle, slightly crushed, crackle, a slow wow and a random flutter.
        "voice.count": 10, "a.type": "Harmonic", "a.partials": 10, "a.bright": -0.4, "b.type": "Analog", "b.wave": 0.33,
        "b.fine": 12, "b.level": 0.4, "drive.type": "Crush", "drive.amount": 0.25, "noise.colour": "Crackle",
        "noise.level": 0.15, "lfo1.shape": "Sine", "lfo1.rate": 0.7, **mod(7, "LFO 1", "Pitch", 0.003),
        "lfo2.shape": "Random", "lfo2.rate": 8.0, **mod(8, "LFO 2", "Volume", 0.05), "f1.type": "Low-pass",
        "f1.cutoff": 2_200, "f1.resonance": 0.1, "mod1.amount": 0.0, "amp.attack": 0.3, "amp.decay": 1.5,
        "amp.sustain": 0.85, "amp.release": 1.5,
    },
    "Polyrhythmic Sequence": {
        # Twelve accents a bar (triplet eighths) from the drawn function against a sixteenth-note random sequence.
        "a.type": "Wavetable", "a.table": "Digital", "a.position": 0.4, "a.mode": "Steps", "a.warp": 0.2,
        "b.type": "Analog", "b.wave": 1.0, "b.octave": -1, "b.fine": 0, "b.level": 0.4, "f1.type": "Ladder",
        "f1.cutoff": 400, "f1.resonance": 0.5, "mod1.amount": 0.0, "fn.points": 12, "fn.sync": 12, "fn.smooth": 0.15,
        **points([1.0, 0.3, 0.6, 1.0, 0.2, 0.5, 1.0, 0.3, 0.8, 0.4, 1.0, 0.1]), **mod(7, "Function", "Filters · cutoff", 0.4),
        **mod(2, "Function", "Volume", 0.5), "turing.sync": 3, "turing.change": 0.25, "turing.length": 8,
        **mod(8, "Turing", "A · timbre 1", 0.4), "amp.sustain": 1.0, "amp.release": 0.2,
    },
    "Chopped Hybrid Riser": {
        # Envelope 3 over six seconds raises the pitch, the filter, the noise and the table; a square LFO chops it.
        "voice.count": 4, "a.type": "Wavetable", "a.table": "Teeth", "a.position": 0.2, "b.type": "Analog", "b.wave": 0.67,
        "b.fine": 25, "b.level": 0.5, "unison.voices": 4, "unison.detune": 30, "unison.spread": 1.0,
        "noise.colour": "White", "noise.level": 0.0, "env3.attack": 6.0, "env3.decay": 1.0, "env3.sustain": 1.0,
        "env3.release": 1.0, **mod(1, "Env 3", "Pitch", 0.4), **mod(7, "Env 3", "Filters · cutoff", 0.5),
        **mod(8, "Env 3", "Noise", 0.5), **mod(2, "Env 3", "A · timbre 1", 0.7), "lfo2.shape": "Square", "lfo2.sync": 3,
        **mod(3, "LFO 2", "Volume", -0.4), "f1.type": "Low-pass", "f1.cutoff": 250, "f1.resonance": 0.5,
        "amp.attack": 1.5, "amp.decay": 1.0, "amp.sustain": 1.0, "amp.release": 1.0,
    },
    "Metal and Skin Impact": {
        # A skin two octaves down hit by a noise burst, a metal bar with it, pink noise and a pitch fall on the hit.
        "voice.mode": "Mono", "a.type": "Modal", "a.material": "Skin", "a.exciter": "Burst", "a.tone": 0.2, "a.decay": 2.0,
        "a.octave": -2, "b.type": "Modal", "b.material": "Metal", "b.exciter": "Mallet", "b.tone": 0.4, "b.strike": 0.6,
        "b.decay": 3.0, "b.octave": -1, "b.fine": 0, "b.level": 0.5, "noise.colour": "Pink", "noise.level": 0.0,
        "env2.attack": 0.001, "env2.decay": 0.4, "env2.sustain": 0.0, "mod1.amount": 0.0, **mod(7, "Env 2", "Noise", 0.6),
        **mod(8, "Env 2", "Pitch", 0.15), "drive.type": "Soft", "drive.amount": 0.5, "f1.type": "Low-pass",
        "f1.cutoff": 4_000, "amp.attack": 0.001, "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 3.0,
    },
}


GRAINS_B = {"b.source": "Granular"}

NEBULEUSE = {
    "Init": {},
    "Celestial Choir": {
        "a.texture": "Choir", "a.grain": 0.25, "a.density": 20, "a.spray": 0.3, "a.harmonia": "Octave and fifth",
        "a.harmoniaLevel": 0.4, "b.table": "Classic", "b.position": 0.0, "b.octave": 1, "b.level": 0.2,
        "amp.attack": 0.8, "amp.release": 3.0,
    },
    "Glass Pad": {
        "a.texture": "Glass", "a.grain": 0.08, "a.density": 40, "a.scatter": 0.1, "a.harmonia": "Octave", "a.timbre": 0.4,
        "b.table": "Harmonic", "b.position": 0.5, "b.level": 0.3, "amp.attack": 0.6, "amp.release": 2.5,
    },
    "North Wind": {
        "a.texture": "Wind", "a.grain": 0.3, "a.density": 12, "a.travel": 0.3, **GRAINS_B, "b.texture": "Wind",
        "b.octave": -1, "b.level": 0.5, "f.cutoff": 3_000, "orb.motion": 0.6, "amp.attack": 1.0, "amp.release": 2.5,
    },
    "Distant Strings": {
        "a.texture": "Strings", "a.grain": 0.2, "a.density": 25, "a.spray": 0.15, "a.harmonia": "Fifth",
        "a.harmoniaLevel": 0.35, "b.table": "Classic", "b.position": 0.57, "b.level": 0.3, "b.cutoff": 2_000,
        "amp.attack": 1.2, "amp.release": 2.5,
    },
    "Carillon": {
        "a.texture": "Bells", "a.grain": 0.4, "a.density": 6, "a.spray": 0.6, "a.harmonia": "Major", "a.harmoniaLevel": 0.6,
        "b.level": 0.0, "amp.attack": 0.05, "amp.release": 3.0,
    },
    "Summer Rain": {
        "a.texture": "Rain", "a.grain": 0.05, "a.density": 60, "a.spray": 1.0, "a.travel": 0.5, **GRAINS_B,
        "b.texture": "Glass", "b.octave": 1, "b.level": 0.3, "b.density": 10, "b.grain": 0.03, "f.cutoff": 9_000,
        "amp.attack": 0.5, "amp.release": 2.0,
    },
    "Dark Drone": {
        "a.texture": "Drone", "a.octave": -1, "a.grain": 0.5, "a.density": 8, "a.timbre": -0.6, **GRAINS_B,
        "b.texture": "Drone", "b.octave": -2, "b.level": 0.6, "b.timbre": -0.6, "f.cutoff": 1_500, "f.resonance": 0.3,
        "orb.motion": 0.5, "orb.rate": 0.03, "amp.attack": 1.5, "amp.release": 3.0,
    },
    "Bright Drone": {
        "a.texture": "Drone", "a.harmonia": "Wide", "a.harmoniaLevel": 0.6, "a.timbre": 0.5, "b.table": "Voices",
        "b.position": 0.4, "b.octave": 1, "b.level": 0.25, "amp.attack": 1.0, "amp.release": 3.0,
    },
    "Reversed Grains": {
        "a.texture": "Choir", "a.reverse": 1.0, "a.grain": 0.3, "a.density": 15, **GRAINS_B, "b.texture": "Strings",
        "b.reverse": 0.5, "b.level": 0.5, "amp.attack": 0.7, "amp.release": 2.5,
    },
    "Chord Cloud": {
        "a.source": "Synthesis", "a.table": "Classic", "a.position": 0.3, "a.harmonia": "Seventh", "a.harmoniaLevel": 0.8,
        **GRAINS_B, "b.texture": "Choir", "b.harmonia": "Ninth", "b.harmoniaLevel": 0.6, "b.level": 0.5,
        "amp.attack": 0.5, "amp.release": 2.5,
    },
    "Nebula Bass": {
        "voice.mode": "Mono", "a.source": "Synthesis", "a.table": "Classic", "a.position": 0.57, "a.octave": -1,
        "a.level": 0.8, "a.cutoff": 900, **GRAINS_B, "b.texture": "Drone", "b.octave": -1, "b.level": 0.4,
        "amp.attack": 0.01, "amp.release": 0.3, "orb.motion": 0.2,
    },
    "Orbital Pad": {
        "a.texture": "Strings", **GRAINS_B, "b.texture": "Choir", "b.level": 0.6, "orb.motion": 1.0, "orb.rate": 0.15,
        "amp.attack": 0.6, "amp.release": 2.5,
    },
    "Swept Texture": {
        "a.texture": "Wind", "a.travel": 1.0, "b.level": 0.0, "f.type": "Band-pass", "f.cutoff": 1_200, "f.resonance": 0.5,
        "lfo1.rate": 0.1, **mod(6, "LFO 1", "Filter · cutoff", 0.5), "amp.attack": 0.8, "amp.release": 2.0,
    },
    "Vocal Breath": {
        "a.texture": "Choir", "b.level": 0.0, "f.type": "Vowel", "f.cutoff": 400, "f.resonance": 0.5, "orb.motion": 0.6,
        "amp.attack": 0.5, "amp.release": 2.0,
    },
    "Crystals": {
        "a.texture": "Glass", "a.grain": 0.02, "a.density": 80, "a.scatter": 0.4, "a.spray": 1.0, "a.timbre": 0.6,
        **GRAINS_B, "b.texture": "Bells", "b.level": 0.3, "amp.attack": 0.3, "amp.release": 2.0,
    },
    "Sea of Bells": {
        "a.texture": "Bells", "a.travel": 0.2, "a.density": 12, "a.grain": 0.5, "a.harmonia": "Octave", **GRAINS_B,
        "b.texture": "Bells", "b.octave": -1, "b.level": 0.4, "amp.attack": 0.4, "amp.release": 4.0,
    },
    "Airy Lead": {
        "voice.mode": "Legato", "voice.glide": 0.1, "a.source": "Synthesis", "a.table": "Voices", "a.position": 0.6,
        "a.level": 0.8, **GRAINS_B, "b.texture": "Choir", "b.level": 0.3, "lfo1.rate": 5.5,
        **mod(5, "LFO 1", "Pitch", 0.006), "amp.attack": 0.05, "amp.release": 0.6,
    },
    "Shadow": {
        "a.texture": "Drone", "a.timbre": -1.0, "a.cutoff": 600, **GRAINS_B, "b.texture": "Wind", "b.level": 0.3,
        "b.timbre": -0.5, "f.cutoff": 800, "amp.attack": 1.5, "amp.release": 3.0,
    },
    "Dawn": {
        "a.texture": "Choir", **GRAINS_B, "b.texture": "Strings", "b.level": 0.6, "f.cutoff": 600, "env2.attack": 4.0,
        "env2.sustain": 1.0, **mod(6, "Env 2", "Filter · cutoff", 0.5), "amp.attack": 1.5, "amp.release": 3.0,
    },
    "Pure Granular": {
        "a.texture": "Strings", "a.scatter": 0.6, "a.spray": 0.8, "a.density": 40, "a.grain": 0.03, "b.level": 0.0,
        "amp.attack": 0.4, "amp.release": 2.0,
    },
    "Ghost": {
        "a.texture": "Choir", "a.reverse": 0.7, "a.timbre": -0.3, "a.drift": 1.0, **GRAINS_B, "b.texture": "Wind",
        "b.level": 0.3, "b.drift": 1.0, "lfo2.rate": 0.2, **mod(6, "LFO 2", "Pan", 0.4), "amp.attack": 1.0,
        "amp.release": 3.0,
    },
    # ---- Pads and choirs ----------------------------------------------------------------------
    "Glittering Frost": {
        # Tiny glass grains thrown wide with their octave, a bright harmonic table high above, past a high-pass;
        # LFO 2 thickens and thins the grains, the wheel shortens them.
        "a.texture": "Glass", "a.grain": 0.06, "a.density": 50, "a.scatter": 0.15, "a.spray": 0.7, "a.travel": 0.4,
        "a.harmonia": "Octave", "a.harmoniaLevel": 0.5, "a.timbre": 0.3, "b.table": "Harmonic", "b.position": 0.8,
        "b.octave": 1, "b.level": 0.18, "b.drift": 0.4, "f.type": "High-pass", "f.cutoff": 250, "f.resonance": 0.1,
        "filter.key": 0.0, "lfo2.shape": "Sine", "lfo2.rate": 0.15, **mod(6, "LFO 2", "A · density", 0.3),
        **mod(7, "Velocity", "A · level", 0.2), **mod(8, "Mod wheel", "A · grain", -0.5), "orb.motion": 0.5,
        "orb.rate": 0.1, "amp.attack": 1.0, "amp.release": 3.0,
    },
    "Cathedral Choir": {
        # A choir an octave down with its minor chord, darkened, over a drone two octaves down; envelope 2 opens the
        # filter over four seconds, the wheel brings in more of the chord.
        "a.texture": "Choir", "a.octave": -1, "a.grain": 0.35, "a.density": 14, "a.spray": 0.25, "a.timbre": -0.4,
        "a.harmonia": "Minor", "a.harmoniaLevel": 0.4, "a.drift": 0.4, **GRAINS_B, "b.texture": "Drone", "b.octave": -2,
        "b.level": 0.35, "b.timbre": -0.5, "b.grain": 0.3, "f.cutoff": 1_800, "f.resonance": 0.2, "env2.attack": 4.0,
        "env2.decay": 4.0, "env2.sustain": 0.8, **mod(6, "Env 2", "Filter · cutoff", 0.3), **mod(7, "Mod wheel", "Harmonia", 0.5),
        "orb.motion": 0.4, "orb.rate": 0.04, "amp.attack": 1.5, "amp.release": 3.5,
    },
    "Awakening Strings": {
        # Strings grains with octave and fifth over a saw table; envelope 2 opens the filter and the chord over six seconds.
        "a.texture": "Strings", "a.grain": 0.2, "a.density": 30, "a.spray": 0.2, "a.harmonia": "Octave and fifth",
        "a.harmoniaLevel": 0.3, "b.table": "Classic", "b.position": 0.57, "b.level": 0.3, "b.fine": 7, "b.cutoff": 3_000,
        "f.cutoff": 450, "f.resonance": 0.15, "env2.attack": 6.0, "env2.decay": 5.0, "env2.sustain": 1.0,
        "env2.release": 3.0, **mod(6, "Env 2", "Filter · cutoff", 0.45), **mod(7, "Env 2", "Harmonia", 0.5),
        **mod(8, "Mod wheel", "B · level", 0.3), "amp.attack": 2.0, "amp.release": 3.5,
    },
    "Floating Ninth Chords": {
        # Two synthesis layers, each its own chord (a ninth, a sus 4 an octave up), both drifting; slow LFOs scan the tables.
        "a.source": "Synthesis", "a.table": "Classic", "a.position": 0.4, "a.harmonia": "Ninth", "a.harmoniaLevel": 0.7,
        "a.drift": 0.5, "a.cutoff": 4_000, "b.table": "Harmonic", "b.position": 0.5, "b.octave": 1, "b.harmonia": "Sus 4",
        "b.harmoniaLevel": 0.5, "b.level": 0.25, "b.drift": 0.5, "f.cutoff": 2_500, "f.resonance": 0.2,
        "lfo2.shape": "Sine", "lfo2.rate": 0.15, **mod(6, "LFO 2", "A · position", 0.2), "lfo1.shape": "Triangle",
        "lfo1.rate": 0.11, **mod(7, "LFO 1", "B · position", 0.25), **mod(8, "Velocity", "Filter · cutoff", 0.1),
        "amp.attack": 0.8, "amp.release": 2.5,
    },
    "Orbiting Vowels": {
        # A choir and a voice table through a vowel filter; the orb circles wide, moving the vowel and the table.
        "a.texture": "Choir", "a.grain": 0.2, "a.density": 22, "a.level": 1.0, "b.table": "Voices", "b.position": 0.5,
        "b.level": 0.6,
        "b.fine": 3, "f.type": "Vowel", "f.cutoff": 400, "f.resonance": 0.5, "orb.motion": 0.9, "orb.rate": 0.12,
        **mod(6, "Orb X", "B · position", 0.4), **mod(2, "Orb Y", "Filter · cutoff", 0.35),
        **mod(7, "Mod wheel", "Resonance", 0.3), "amp.attack": 0.6, "amp.release": 2.5,
    },
    "Northern Lights": {
        # A high choir and glass grains past a high-pass; LFO 2 sweeps the stereo, the orb moves the density.
        "a.texture": "Choir", "a.octave": 1, "a.grain": 0.15, "a.density": 30, "a.spray": 0.5, "a.timbre": 0.3, **GRAINS_B,
        "b.texture": "Glass", "b.level": 0.4, "b.travel": 0.3, "f.type": "High-pass", "f.cutoff": 300,
        "lfo2.shape": "Sine", "lfo2.rate": 0.1, **mod(6, "LFO 2", "Pan", 0.5), "orb.motion": 0.8, "orb.rate": 0.05,
        **mod(7, "Orb Y", "A · density", 0.4), "amp.attack": 1.5, "amp.release": 3.5,
    },
    "Hazy Synthwave Pad": {
        # A saw table with its octave, a slow LFO scanning it, strings grains in the haze; darker, drifting.
        "a.source": "Synthesis", "a.table": "Classic", "a.position": 0.57, "a.harmonia": "Octave", "a.harmoniaLevel": 0.4,
        "a.drift": 0.4, "a.cutoff": 3_000, **GRAINS_B, "b.texture": "Strings", "b.level": 0.45, "b.timbre": -0.3,
        "lfo2.shape": "Triangle", "lfo2.rate": 0.12, **mod(6, "LFO 2", "A · position", 0.15),
        **mod(7, "Mod wheel", "Filter · cutoff", 0.3), "f.cutoff": 2_200, "f.resonance": 0.2, "amp.attack": 0.7,
        "amp.release": 2.5,
    },
    "Rustling Lo-Fi Pad": {
        # Darkened strings grains with a seventh, a whisper of rain, a slow wow and a random flutter.
        "a.texture": "Strings", "a.timbre": -0.6, "a.cutoff": 3_000, "a.drift": 0.8, "a.grain": 0.25, "a.density": 20,
        "a.harmonia": "Seventh", "a.harmoniaLevel": 0.3, **GRAINS_B, "b.texture": "Rain", "b.level": 0.12,
        "b.timbre": -0.5, "lfo1.shape": "Sine", "lfo1.rate": 0.6, **mod(6, "LFO 1", "Pitch", 0.003),
        "lfo2.shape": "Random", "lfo2.rate": 6.0, **mod(7, "LFO 2", "Volume", 0.05), "f.cutoff": 2_500,
        "f.resonance": 0.15, "amp.attack": 0.5, "amp.release": 2.0,
    },
    "Time-Stretched Strings": {
        # Long strings grains crawling forward, long choir grains crawling back: a chord frozen in time.
        "a.texture": "Strings", "a.grain": 0.5, "a.density": 8, "a.travel": 0.05, "a.spray": 0.1, "a.scatter": 0.0,
        "a.harmonia": "Fifth", "a.harmoniaLevel": 0.3, **GRAINS_B, "b.texture": "Choir", "b.level": 0.3, "b.grain": 0.5,
        "b.density": 6, "b.travel": -0.05, "f.cutoff": 3_000, **mod(6, "Mod wheel", "A · grain", -0.6),
        "amp.attack": 1.2, "amp.release": 3.0,
    },
    # ---- Drones, textures, effects ------------------------------------------------------------
    "Cinematic Rumble": {
        # A drone two octaves down with its sub, wind beneath; a slow LFO sweeps the ladder, a random one the wind.
        "voice.count": 8, "a.texture": "Drone", "a.octave": -2, "a.grain": 0.4, "a.density": 10, "a.harmonia": "Sub",
        "a.harmoniaLevel": 0.5, "a.timbre": -0.3, **GRAINS_B, "b.texture": "Wind", "b.octave": -1, "b.level": 0.4,
        "b.travel": 0.3, "f.type": "Ladder", "f.cutoff": 500, "f.resonance": 0.35, "filter.key": 0.2,
        "lfo1.shape": "Sine", "lfo1.rate": 0.06, **mod(6, "LFO 1", "Filter · cutoff", 0.2), "lfo2.shape": "Random",
        "lfo2.rate": 0.5, **mod(7, "LFO 2", "B · level", 0.25), **mod(8, "Velocity", "Filter · cutoff", 0.15),
        "orb.motion": 0.2, "amp.attack": 2.0, "amp.release": 3.5,
    },
    "Singing Desert Wind": {
        # Wind grains through a band-pass a slow LFO sweeps, a faint voice table singing a fifth; the drift pans it.
        "a.texture": "Wind", "a.travel": 0.6, "a.grain": 0.25, "a.density": 20, "a.spray": 0.4, "b.table": "Voices",
        "b.position": 0.5, "b.level": 0.15, "b.fine": 0, "b.harmonia": "Fifth", "b.harmoniaLevel": 0.5, "b.drift": 0.6,
        "f.type": "Band-pass", "f.cutoff": 900, "f.resonance": 0.45, "lfo1.rate": 0.07,
        **mod(6, "LFO 1", "Filter · cutoff", 0.3), **mod(7, "Drift", "Pan", 0.5), **mod(8, "Drift", "B · position", 0.3),
        "amp.attack": 2.0, "amp.release": 3.0,
    },
    "Fifths Drone": {
        # A folded table an octave down with octave and fifth, a slow LFO folding it further, a drone beneath.
        "a.source": "Synthesis", "a.table": "Folded", "a.position": 0.3, "a.octave": -1, "a.harmonia": "Octave and fifth",
        "a.harmoniaLevel": 0.6, "a.drift": 0.6, "a.cutoff": 3_000, **GRAINS_B, "b.texture": "Drone", "b.level": 0.4,
        "b.octave": -1, "lfo1.shape": "Sine", "lfo1.rate": 0.05, **mod(6, "LFO 1", "A · position", 0.4), "lfo2.rate": 0.03,
        **mod(7, "LFO 2", "Filter · cutoff", 0.15), "f.cutoff": 2_000, "f.resonance": 0.25, "amp.attack": 2.5,
        "amp.release": 3.5,
    },
    "Abyss": {
        # A drone three octaves down, darkened, and low wind under a resonant low-pass that breathes very slowly.
        "a.texture": "Drone", "a.octave": -3, "a.grain": 0.4, "a.density": 10, "a.timbre": -0.8, **GRAINS_B,
        "b.texture": "Wind", "b.octave": -2, "b.level": 0.5, "b.timbre": -0.6, "f.type": "Low-pass", "f.cutoff": 400,
        "f.resonance": 0.5, "lfo1.rate": 0.03, **mod(6, "LFO 1", "Filter · cutoff", 0.2), "orb.motion": 0.5,
        "orb.rate": 0.02, "amp.attack": 2.5, "amp.release": 3.5,
    },
    "Distant Storm": {
        # Dense rain grains and low wind through a band-pass that a random LFO shakes; the drift swells the wind.
        "a.texture": "Rain", "a.grain": 0.05, "a.density": 70, "a.spray": 1.0, "a.travel": 0.5, **GRAINS_B,
        "b.texture": "Wind", "b.octave": -1, "b.level": 0.5, "b.timbre": -0.4, "f.type": "Band-pass", "f.cutoff": 1_500,
        "f.resonance": 0.3, "filter.key": 0.0, "lfo2.shape": "Random", "lfo2.rate": 2.0,
        **mod(6, "LFO 2", "Filter · cutoff", 0.2), **mod(7, "Drift", "B · level", 0.3), "lfo1.shape": "Sine",
        "lfo1.rate": 0.08, **mod(8, "LFO 1", "Pan", 0.4), "amp.attack": 1.0, "amp.release": 3.0,
    },
    "Wind Chimes": {
        # Sparse bell grains thrown everywhere, slightly out of tune, with a major chord; wind behind; the drift
        # makes them ring more or less often.
        "a.texture": "Bells", "a.grain": 0.15, "a.density": 5, "a.spray": 1.0, "a.scatter": 0.4, "a.harmonia": "Major",
        "a.harmoniaLevel": 0.7, "a.travel": 0.5, **GRAINS_B, "b.texture": "Wind", "b.level": 0.25, "f.cutoff": 7_000,
        **mod(6, "Drift", "A · density", 0.4), "amp.attack": 0.5, "amp.release": 3.0,
    },
    "Rewound Memories": {
        # Long strings grains played backwards with a major chord, bells reversed an octave up, a slow drift of pitch.
        "a.texture": "Strings", "a.reverse": 1.0, "a.grain": 0.45, "a.density": 6, "a.spray": 0.5, "a.timbre": -0.3,
        "a.harmonia": "Major", "a.harmoniaLevel": 0.5, **GRAINS_B, "b.texture": "Bells", "b.reverse": 0.8, "b.octave": 1,
        "b.level": 0.3, "b.grain": 0.35, "b.density": 5, "lfo2.shape": "Sine", "lfo2.rate": 0.12,
        **mod(6, "LFO 2", "B · level", 0.2), **mod(7, "Drift", "Pitch", 0.002), "f.cutoff": 3_500, "amp.attack": 1.0,
        "amp.release": 3.0,
    },
    "Pulsing Grains": {
        # Glass grains with their octave over a metal table, pulsed in sixteenths by a square LFO; a bar-long sweep.
        "a.texture": "Glass", "a.grain": 0.04, "a.density": 45, "a.harmonia": "Octave", "a.harmoniaLevel": 0.5,
        "b.table": "Metal", "b.position": 0.3, "b.level": 0.25, "lfo2.shape": "Square", "lfo2.sync": 3,
        **mod(6, "LFO 2", "Volume", -0.7), "lfo1.shape": "Saw", "lfo1.sync": 12, **mod(7, "LFO 1", "Filter · cutoff", 0.2),
        "f.cutoff": 4_000, "f.resonance": 0.25, "amp.attack": 0.05, "amp.release": 1.5,
    },
    "Celestial Riser": {
        # A choir with octave and fifth over wind; envelope 2 over seven seconds lifts the pitch an octave, opens the
        # filter and thickens the grains.
        "voice.count": 6, "a.texture": "Choir", "a.grain": 0.1, "a.density": 30, "a.spray": 0.6,
        "a.harmonia": "Octave and fifth", "a.harmoniaLevel": 0.6, "a.level": 1.0, **GRAINS_B, "b.texture": "Wind",
        "b.level": 0.6, "f.cutoff": 450, "f.resonance": 0.4, "filter.key": 0.0, "env2.attack": 7.0, "env2.decay": 1.0, "env2.sustain": 1.0,
        "env2.release": 1.5, **mod(6, "Env 2", "Pitch", 0.5), **mod(7, "Env 2", "Filter · cutoff", 0.6),
        **mod(8, "Env 2", "A · density", 0.6), "orb.motion": 0.6, "orb.rate": 0.3, "amp.attack": 2.5, "amp.decay": 1.0,
        "amp.sustain": 1.0, "amp.release": 1.5,
    },
    "Cosmic Impact": {
        # A drone and bells two octaves down struck at once; envelope 2 drops the pitch, closes the filter, thins the grains.
        "voice.mode": "Mono", "a.texture": "Drone", "a.octave": -2, "a.grain": 0.5, "a.density": 12, "a.harmonia": "Sub",
        "a.harmoniaLevel": 0.6, **GRAINS_B, "b.texture": "Bells", "b.octave": -2, "b.level": 0.6, "b.scan": 0.0,
        "b.travel": 0.0, "b.grain": 0.4, "b.density": 6, "f.type": "Low-pass", "f.cutoff": 300, "f.resonance": 0.2,
        "env2.attack": 0.001, "env2.decay": 1.5, "env2.sustain": 0.0, "env2.release": 1.0,
        **mod(6, "Env 2", "Filter · cutoff", 0.6), **mod(7, "Env 2", "Pitch", 0.2), **mod(8, "Env 2", "A · density", 0.5),
        "orb.motion": 0.0, "amp.attack": 0.001, "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 3.0,
    },
    # ---- Bass, lead, keys, pluck --------------------------------------------------------------
    "Grainy Bass": {
        # A triangle-ish table an octave down, drone grains under it; envelope 2 and the velocity snap the ladder,
        # the wheel brings up the grains.
        "voice.mode": "Mono", "voice.glide": 0.03, "a.source": "Synthesis", "a.table": "Classic", "a.position": 0.3,
        "a.octave": -1, "a.level": 0.8, "a.cutoff": 1_200, "a.resonance": 0.2, "a.drift": 0.05, **GRAINS_B,
        "b.texture": "Drone", "b.octave": -1, "b.level": 0.35, "b.grain": 0.08, "b.density": 40, "b.scan": 0.5,
        "b.travel": 0.0, "f.type": "Ladder", "f.cutoff": 600, "f.resonance": 0.3, "filter.key": 0.4, "env2.attack": 0.001,
        "env2.decay": 0.3, "env2.sustain": 0.1, "env2.release": 0.2, **mod(6, "Env 2", "Filter · cutoff", 0.35),
        **mod(7, "Velocity", "Filter · cutoff", 0.15), **mod(8, "Mod wheel", "B · level", 0.4), "orb.motion": 0.1,
        "amp.attack": 0.005, "amp.decay": 0.8, "amp.sustain": 0.8, "amp.release": 0.25,
    },
    "Singing Glass Lead": {
        # A harmonic table that glides, glass grains an octave up; a vibrato, the wheel brings up the glass.
        "voice.mode": "Legato", "voice.glide": 0.12, "a.source": "Synthesis", "a.table": "Harmonic", "a.position": 0.7,
        "a.level": 0.8, "a.drift": 0.1, **GRAINS_B, "b.texture": "Glass", "b.level": 0.25, "b.density": 30,
        "b.grain": 0.05, "b.octave": 1, "lfo1.rate": 5.0, **mod(6, "LFO 1", "Pitch", 0.005),
        **mod(7, "Mod wheel", "B · level", 0.4), **mod(8, "Pressure", "Filter · cutoff", 0.2), "f.cutoff": 5_000,
        "f.resonance": 0.15, "amp.attack": 0.04, "amp.decay": 1.0, "amp.sustain": 0.9, "amp.release": 0.8,
    },
    "Misty Bell Piano": {
        # Bell grains taken from the strikes, a sine an octave up that envelope 2 rings out on the attack.
        "a.texture": "Bells", "a.grain": 0.3, "a.density": 8, "a.scan": 0.0, "a.travel": 0.15, "a.spray": 0.05,
        "a.level": 0.6, "a.timbre": -0.2, "b.table": "Classic", "b.position": 0.0, "b.octave": 1, "b.fine": 0,
        "b.level": 0.35, "f.cutoff": 5_000, "f.resonance": 0.1, "env2.attack": 0.001, "env2.decay": 1.0,
        "env2.sustain": 0.0, **mod(6, "Env 2", "B · level", 0.4), **mod(7, "Velocity", "Filter · cutoff", 0.15),
        "orb.motion": 0.15, "amp.attack": 0.003, "amp.decay": 2.5, "amp.sustain": 0.2, "amp.release": 2.0,
        "amp.velocity": 0.6,
    },
    "Grainy Glass Pluck": {
        # Glass grains and a bright harmonic table, plucked: envelope 2 snaps the filter shut, the amp dies away.
        "a.texture": "Glass", "a.grain": 0.05, "a.density": 40, "a.spray": 0.3, "a.level": 0.6, "b.table": "Harmonic",
        "b.position": 0.9, "b.level": 0.45, "b.fine": 0, "f.type": "Low-pass", "f.cutoff": 1_500, "f.resonance": 0.25,
        "env2.attack": 0.001, "env2.decay": 0.3, "env2.sustain": 0.0, "env2.release": 0.3,
        **mod(6, "Env 2", "Filter · cutoff", 0.4), **mod(7, "Velocity", "Filter · cutoff", 0.1), "orb.motion": 0.2,
        "amp.attack": 0.002, "amp.decay": 0.6, "amp.sustain": 0.0, "amp.release": 0.6,
    },
}

MODELS = {"ambre": ("ambre-instrument", "ambre-synth", AMBRE),
          "colosse": ("colosse-instrument", "colosse-synth", COLOSSE),
          "mosaique": ("mosaique-instrument", "mosaique-synth", MOSAIQUE),
          "nebuleuse": ("nebuleuse-instrument", "nebuleuse-synth", NEBULEUSE),
          "opale": ("opale-instrument", "opale-synth", _presets("opale")),
          "onyx": ("onyx-instrument", "onyx-synth", _presets("onyx")),
          "tirette": ("tirette-instrument", "tirette-synth", _presets("tirette")),
          "alliage": ("alliage-instrument", "alliage-synth", _presets("alliage")),
          "quartz": ("quartz-instrument", "quartz-synth", _presets("quartz")),
          "argile": ("argile-instrument", "argile-synth", _presets("argile")),
          "saphir": ("saphir-instrument", "saphir-synth", _presets("saphir")),
          "derive": ("derive-instrument", "derive-synth", _presets("derive")),
          "bronze": ("bronze-instrument", "bronze-synth", _presets("bronze")),
          "silex": ("silex-instrument", "silex-synth", _presets("silex")),
          "jade": ("jade-instrument", "jade-synth", _presets("jade"))}


LEVEL_NOTES = {"silex": (36, [36, 38, 42, 49])}


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
    parser.add_argument("--only", nargs="*", default=None)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-synth-presets-"))
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
        for model, (instrument, type_key, sounds) in MODELS.items():
            if args.only and model not in args.only:
                continue
            request({"cmd": "new_project"})
            track = request({"cmd": "add_midi_track", "name": model, "instrument": instrument})["name"]
            # The note it is levelled on and the chord it is capped with (a
            # drum machine's: its kick, and a kick, snare, hat and crash).
            note, chord_notes = LEVEL_NOTES.get(model, (60, [48, 55, 60, 64]))
            for name, settings in sounds.items():
                answer = request({"cmd": "synth", "track": track, "init": True, "set": settings, "note": note,
                                  "seconds": 1.5, "tail": 6.0})
                if answer.get("problems"):
                    print("FAIL", name, answer["problems"], flush=True)
                    failures += 1
                    continue
                # Its volume: a held C4 near the same loudness, its peak under the cap.
                if name != "Init":
                    master = answer["values"]["voice.master"]
                    gain = min(RMS_DB - answer["rmsDb"], PEAK_CAP_DB - answer["peakDb"])
                    master = max(0.05, min(1.0, master * math.pow(10.0, gain / 20.0)))
                    answer = request({"cmd": "synth", "track": track, "set": {"voice.master": round(master, 3)},
                                      "note": note, "seconds": 1.5, "tail": 6.0})
                chord = request({"cmd": "synth", "track": track, "notes": chord_notes, "seconds": 1.5, "tail": 6.0})
                # A chord of four under its own cap too: struck sounds add up on their attacks, grains vary.
                if name != "Init" and chord["peakDb"] > CHORD_PEAK_CAP_DB:
                    master = answer["values"]["voice.master"]
                    master = max(0.05, master * math.pow(10.0, (CHORD_PEAK_CAP_DB - chord["peakDb"]) / 20.0))
                    answer = request({"cmd": "synth", "track": track, "set": {"voice.master": round(master, 3)},
                                      "note": note, "seconds": 1.5, "tail": 6.0})
                    chord = request({"cmd": "synth", "track": track, "notes": chord_notes, "seconds": 1.5,
                                     "tail": 6.0})
                ok = answer["rmsDb"] > -40.0 and chord["peakDb"] < 3.0 and answer["endDb"] < -60.0
                print(("ok   " if ok else "FAIL ") + f"{model} {name}: rms {answer['rmsDb']:.1f} dB, peak {answer['peakDb']:.1f} dB, "
                      f"chord peak {chord['peakDb']:.1f} dB, width {answer['sideDb']:.0f} dB, end {answer['endDb']:.0f} dB",
                      flush=True)
                failures += 0 if ok else 1
                request({"cmd": "presets", "track": track, "nodeId": answer["nodeId"], "action": "save", "name": name})
            target = PRESETS / type_key
            target.mkdir(parents=True, exist_ok=True)
            for old in target.glob("*.cypreset"):
                old.unlink()
            written = sorted((presets / "device" / type_key).glob("*.cypreset"))
            for file in written:
                # Without the date: the same sounds make the same files.
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
    raise SystemExit(main())
