"""The synth family's factory sounds (Ambre and the models after it): each set
through CyDAW 2's control port on its card, measured offline (no sound out:
the synth command renders it in memory), its volume brought so that a held C4
sits near the same loudness, saved as a device preset and copied into
content/factory/Presets/device/<its type>.

    python tools/factory_content/build_synth_presets.py [--exe "<CyDAW 2.exe>"] [--port 47838] [--only ambre]

A sound is a few settings over the defaults ("Init" first): keys as in the
model's table ("osc1.wave", "filter.cutoff"); a choice by its English name
("Pulse", "Ladder").
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

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
PRESETS = ROOT / "content" / "factory" / "Presets" / "device"
port = 47838

# Where a held C4 lands (its loudness while held), and the most its peak may reach.
RMS_DB = -18.0
PEAK_CAP_DB = -3.0

AMBRE = {
    "Init": {},
    "Basse Moog": {
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Pulse", "osc2.octave": -1,
        "osc2.fine": 4, "osc2.level": 0.5, "osc3.wave": "Saw", "osc3.octave": -2, "osc3.level": 0.45,
        "filter.model": "Ladder", "filter.cutoff": 260, "filter.resonance": 0.25, "filter.env": 0.55, "filter.drive": 0.35,
        "fenv.attack": 0.001, "fenv.decay": 0.32, "fenv.sustain": 0.15, "amp.attack": 0.002, "amp.decay": 0.6,
        "amp.sustain": 0.85, "amp.release": 0.12, "drift": 0.25,
    },
    "Basse acide": {
        "voice.mode": "Legato", "voice.glide": 0.07, "osc1.wave": "Saw", "osc1.octave": -1, "osc2.level": 0.0,
        "filter.model": "Ladder", "filter.cutoff": 320, "filter.resonance": 0.82, "filter.env": 0.7, "filter.drive": 0.55,
        "filter.key": 0.3, "fenv.attack": 0.001, "fenv.decay": 0.18, "fenv.sustain": 0.0, "fenv.release": 0.1,
        "amp.attack": 0.001, "amp.decay": 0.4, "amp.sustain": 0.9, "amp.release": 0.06, "drift": 0.1,
    },
    "Basse ronde": {
        "voice.mode": "Mono", "osc1.wave": "Triangle", "osc1.octave": -1, "osc2.wave": "Sine", "osc2.octave": -2,
        "osc2.fine": 0, "osc2.level": 0.7, "filter.model": "Cascade", "filter.cutoff": 600, "filter.resonance": 0.1,
        "filter.env": 0.2, "amp.attack": 0.003, "amp.decay": 1.0, "amp.sustain": 0.8, "amp.release": 0.15, "drift": 0.15,
    },
    "Basse pulsée": {
        "voice.mode": "Mono", "osc1.wave": "Pulse", "osc1.octave": -1, "osc1.pw": 0.3, "osc2.wave": "Pulse",
        "osc2.octave": -1, "osc2.pw": 0.6, "osc2.fine": -6, "lfo1.pwm": 0.35, "lfo1.rate": 0.6, "lfo1.delay": 0.0,
        "filter.model": "Cascade", "filter.cutoff": 450, "filter.resonance": 0.35, "filter.env": 0.45,
        "fenv.decay": 0.25, "fenv.sustain": 0.2, "amp.release": 0.1,
    },
    "Sub sinus": {
        "voice.mode": "Mono", "osc1.wave": "Sine", "osc1.octave": -1, "osc2.level": 0.0, "filter.cutoff": 2_000,
        "filter.resonance": 0.0, "filter.env": 0.0, "filter.drive": 0.1, "amp.attack": 0.004, "amp.sustain": 1.0,
        "amp.release": 0.1, "drift": 0.0,
    },
    "Lead mono": {
        "voice.mode": "Legato", "voice.glide": 0.06, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 9,
        "osc3.wave": "Pulse", "osc3.octave": -1, "osc3.level": 0.35, "filter.model": "Ladder", "filter.cutoff": 2_400,
        "filter.resonance": 0.3, "filter.env": 0.35, "filter.drive": 0.4, "fenv.decay": 0.5, "fenv.sustain": 0.4,
        "amp.sustain": 0.9, "amp.release": 0.2, "lfo1.rate": 5.5, "lfo1.delay": 0.4, "mod.wheel": 0.6,
    },
    "Lead synchro": {
        "voice.mode": "Legato", "voice.glide": 0.04, "osc1.wave": "Saw", "osc1.level": 0.4, "osc2.wave": "Saw",
        "osc2.sync": 1, "osc2.semi": 7, "osc2.fine": 0, "osc2.level": 0.9, "filter.model": "Multimode",
        "filter.cutoff": 3_500, "filter.resonance": 0.2, "filter.env": 0.3, "fenv.decay": 0.8, "fenv.sustain": 0.5,
        "lfo1.pitch": 0.0, "amp.release": 0.18,
    },
    "Lead mordant": {
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.fine": -8, "filter.model": "Biting",
        "filter.mode": "Low-pass", "filter.cutoff": 1_500, "filter.resonance": 0.75, "filter.drive": 0.8,
        "filter.env": 0.4, "fenv.decay": 0.35, "fenv.sustain": 0.35, "amp.release": 0.15,
    },
    "Nappe chaude": {
        "voice.count": 12, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 11, "unison.voices": 2,
        "unison.detune": 14, "unison.spread": 0.8, "filter.model": "Cascade", "filter.cutoff": 1_100,
        "filter.resonance": 0.15, "filter.env": 0.2, "fenv.attack": 0.9, "fenv.decay": 2.0, "fenv.sustain": 0.5,
        "fenv.release": 1.6, "amp.attack": 0.7, "amp.decay": 1.5, "amp.sustain": 0.85, "amp.release": 1.6,
        "lfo2.rate": 0.25, "lfo2.cutoff": 0.15, "drift": 0.45,
    },
    "Nappe cordes": {
        "voice.count": 12, "osc1.wave": "Pulse", "osc1.pw": 0.45, "osc2.wave": "Pulse", "osc2.pw": 0.55,
        "osc2.fine": 8, "lfo1.pwm": 0.6, "lfo1.rate": 0.8, "lfo1.delay": 0.0, "filter.model": "Multimode",
        "filter.cutoff": 2_600, "filter.resonance": 0.1, "filter.env": 0.15, "fenv.attack": 0.4, "fenv.sustain": 0.6,
        "amp.attack": 0.45, "amp.sustain": 0.9, "amp.release": 1.2, "hpf": 120, "drift": 0.35,
    },
    "Nappe balayée": {
        "voice.count": 12, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.octave": 1, "osc2.fine": 6,
        "osc2.level": 0.4, "filter.model": "Ladder", "filter.cutoff": 500, "filter.resonance": 0.55, "filter.env": 0.0,
        "lfo2.shape": "Triangle", "lfo2.rate": 0.12, "lfo2.cutoff": 0.6, "amp.attack": 1.0, "amp.sustain": 0.9,
        "amp.release": 2.0, "unison.voices": 2, "unison.detune": 10,
    },
    "Cuivres": {
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 6, "filter.model": "Ladder",
        "filter.cutoff": 700, "filter.resonance": 0.12, "filter.env": 0.6, "filter.velocity": 0.6,
        "fenv.attack": 0.07, "fenv.decay": 0.5, "fenv.sustain": 0.45, "fenv.release": 0.3, "amp.attack": 0.03,
        "amp.decay": 0.6, "amp.sustain": 0.85, "amp.release": 0.25, "lfo1.rate": 5.0, "lfo1.delay": 0.5,
        "lfo1.pitch": 0.08,
    },
    "Pincé": {
        "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.fine": 5, "osc2.level": 0.5, "filter.model": "Ladder",
        "filter.cutoff": 450, "filter.resonance": 0.35, "filter.env": 0.75, "fenv.attack": 0.001, "fenv.decay": 0.22,
        "fenv.sustain": 0.0, "amp.attack": 0.001, "amp.decay": 0.45, "amp.sustain": 0.0, "amp.release": 0.3,
    },
    "Arpège résonant": {
        "osc1.wave": "Saw", "osc2.level": 0.0, "filter.model": "Multimode", "filter.cutoff": 600,
        "filter.resonance": 0.7, "filter.env": 0.6, "fenv.decay": 0.15, "fenv.sustain": 0.0, "amp.decay": 0.3,
        "amp.sustain": 0.0, "amp.release": 0.2, "lfo2.rate": 0.2, "lfo2.cutoff": 0.2,
    },
    "Clavier poly": {
        "voice.count": 12, "osc1.wave": "Saw", "osc2.wave": "Pulse", "osc2.pw": 0.35, "osc2.fine": 4,
        "osc2.level": 0.5, "filter.model": "Cascade", "filter.cutoff": 1_600, "filter.env": 0.35,
        "fenv.decay": 0.8, "fenv.sustain": 0.25, "amp.decay": 1.4, "amp.sustain": 0.35, "amp.release": 0.5,
    },
    "Orgue analogique": {
        "osc1.wave": "Pulse", "osc1.pw": 0.5, "osc1.level": 0.6, "osc2.wave": "Sine", "osc2.octave": 1,
        "osc2.fine": 0, "osc2.level": 0.5, "osc3.wave": "Sine", "osc3.octave": -1, "osc3.fine": 0, "osc3.level": 0.4,
        "filter.model": "Multimode", "filter.cutoff": 4_000, "filter.resonance": 0.0, "filter.env": 0.0,
        "amp.attack": 0.005, "amp.sustain": 1.0, "amp.release": 0.08, "lfo1.rate": 6.5, "lfo1.delay": 0.0,
        "lfo1.pitch": 0.05, "drift": 0.2,
    },
    "Cloche FM": {
        "osc1.wave": "Sine", "osc2.level": 0.0, "osc3.wave": "Sine", "osc3.octave": 1, "osc3.semi": 7,
        "osc3.fine": 0, "osc3.level": 0.0, "fm.amount": 0.55, "filter.model": "Multimode", "filter.cutoff": 9_000,
        "filter.resonance": 0.0, "filter.env": 0.0, "amp.attack": 0.001, "amp.decay": 2.5, "amp.sustain": 0.0,
        "amp.release": 2.0, "drift": 0.0,
    },
    "Vent": {
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.level": 1.0, "filter.model": "Multimode", "filter.mode": "Band-pass",
        "filter.cutoff": 900, "filter.resonance": 0.6, "filter.env": 0.0, "filter.key": 0.8, "lfo2.rate": 0.15,
        "lfo2.cutoff": 0.5, "amp.attack": 1.2, "amp.sustain": 1.0, "amp.release": 2.0,
    },
    "Unisson large": {
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 13, "unison.voices": 4,
        "unison.detune": 26, "unison.spread": 1.0, "filter.model": "Multimode", "filter.cutoff": 5_000,
        "filter.resonance": 0.1, "filter.env": 0.15, "amp.attack": 0.01, "amp.sustain": 0.9, "amp.release": 0.5,
        "hpf": 90,
    },
    "Wobble": {
        "voice.mode": "Mono", "osc1.wave": "Saw", "osc1.octave": -1, "osc2.wave": "Pulse", "osc2.octave": -1,
        "osc2.fine": -10, "filter.model": "Ladder", "filter.cutoff": 300, "filter.resonance": 0.6, "filter.drive": 0.6,
        "filter.env": 0.0, "lfo2.shape": "Sine", "lfo2.sync": 6, "lfo2.cutoff": 0.75, "amp.sustain": 1.0,
        "amp.release": 0.1,
    },
    "Dérive vintage": {
        "voice.count": 8, "osc1.wave": "Saw", "osc2.wave": "Saw", "osc2.fine": 3, "drift": 1.0,
        "filter.model": "Cascade", "filter.cutoff": 1_900, "filter.resonance": 0.2, "filter.env": 0.25,
        "amp.attack": 0.02, "amp.release": 0.6, "lfo2.rate": 0.3, "lfo2.amp": 0.1,
    },
}

SAW = 0.57  # Classic: the saw's frame


def mod(slot: int, source: str, destination: str, amount: float) -> dict:
    return {f"mod{slot}.source": source, f"mod{slot}.dest": destination, f"mod{slot}.amount": amount}


def steps(values: list[float]) -> dict:
    return {f"perf.step{index + 1}": value for index, value in enumerate(values)}


COLOSSE = {
    "Init": {},
    "Basse colosse": {
        "voice.mode": "Mono", "osc1.position": SAW, "osc2.position": 0.85, "osc2.octave": -1, "osc2.fine": 0,
        "osc2.level": 0.5, "sub.level": 0.6, "f1.cutoff": 380, "f1.resonance": 0.3, "drive.amount": 0.4,
        "env2.decay": 0.3, "env2.sustain": 0.1, **mod(1, "Env 2", "Filters · cutoff", 0.45), "amp.release": 0.12,
    },
    "Basse réducteur": {
        "voice.mode": "Mono", "osc1.table": "Teeth", "osc1.position": 0.3, "osc1.octave": -1, "osc2.table": "Teeth",
        "osc2.position": 0.3, "osc2.octave": -1, "osc2.fine": 12, "osc2.level": 0.8, "unison.voices": 2,
        "unison.detune": 18, "f1.type": "Low-pass", "f1.cutoff": 900, "f1.resonance": 0.25, "drive.type": "Crush",
        "drive.amount": 0.3, "drive.mix": 0.5, "lfo1.rate": 0.3, **mod(1, "LFO 1", "Osc 1 · position", 0.2),
        **mod(2, "Env 2", "Filter 1 · cutoff", 0.3), "amp.release": 0.1,
    },
    "Grondement": {
        "voice.mode": "Mono", "osc1.table": "Voices", "osc1.position": 0.2, "osc1.octave": -1, "osc1.mode": "Formant",
        "osc1.warp": 0.4, "osc2.level": 0.0, "sub.level": 0.4, "f1.type": "Vowel", "f1.cutoff": 400,
        "f1.resonance": 0.6, "lfo1.sync": 6, **mod(1, "LFO 1", "Osc 1 · warp", 0.5), **mod(2, "LFO 1", "Filter 1 · cutoff", 0.35),
        "drive.type": "Fold", "drive.amount": 0.5, "drive.mix": 0.7, "amp.release": 0.1,
    },
    "Wobble numérique": {
        "voice.mode": "Mono", "osc1.table": "Digital", "osc1.position": 0.4, "osc1.octave": -1, "osc2.level": 0.3,
        "osc2.octave": -1, "sub.level": 0.5, "f1.cutoff": 250, "f1.resonance": 0.7, "lfo1.shape": "Sine", "lfo1.sync": 9,
        **mod(1, "LFO 1", "Filters · cutoff", 0.6), "drive.amount": 0.5, "amp.release": 0.1,
    },
    "Lead tranchant": {
        "voice.mode": "Legato", "voice.glide": 0.05, "osc1.table": "Sync", "osc1.position": 0.6, "osc1.mode": "Sync",
        "osc1.warp": 0.3, "osc2.position": SAW, "osc2.fine": 7, "unison.voices": 3, "unison.detune": 12,
        "f1.type": "Low-pass", "f1.cutoff": 4_000, "f1.resonance": 0.15, **mod(1, "Env 2", "Osc 1 · warp", 0.5),
        "env2.decay": 0.8, "env2.sustain": 0.3, "amp.release": 0.2,
    },
    "Lead métal": {
        "voice.mode": "Legato", "voice.glide": 0.03, "osc1.table": "Metal", "osc1.position": 0.5, "pm.ratio": "2",
        "pm.osc1": 0.4, "osc2.position": SAW, "osc2.level": 0.3, "f1.cutoff": 3_000, "f1.resonance": 0.25,
        "drive.type": "Hard", "drive.amount": 0.3, **mod(1, "Env 2", "Phase modulation", 0.3), "amp.release": 0.2,
    },
    "Supersaw colosse": {
        "voice.count": 8, "osc1.position": SAW, "osc2.position": SAW, "osc2.fine": 10, "osc2.level": 0.7,
        "unison.voices": 8, "unison.detune": 25, "unison.spread": 1.0, "f1.type": "Low-pass", "f1.cutoff": 6_000,
        "f1.resonance": 0.1, **mod(1, "Env 2", "Filters · cutoff", 0.15), "amp.release": 0.6,
    },
    "Nappe mouvante": {
        "voice.count": 10, "osc1.table": "Harmonic", "osc1.position": 0.3, "osc2.table": "Voices", "osc2.position": 0.5,
        "osc2.level": 0.6, "unison.voices": 3, "unison.detune": 14, "f1.type": "Low-pass", "f1.cutoff": 1_800,
        "env3.attack": 2.0, "env3.decay": 3.0, "env3.sustain": 0.6, **mod(1, "Env 3", "Osc 1 · position", 0.6),
        "lfo2.rate": 0.1, **mod(2, "LFO 2", "Osc 2 · position", 0.3), "amp.attack": 0.8, "amp.sustain": 0.9,
        "amp.release": 2.0,
    },
    "Nappe voyelles": {
        "voice.count": 10, "osc1.position": SAW, "osc2.position": 0.85, "osc2.fine": 9, "unison.voices": 2,
        "f1.type": "Vowel", "f1.cutoff": 300, "f1.resonance": 0.5, "lfo2.rate": 0.12, "lfo2.shape": "Triangle",
        **mod(1, "LFO 2", "Filter 1 · cutoff", 0.45), "amp.attack": 0.6, "amp.sustain": 0.9, "amp.release": 1.5,
    },
    "Pincé harmonique": {
        "osc1.table": "Harmonic", "osc1.position": 0.85, "osc2.level": 0.0, "env2.decay": 0.25, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Osc 1 · position", -0.8), "f1.cutoff": 8_000, "amp.decay": 0.45, "amp.sustain": 0.0,
        "amp.release": 0.3,
    },
    "Cloche de phase": {
        "osc1.position": 0.0, "osc2.level": 0.0, "pm.ratio": "3", "pm.osc1": 0.6, "env2.decay": 1.5, "env2.sustain": 0.0,
        **mod(1, "Env 2", "Phase modulation", 0.4), "f1.cutoff": 12_000, "amp.decay": 2.5, "amp.sustain": 0.0,
        "amp.release": 1.5,
    },
    "Séquence filtrée": {
        "osc1.position": SAW, "osc2.position": 0.85, "f1.cutoff": 300, "f1.resonance": 0.6, "perf.steps": 16,
        "perf.sync": 3, **mod(1, "Sequence", "Filter 1 · cutoff", 0.6), "amp.sustain": 1.0, "amp.release": 0.2,
    },
    "Arpège à pas": {
        "osc1.table": "Sync", "osc1.position": 0.3, "osc2.level": 0.0, "f1.cutoff": 3_000, "perf.steps": 8, "perf.sync": 3,
        "perf.smooth": 0.0, **steps([0.0, 0.583, 1.0, 0.583, 0.25, 0.583, 1.0, 0.833]),
        **mod(1, "Sequence", "Pitch", 0.5), **mod(2, "Env 2", "Filters · cutoff", 0.2), "amp.release": 0.15,
    },
    "Corde peigne": {
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.colour": "White", "noise.level": 0.0, "f1.type": "Comb",
        "f1.cutoff": 261.63, "f1.resonance": 0.97, "filter.key": 1.0, "filter.velocity": 0.0, "env2.attack": 0.001,
        "env2.decay": 0.06, "env2.sustain": 0.0, **mod(1, "Env 2", "Noise", 1.0), "drive.amount": 0.6,
        "amp.attack": 0.001, "amp.sustain": 1.0, "amp.release": 0.4,
    },
    "Hurlement": {
        "voice.mode": "Mono", "osc1.position": SAW, "osc2.position": 0.85, "f1.type": "Scream", "f1.cutoff": 900,
        "f1.resonance": 0.7, "lfo1.rate": 3.0, **mod(1, "LFO 1", "Filter 1 · cutoff", 0.3), "drive.amount": 0.4,
        "amp.release": 0.2,
    },
    "Sub profond": {
        "voice.mode": "Mono", "osc1.position": 0.0, "osc1.octave": -1, "osc2.level": 0.0, "sub.level": 0.7,
        "f1.cutoff": 2_000, "mod1.amount": 0.0, "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Cuivres modernes": {
        "voice.count": 8, "osc1.position": SAW, "osc2.position": 0.7, "osc2.fine": 6, "f1.cutoff": 600,
        "f1.resonance": 0.15, "env2.attack": 0.06, "env2.decay": 0.5, "env2.sustain": 0.4,
        **mod(1, "Env 2", "Filters · cutoff", 0.55), "amp.attack": 0.03, "amp.release": 0.3,
    },
    "Texture numérique": {
        "osc1.table": "Digital", "osc1.position": 0.5, "osc1.mode": "Steps", "osc1.warp": 0.5, "lfo2.shape": "Random",
        "lfo2.rate": 6.0, **mod(1, "LFO 2", "Osc 1 · position", 0.4), "noise.colour": "Crackle", "noise.level": 0.3,
        "f1.type": "Low-pass", "f1.cutoff": 5_000, "amp.attack": 0.2, "amp.release": 0.8,
    },
    "Choeur formant": {
        "voice.count": 10, "osc1.table": "Voices", "osc1.position": 0.0, "osc2.table": "Voices", "osc2.position": 1.0,
        "osc2.fine": 8, "osc2.level": 0.7, "unison.voices": 4, "unison.detune": 10, "f1.type": "Low-pass",
        "f1.cutoff": 5_000, "env3.attack": 1.5, "env3.sustain": 1.0, **mod(1, "Env 3", "Osc 1 · position", 0.5),
        "amp.attack": 0.3, "amp.sustain": 0.9, "amp.release": 1.2,
    },
    "Redressé": {
        "osc1.position": SAW, "osc2.level": 0.0, "drive.type": "Rectify", "drive.amount": 0.7, "f1.type": "Band-pass",
        "f1.cutoff": 1_400, "f1.resonance": 0.4, **mod(1, "Env 2", "Filter 1 · cutoff", 0.3), "amp.release": 0.25,
    },
    "Basse pliée": {
        "voice.mode": "Mono", "osc1.table": "Folded", "osc1.position": 0.3, "osc1.octave": -1, "osc2.level": 0.0,
        "sub.level": 0.3, "env2.decay": 0.25, "env2.sustain": 0.1, **mod(1, "Env 2", "Osc 1 · position", 0.5),
        "f1.cutoff": 2_500, "amp.release": 0.1,
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
    "Cloches de verre": {
        "a.type": "Modal", "a.material": "Glass", "a.exciter": "Mallet", "a.tone": 0.8, "a.decay": 5.0, "b.type": "Harmonic",
        "b.partials": 6, "b.bright": 0.3, "b.octave": 1, "b.level": 0.15, "f1.cutoff": 14_000, "mod1.amount": 0.0,
        "amp.decay": 4.0, "amp.sustain": 0.0, "amp.release": 3.0,
    },
    "Corde frappée": {
        "a.type": "Modal", "a.material": "String", "a.exciter": "Mallet", "a.tone": 0.7, "a.strike": 0.15, "a.decay": 3.0,
        "b.type": "Analog", "b.level": 0.12, "f1.cutoff": 3_000, "mod1.amount": 0.2, "amp.sustain": 1.0, "amp.release": 0.8,
    },
    "Tambour de peau": {
        "a.type": "Modal", "a.material": "Skin", "a.exciter": "Mallet", "a.tone": 0.4, "a.decay": 0.6, "a.octave": -1,
        **SILENT_B, "f1.cutoff": 8_000, "mod1.amount": 0.0, "amp.decay": 0.8, "amp.sustain": 0.0, "amp.release": 0.4,
    },
    "Archet de verre": {
        "a.type": "Modal", "a.material": "Glass", "a.exciter": "Bow", "a.tone": 0.5, "a.decay": 3.0, **SILENT_B,
        "f1.cutoff": 9_000, "mod1.amount": 0.0, "amp.attack": 0.3, "amp.sustain": 1.0, "amp.release": 1.5,
    },
    "Violoncelle d'archet": {
        "voice.mode": "Legato", "voice.glide": 0.05, "a.type": "Modal", "a.material": "String", "a.exciter": "Bow",
        "a.tone": 0.45, "a.decay": 2.0, "a.octave": -1, **SILENT_B, "f1.cutoff": 5_000, "mod1.amount": 0.0,
        "lfo1.rate": 5.0, **mod(7, "LFO 1", "Pitch", 0.01), "amp.attack": 0.15, "amp.sustain": 1.0, "amp.release": 0.6,
    },
    "Orgue harmonique": {
        "a.type": "Harmonic", "a.partials": 8, "a.bright": 0.2, "a.parity": -0.6, "b.type": "Harmonic", "b.partials": 4,
        "b.octave": 1, "b.fine": 0, "b.level": 0.4, "f1.cutoff": 10_000, "mod1.amount": 0.0, "amp.attack": 0.01,
        "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Nappe additive": {
        "voice.count": 10, "a.type": "Harmonic", "a.partials": 32, "a.bright": -0.2, "a.stretch": 0.2, "b.table": "Voices",
        "b.position": 0.5, "b.level": 0.35, "env3.attack": 2.0, "env3.sustain": 0.8, **mod(7, "Env 3", "A · timbre 1", 0.6),
        "lfo2.rate": 0.15, **mod(8, "LFO 2", "B · timbre 1", 0.3), "f1.type": "Low-pass", "f1.cutoff": 6_000,
        "mod1.amount": 0.0, "unison.voices": 2, "amp.attack": 0.8, "amp.sustain": 0.9, "amp.release": 2.0,
    },
    "Spectre étiré": {
        "a.type": "Harmonic", "a.partials": 24, "a.bright": 0.1, "a.stretch": 0.8, **SILENT_B, "f1.cutoff": 12_000,
        "mod1.amount": 0.0, "env2.decay": 1.5, "env2.sustain": 0.0, **mod(7, "Env 2", "A · timbre 1", 0.5),
        "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 2.0,
    },
    "Analogique gras": {
        "a.type": "Analog", "a.wave": 0.67, "b.type": "Analog", "b.wave": 1.0, "b.octave": -1, "b.fine": 0, "b.level": 0.5,
        "f1.cutoff": 900, "f1.resonance": 0.3, **mod(1, "Env 2", "Filters · cutoff", 0.4), "amp.release": 0.25,
    },
    "Synchro hurlante": {
        "voice.mode": "Mono", "a.type": "Analog", "a.wave": 0.67, "a.sync": 19, **SILENT_B, "f1.type": "Scream",
        "f1.cutoff": 1_200, "f1.resonance": 0.6, "lfo1.rate": 2.0, **mod(1, "LFO 1", "Filter 1 · cutoff", 0.25),
        "drive.amount": 0.3, "amp.release": 0.2,
    },
    "Tables mouvantes": {
        "voice.count": 10, "a.type": "Wavetable", "a.table": "Metal", "a.position": 0.2, "lfo1.rate": 0.2,
        **mod(7, "LFO 1", "A · timbre 1", 0.4), "b.table": "Voices", "b.position": 0.6, "b.level": 0.6, "lfo2.rate": 0.13,
        **mod(8, "LFO 2", "B · timbre 1", 0.3), "unison.voices": 3, "unison.detune": 14, "f1.type": "Low-pass",
        "f1.cutoff": 4_000, "mod1.amount": 0.0, "amp.attack": 0.5, "amp.sustain": 0.9, "amp.release": 1.8,
    },
    "Turing arpège": {
        "a.type": "Analog", "a.wave": 0.67, **SILENT_B, "f1.cutoff": 400, "f1.resonance": 0.5, "turing.sync": 3,
        "turing.change": 0.15, **mod(1, "Turing", "Filters · cutoff", 0.5), "amp.decay": 0.25, "amp.sustain": 0.3,
        "amp.release": 0.2,
    },
    "Aléatoire mélodique": {
        "a.type": "Wavetable", "a.table": "Sync", "a.position": 0.3, **SILENT_B, "turing.sync": 5, "turing.change": 0.08,
        "turing.length": 8, **mod(7, "Turing", "Pitch", 0.5), "f1.cutoff": 3_500, "mod1.amount": 0.2, "amp.release": 0.2,
    },
    "Fonction pulsée": {
        "voice.count": 10, "a.type": "Analog", "a.wave": 0.67, "b.type": "Analog", "b.wave": 0.67, "b.fine": 9, "b.level": 0.7,
        "unison.voices": 2, "f1.type": "Low-pass", "f1.cutoff": 5_000, "mod1.amount": 0.0, "fn.points": 8, "fn.sync": 9,
        "fn.smooth": 0.05, **points([0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 1.0, 1.0]), **mod(7, "Function", "Volume", -1.0),
        "amp.sustain": 1.0, "amp.release": 0.3,
    },
    "Fonction filtrée": {
        "a.type": "Analog", "a.wave": 0.67, "b.level": 0.4, "f1.cutoff": 250, "f1.resonance": 0.55, "fn.sync": 12,
        "fn.smooth": 1.0, **mod(1, "Function", "Filters · cutoff", 0.6), "amp.sustain": 1.0, "amp.release": 0.4,
    },
    "Pincé hybride": {
        "a.type": "Wavetable", "a.table": "Harmonic", "a.position": 0.8, "env2.decay": 0.3, "env2.sustain": 0.0,
        **mod(7, "Env 2", "A · timbre 1", -0.7), "b.type": "Modal", "b.material": "String", "b.exciter": "Mallet",
        "b.tone": 0.6, "b.level": 0.5, "f1.cutoff": 6_000, "mod1.amount": 0.1, "amp.decay": 0.5, "amp.sustain": 0.0,
        "amp.release": 0.35,
    },
    "Basse hybride": {
        "voice.mode": "Mono", "a.type": "Analog", "a.wave": 0.67, "a.octave": -1, "b.type": "Harmonic", "b.partials": 2,
        "b.octave": -1, "b.fine": 0, "b.level": 0.6, "f1.cutoff": 300, "f1.resonance": 0.25,
        **mod(1, "Env 2", "Filters · cutoff", 0.5), "env2.decay": 0.3, "env2.sustain": 0.1, "amp.release": 0.12,
    },
    "Voix du vent": {
        "voice.count": 10, "a.type": "Harmonic", "a.partials": 12, "a.level": 0.3, "a.send": 1.0, **SILENT_B,
        "noise.colour": "Breath", "noise.level": 0.6, "noise.send": 1.0, "f2.type": "Vowel", "f2.cutoff": 400,
        "f2.resonance": 0.6, "lfo2.rate": 0.2, **mod(7, "LFO 2", "Filter 2 · cutoff", 0.3), "mod1.amount": 0.0,
        "amp.attack": 0.6, "amp.sustain": 1.0, "amp.release": 1.5,
    },
    "Macros en morphose": {
        "a.type": "Analog", "a.wave": 0.0, "b.type": "Wavetable", "b.table": "Classic", "b.position": 0.0, "b.level": 0.6,
        "macro1": 0.5, "macro2": 0.3, "f1.cutoff": 1_500, "amp.attack": 0.05, "amp.sustain": 0.9, "amp.release": 0.8,
    },
}


GRAINS_B = {"b.source": "Granular"}

NEBULEUSE = {
    "Init": {},
    "Chœur céleste": {
        "a.texture": "Choir", "a.grain": 0.25, "a.density": 20, "a.spray": 0.3, "a.harmonia": "Octave and fifth",
        "a.harmoniaLevel": 0.4, "b.table": "Classic", "b.position": 0.0, "b.octave": 1, "b.level": 0.2,
        "amp.attack": 0.8, "amp.release": 3.0,
    },
    "Nappe de verre": {
        "a.texture": "Glass", "a.grain": 0.08, "a.density": 40, "a.scatter": 0.1, "a.harmonia": "Octave", "a.timbre": 0.4,
        "b.table": "Harmonic", "b.position": 0.5, "b.level": 0.3, "amp.attack": 0.6, "amp.release": 2.5,
    },
    "Vent du nord": {
        "a.texture": "Wind", "a.grain": 0.3, "a.density": 12, "a.travel": 0.3, **GRAINS_B, "b.texture": "Wind",
        "b.octave": -1, "b.level": 0.5, "f.cutoff": 3_000, "orb.motion": 0.6, "amp.attack": 1.0, "amp.release": 2.5,
    },
    "Cordes lointaines": {
        "a.texture": "Strings", "a.grain": 0.2, "a.density": 25, "a.spray": 0.15, "a.harmonia": "Fifth",
        "a.harmoniaLevel": 0.35, "b.table": "Classic", "b.position": 0.57, "b.level": 0.3, "b.cutoff": 2_000,
        "amp.attack": 1.2, "amp.release": 2.5,
    },
    "Carillon": {
        "a.texture": "Bells", "a.grain": 0.4, "a.density": 6, "a.spray": 0.6, "a.harmonia": "Major", "a.harmoniaLevel": 0.6,
        "b.level": 0.0, "amp.attack": 0.05, "amp.release": 3.0,
    },
    "Pluie d'été": {
        "a.texture": "Rain", "a.grain": 0.05, "a.density": 60, "a.spray": 1.0, "a.travel": 0.5, **GRAINS_B,
        "b.texture": "Glass", "b.octave": 1, "b.level": 0.3, "b.density": 10, "b.grain": 0.03, "f.cutoff": 9_000,
        "amp.attack": 0.5, "amp.release": 2.0,
    },
    "Drone sombre": {
        "a.texture": "Drone", "a.octave": -1, "a.grain": 0.5, "a.density": 8, "a.timbre": -0.6, **GRAINS_B,
        "b.texture": "Drone", "b.octave": -2, "b.level": 0.6, "b.timbre": -0.6, "f.cutoff": 1_500, "f.resonance": 0.3,
        "orb.motion": 0.5, "orb.rate": 0.03, "amp.attack": 1.5, "amp.release": 3.0,
    },
    "Drone lumineux": {
        "a.texture": "Drone", "a.harmonia": "Wide", "a.harmoniaLevel": 0.6, "a.timbre": 0.5, "b.table": "Voices",
        "b.position": 0.4, "b.octave": 1, "b.level": 0.25, "amp.attack": 1.0, "amp.release": 3.0,
    },
    "Grains inversés": {
        "a.texture": "Choir", "a.reverse": 1.0, "a.grain": 0.3, "a.density": 15, **GRAINS_B, "b.texture": "Strings",
        "b.reverse": 0.5, "b.level": 0.5, "amp.attack": 0.7, "amp.release": 2.5,
    },
    "Nuage d'accords": {
        "a.source": "Synthesis", "a.table": "Classic", "a.position": 0.3, "a.harmonia": "Seventh", "a.harmoniaLevel": 0.8,
        **GRAINS_B, "b.texture": "Choir", "b.harmonia": "Ninth", "b.harmoniaLevel": 0.6, "b.level": 0.5,
        "amp.attack": 0.5, "amp.release": 2.5,
    },
    "Basse nébuleuse": {
        "voice.mode": "Mono", "a.source": "Synthesis", "a.table": "Classic", "a.position": 0.57, "a.octave": -1,
        "a.level": 0.8, "a.cutoff": 900, **GRAINS_B, "b.texture": "Drone", "b.octave": -1, "b.level": 0.4,
        "amp.attack": 0.01, "amp.release": 0.3, "orb.motion": 0.2,
    },
    "Pad orbital": {
        "a.texture": "Strings", **GRAINS_B, "b.texture": "Choir", "b.level": 0.6, "orb.motion": 1.0, "orb.rate": 0.15,
        "amp.attack": 0.6, "amp.release": 2.5,
    },
    "Texture balayée": {
        "a.texture": "Wind", "a.travel": 1.0, "b.level": 0.0, "f.type": "Band-pass", "f.cutoff": 1_200, "f.resonance": 0.5,
        "lfo1.rate": 0.1, **mod(6, "LFO 1", "Filter · cutoff", 0.5), "amp.attack": 0.8, "amp.release": 2.0,
    },
    "Souffle vocal": {
        "a.texture": "Choir", "b.level": 0.0, "f.type": "Vowel", "f.cutoff": 400, "f.resonance": 0.5, "orb.motion": 0.6,
        "amp.attack": 0.5, "amp.release": 2.0,
    },
    "Cristaux": {
        "a.texture": "Glass", "a.grain": 0.02, "a.density": 80, "a.scatter": 0.4, "a.spray": 1.0, "a.timbre": 0.6,
        **GRAINS_B, "b.texture": "Bells", "b.level": 0.3, "amp.attack": 0.3, "amp.release": 2.0,
    },
    "Mer de cloches": {
        "a.texture": "Bells", "a.travel": 0.2, "a.density": 12, "a.grain": 0.5, "a.harmonia": "Octave", **GRAINS_B,
        "b.texture": "Bells", "b.octave": -1, "b.level": 0.4, "amp.attack": 0.4, "amp.release": 4.0,
    },
    "Lead aérien": {
        "voice.mode": "Legato", "voice.glide": 0.1, "a.source": "Synthesis", "a.table": "Voices", "a.position": 0.6,
        "a.level": 0.8, **GRAINS_B, "b.texture": "Choir", "b.level": 0.3, "lfo1.rate": 5.5,
        **mod(5, "LFO 1", "Pitch", 0.006), "amp.attack": 0.05, "amp.release": 0.6,
    },
    "Ombre": {
        "a.texture": "Drone", "a.timbre": -1.0, "a.cutoff": 600, **GRAINS_B, "b.texture": "Wind", "b.level": 0.3,
        "b.timbre": -0.5, "f.cutoff": 800, "amp.attack": 1.5, "amp.release": 3.0,
    },
    "Aube": {
        "a.texture": "Choir", **GRAINS_B, "b.texture": "Strings", "b.level": 0.6, "f.cutoff": 600, "env2.attack": 4.0,
        "env2.sustain": 1.0, **mod(6, "Env 2", "Filter · cutoff", 0.5), "amp.attack": 1.5, "amp.release": 3.0,
    },
    "Granulaire pur": {
        "a.texture": "Strings", "a.scatter": 0.6, "a.spray": 0.8, "a.density": 40, "a.grain": 0.03, "b.level": 0.0,
        "amp.attack": 0.4, "amp.release": 2.0,
    },
    "Fantôme": {
        "a.texture": "Choir", "a.reverse": 0.7, "a.timbre": -0.3, "a.drift": 1.0, **GRAINS_B, "b.texture": "Wind",
        "b.level": 0.3, "b.drift": 1.0, "lfo2.rate": 0.2, **mod(6, "LFO 2", "Pan", 0.4), "amp.attack": 1.0,
        "amp.release": 3.0,
    },
}

MODELS = {"ambre": ("ambre-instrument", "ambre-synth", AMBRE),
          "colosse": ("colosse-instrument", "colosse-synth", COLOSSE),
          "mosaique": ("mosaique-instrument", "mosaique-synth", MOSAIQUE),
          "nebuleuse": ("nebuleuse-instrument", "nebuleuse-synth", NEBULEUSE)}


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
            for name, settings in sounds.items():
                answer = request({"cmd": "synth", "track": track, "init": True, "set": settings, "note": 60,
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
                                      "note": 60, "seconds": 1.5, "tail": 6.0})
                chord = request({"cmd": "synth", "track": track, "notes": [48, 55, 60, 64], "seconds": 1.5, "tail": 6.0})
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
