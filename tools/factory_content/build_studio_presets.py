"""The factory presets of the studio effects (Réverbe Pro, Compresseur Pro,
Limiteur Pro, Filtre Pro), of the amp, cabinet and microphone simulators and of
the mixing effects (Pression, Fournaise, Écho Bande, Multibande Pro, Velours,
Patine, Sonomètre) of the suites (Tranche, Finition) and of Halo, written by CyDAW itself into
content/factory/Presets/device/<effect>.

For each preset: a fresh card of the effect on an audio track, its values
set through the control port, saved as a device preset. A silent session
(the speakers silenced) in a folder of its own; nothing is played.

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

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
TARGET = ROOT / "content" / "factory" / "Presets" / "device"
port = 47839

# Values by binding suffix; what is not given keeps its default.
PRESETS = {
    "reverb-pro": {
        "Petite pièce": {"revpro.space": 0.2, "revpro.decay": 0.6, "revpro.predelay": 4.0, "revpro.distance": 0.2,
                         "revpro.brightness": 0.1, "revpro.character": 0.25, "revpro.mix": 0.2, "revpro.lowDecay": 1.1,
                         "revpro.highDecay": 0.6, "revpro.early": 2.0},
        "Studio boisé": {"revpro.space": 0.35, "revpro.decay": 1.1, "revpro.predelay": 10.0, "revpro.distance": 0.3,
                         "revpro.brightness": -0.2, "revpro.character": 0.3, "revpro.mix": 0.22, "revpro.lowDecay": 1.2,
                         "revpro.highDecay": 0.45, "revpro.highCut": 9_000.0},
        "Plaque brillante": {"revpro.space": 0.45, "revpro.decay": 1.8, "revpro.predelay": 18.0, "revpro.distance": 0.55,
                             "revpro.brightness": 0.45, "revpro.character": 0.6, "revpro.diffusion": 1.0, "revpro.early": -24.0,
                             "revpro.mix": 0.25, "revpro.lowDecay": 0.8, "revpro.highDecay": 0.9, "revpro.lowCut": 200.0},
        "Grande salle": {"revpro.space": 0.8, "revpro.decay": 2.8, "revpro.predelay": 24.0, "revpro.distance": 0.45,
                         "revpro.character": 0.4, "revpro.mix": 0.28, "revpro.lowDecay": 1.35, "revpro.highDecay": 0.5},
        "Cathédrale": {"revpro.space": 1.0, "revpro.decay": 7.5, "revpro.predelay": 40.0, "revpro.distance": 0.7,
                       "revpro.brightness": -0.15, "revpro.character": 0.5, "revpro.mix": 0.32, "revpro.lowDecay": 1.4,
                       "revpro.highDecay": 0.4, "revpro.width": 1.3},
        "Voix en avant (ducking)": {"revpro.space": 0.5, "revpro.decay": 2.0, "revpro.sync": 6.0, "revpro.ducking": 10.0,
                                    "revpro.duckRelease": 350.0, "revpro.mix": 0.3, "revpro.lowCut": 250.0, "revpro.highCut": 11_000.0},
        "Nappe immense": {"revpro.space": 1.0, "revpro.decay": 15.0, "revpro.character": 0.8, "revpro.width": 1.5,
                          "revpro.mix": 0.45, "revpro.lowCut": 150.0, "revpro.distance": 0.8},
    },
    "comp-pro": {
        "Voix douce": {"comppro.style": 3.0, "comppro.threshold": -22.0, "comppro.ratio": 3.0, "comppro.knee": 8.0,
                       "comppro.attack": 8.0, "comppro.release": 140.0, "comppro.autoGain": 1.0, "comppro.sidechain": 80.0},
        "Batterie percutante": {"comppro.style": 6.0, "comppro.threshold": -20.0, "comppro.ratio": 4.0, "comppro.attack": 25.0,
                                "comppro.release": 90.0, "comppro.makeup": 3.0, "comppro.hold": 10.0},
        "Colle de bus": {"comppro.style": 5.0, "comppro.threshold": -16.0, "comppro.ratio": 2.0, "comppro.knee": 6.0,
                         "comppro.attack": 30.0, "comppro.release": 300.0, "comppro.autoRelease": 1.0, "comppro.autoGain": 1.0},
        "Compression parallèle": {"comppro.style": 1.0, "comppro.threshold": -35.0, "comppro.ratio": 8.0, "comppro.attack": 3.0,
                                  "comppro.release": 120.0, "comppro.makeup": 10.0, "comppro.mix": 0.4},
        "Opto chaleureux": {"comppro.style": 2.0, "comppro.threshold": -20.0, "comppro.ratio": 3.0, "comppro.attack": 15.0,
                            "comppro.release": 400.0, "comppro.autoGain": 1.0},
        "Mastering léger": {"comppro.style": 4.0, "comppro.threshold": -12.0, "comppro.ratio": 1.5, "comppro.knee": 12.0,
                            "comppro.attack": 40.0, "comppro.release": 500.0, "comppro.autoRelease": 1.0, "comppro.range": 4.0,
                            "comppro.lookahead": 2.0},
        "Pompage": {"comppro.style": 7.0, "comppro.threshold": -30.0, "comppro.ratio": 10.0, "comppro.attack": 1.0,
                    "comppro.release": 220.0, "comppro.hold": 20.0},
        "Basse tenue": {"comppro.threshold": -20.0, "comppro.ratio": 5.0, "comppro.attack": 5.0, "comppro.release": 180.0,
                        "comppro.sidechainLow": 2_000.0, "comppro.autoGain": 1.0},
    },
    "limiter-pro": {
        "Mastering transparent": {"limpro.style": 0.0, "limpro.gain": 4.0, "limpro.output": -1.0, "limpro.lookahead": 1.5,
                                  "limpro.release": 120.0, "limpro.truePeak": 1.0, "limpro.oversampling": 2.0},
        "Fort et moderne": {"limpro.style": 5.0, "limpro.gain": 8.0, "limpro.output": -0.8, "limpro.release": 60.0,
                            "limpro.truePeak": 1.0, "limpro.oversampling": 2.0},
        "Percutant": {"limpro.style": 1.0, "limpro.gain": 6.0, "limpro.output": -1.0, "limpro.release": 40.0},
        "Agressif": {"limpro.style": 4.0, "limpro.gain": 10.0, "limpro.output": -0.5, "limpro.release": 20.0,
                     "limpro.lookahead": 0.5},
        "Bus de batterie": {"limpro.style": 7.0, "limpro.gain": 5.0, "limpro.output": -3.0, "limpro.release": 80.0,
                            "limpro.linkTransients": 0.5},
        "Sécurité sans distorsion": {"limpro.style": 6.0, "limpro.output": -1.0, "limpro.lookahead": 5.0,
                                     "limpro.release": 200.0, "limpro.truePeak": 1.0, "limpro.oversampling": 3.0},
        # The target is the meters' mark: the gain brings the integrated
        # loudness (I) to it, by ear and eye, for each piece.
        "Streaming -14 LUFS": {"limpro.style": 2.0, "limpro.gain": 3.0, "limpro.output": -1.0, "limpro.target": -14.0,
                               "limpro.truePeak": 1.0},
        "CD 16 bits": {"limpro.style": 0.0, "limpro.gain": 5.0, "limpro.output": -0.3, "limpro.dither": 4.0,
                       "limpro.shaping": 2.0},
    },
    "filter-pro": {
        "Passe-bas doux": {"filtpro.f1Type": 0.0, "filtpro.f1Slope": 1.0, "filtpro.f1Cutoff": 1_500.0, "filtpro.f1Resonance": 0.15},
        "Wah automatique": {"filtpro.f1Type": 2.0, "filtpro.f1Slope": 0.0, "filtpro.f1Cutoff": 500.0, "filtpro.f1Resonance": 0.55,
                            "filtpro.m1Source": 3.0, "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.45,
                            "filtpro.followAttack": 5.0, "filtpro.followRelease": 120.0, "filtpro.followGain": 6.0},
        "Balayage lent": {"filtpro.f1Type": 0.0, "filtpro.f1Cutoff": 600.0, "filtpro.f1Resonance": 0.45, "filtpro.m1Source": 1.0,
                          "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.35, "filtpro.lfo1Shape": 1.0, "filtpro.lfo1Sync": 12.0},
        "Wobble 1/8": {"filtpro.f1Type": 0.0, "filtpro.f1Cutoff": 400.0, "filtpro.f1Resonance": 0.6, "filtpro.f1Drive": 6.0,
                       "filtpro.m1Source": 1.0, "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.5, "filtpro.lfo1Sync": 6.0},
        "Téléphone": {"filtpro.f1Type": 1.0, "filtpro.f1Cutoff": 400.0, "filtpro.f1Drive": 6.0, "filtpro.f2On": 1.0,
                      "filtpro.f2Type": 0.0, "filtpro.f2Cutoff": 3_200.0},
        "Voyelles": {"filtpro.f1Type": 4.0, "filtpro.f1Cutoff": 800.0, "filtpro.f1Resonance": 0.6, "filtpro.f2On": 1.0,
                     "filtpro.f2Type": 4.0, "filtpro.f2Cutoff": 1_200.0, "filtpro.f2Resonance": 0.6, "filtpro.f3On": 1.0,
                     "filtpro.f3Type": 4.0, "filtpro.f3Cutoff": 2_500.0, "filtpro.f3Resonance": 0.5, "filtpro.m1Source": 1.0,
                     "filtpro.m1Destination": 9.0, "filtpro.m1Amount": 0.12, "filtpro.lfo1Rate": 0.3, "filtpro.output": -6.0},
        "Peigne métallique": {"filtpro.f1Type": 5.0, "filtpro.f1Cutoff": 220.0, "filtpro.f1Resonance": 0.85, "filtpro.mix": 0.6},
        "Tourbillon stéréo": {"filtpro.f1Type": 0.0, "filtpro.f1Cutoff": 1_200.0, "filtpro.f1Resonance": 0.4, "filtpro.f1Spread": 1.0,
                              "filtpro.m1Source": 1.0, "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.3,
                              "filtpro.lfo1Rate": 0.5, "filtpro.lfo1Stereo": 0.5},
        "Hachures aléatoires": {"filtpro.f1Type": 2.0, "filtpro.f1Cutoff": 1_500.0, "filtpro.f1Resonance": 0.5,
                                "filtpro.m1Source": 1.0, "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.5,
                                "filtpro.lfo1Shape": 5.0, "filtpro.lfo1Sync": 3.0},
    },
    # Ampli: the model (0 Clean US, 1 Crunch UK, 2 Lead UK, 3 Modern, 4 Boutique,
    # 5 Chime, 6 vintage bass, 7 modern bass), the knobs 0..10, the pedal
    # (1 boost, 2 overdrive, 3 fuzz).
    "amp-sim": {
        "Clean brillant": {"amp.model": 0.0, "amp.gain": 3.0, "amp.bass": 5.0, "amp.middle": 4.0, "amp.treble": 6.5,
                           "amp.presence": 6.0, "amp.master": 6.0, "amp.bright": 1.0, "amp.depth": 4.0, "amp.sag": 0.2},
        "Clean jazz": {"amp.model": 0.0, "amp.gain": 2.0, "amp.bass": 6.0, "amp.middle": 5.0, "amp.treble": 3.5,
                       "amp.presence": 3.0, "amp.master": 5.0},
        "Chime funky": {"amp.model": 5.0, "amp.gain": 3.5, "amp.bass": 4.0, "amp.middle": 5.0, "amp.treble": 7.0,
                        "amp.presence": 6.0, "amp.master": 6.0, "amp.sag": 0.4},
        "Crunch rock": {"amp.model": 1.0, "amp.gain": 6.0, "amp.bass": 5.0, "amp.middle": 7.0, "amp.treble": 6.0,
                        "amp.presence": 6.0, "amp.master": 6.0, "amp.sag": 0.35},
        "Blues poussé": {"amp.model": 4.0, "amp.gain": 5.0, "amp.middle": 6.0, "amp.master": 7.0, "amp.pedal": 2.0,
                         "amp.pedalDrive": 0.35, "amp.pedalTone": 0.55, "amp.pedalLevel": 3.0, "amp.sag": 0.45},
        "Lead années 80": {"amp.model": 2.0, "amp.gain": 7.0, "amp.middle": 6.0, "amp.treble": 6.5, "amp.presence": 6.0,
                           "amp.pedal": 2.0, "amp.pedalDrive": 0.2, "amp.pedalTone": 0.6, "amp.pedalLevel": 6.0,
                           "amp.tight": 80.0, "amp.gate": -70.0},
        "Solo chantant": {"amp.model": 4.0, "amp.gain": 8.0, "amp.middle": 7.0, "amp.presence": 4.5, "amp.master": 6.0,
                          "amp.sag": 0.5},
        "Métal moderne": {"amp.model": 3.0, "amp.gain": 8.0, "amp.bass": 6.0, "amp.middle": 3.5, "amp.treble": 6.5,
                          "amp.presence": 7.0, "amp.pedal": 1.0, "amp.pedalDrive": 0.4, "amp.pedalTone": 0.6,
                          "amp.tight": 120.0, "amp.gate": -60.0, "amp.depth": 6.0},
        "Djent serré": {"amp.model": 3.0, "amp.gain": 7.0, "amp.middle": 5.5, "amp.treble": 7.0, "amp.presence": 7.0,
                        "amp.pedal": 2.0, "amp.pedalDrive": 0.1, "amp.pedalTone": 0.7, "amp.pedalLevel": 6.0,
                        "amp.tight": 160.0, "amp.gate": -55.0},
        "Fuzz vintage": {"amp.model": 1.0, "amp.gain": 3.0, "amp.bass": 6.0, "amp.master": 6.0, "amp.pedal": 3.0,
                         "amp.pedalDrive": 0.7, "amp.pedalTone": 0.45, "amp.pedalLevel": -4.0},
        "Basse ronde": {"amp.model": 6.0, "amp.gain": 3.0, "amp.bass": 6.0, "amp.treble": 4.0, "amp.presence": 4.0},
        "Basse grognante": {"amp.model": 6.0, "amp.gain": 7.0, "amp.bass": 6.0, "amp.middle": 6.0, "amp.master": 6.0,
                            "amp.sag": 0.4},
        "Basse moderne slap": {"amp.model": 7.0, "amp.gain": 2.0, "amp.bass": 7.0, "amp.middle": 3.0, "amp.treble": 7.0,
                               "amp.presence": 6.0},
        # Two channels: A the verse, B the chorus or the solo.
        "Clair et lead A-B": {"amp.model": 0.0, "amp.gain": 3.0, "amp.bass": 5.0, "amp.middle": 4.0, "amp.treble": 6.5,
                                "amp.presence": 6.0, "amp.master": 6.0, "amp.bright": 1.0, "amp.modelB": 2.0, "amp.gainB": 7.5,
                                "amp.middleB": 6.5, "amp.trebleB": 6.0, "amp.presenceB": 6.0, "amp.masterB": 6.0,
                                "amp.gate": -75.0},
        "Rythmique et solo A-B": {"amp.model": 1.0, "amp.gain": 5.5, "amp.middle": 6.0, "amp.treble": 6.0, "amp.modelB": 4.0,
                                    "amp.gainB": 8.0, "amp.middleB": 7.0, "amp.presenceB": 4.5, "amp.masterB": 6.5,
                                    "amp.sag": 0.4},
        "Métal couplet clair A-B": {"amp.model": 0.0, "amp.gain": 2.5, "amp.treble": 6.0, "amp.bright": 1.0,
                                       "amp.modelB": 3.0, "amp.gainB": 8.0, "amp.bassB": 6.0, "amp.middleB": 3.5,
                                       "amp.trebleB": 6.5, "amp.presenceB": 7.0, "amp.depthB": 6.0, "amp.tight": 100.0,
                                       "amp.gate": -60.0},
    },
    # Pression: depth, time, the amounts up and down (0..1), each band's gain (dB).
    "pression": {
        "Pression totale": {"pression.depth": 1.0},
        "Pression douce": {"pression.depth": 0.35, "pression.time": 0.6},
        "Basse épaisse": {"pression.depth": 0.6, "pression.lowGain": 3.0, "pression.highGain": -2.0, "pression.time": 0.65},
        "Voix en avant": {"pression.depth": 0.4, "pression.upward": 0.7, "pression.midGain": 2.0, "pression.lowGain": -2.0},
        "Batterie écrasée": {"pression.depth": 0.8, "pression.time": 0.3, "pression.highGain": 1.5},
        "Nappe lumineuse": {"pression.depth": 0.5, "pression.downward": 0.5, "pression.highGain": 3.0},
        "Seulement vers le haut": {"pression.depth": 0.6, "pression.downward": 0.0},
        "Seulement vers le bas": {"pression.depth": 0.7, "pression.upward": 0.0},
    },
    # Fournaise: the character (0 tape, 1 console, 2 class A, 3 valve,
    # 4 pentode), the drive 0..10, the tone -1..1.
    "fournaise": {
        "Bande chaude": {"fournaise.style": 0.0, "fournaise.drive": 4.0, "fournaise.tone": -0.15},
        "Console de mixage": {"fournaise.style": 1.0, "fournaise.drive": 3.0},
        "Classe A sur la voix": {"fournaise.style": 2.0, "fournaise.drive": 3.5, "fournaise.lowCut": 80.0},
        "Lampe mordante": {"fournaise.style": 3.0, "fournaise.drive": 6.0, "fournaise.tone": 0.2},
        "Pentode détruite": {"fournaise.style": 4.0, "fournaise.drive": 7.0, "fournaise.punish": 1.0, "fournaise.highCut": 9_000.0},
        "Batterie poussée": {"fournaise.style": 0.0, "fournaise.drive": 6.0, "fournaise.thump": 1.0, "fournaise.lowCut": 40.0},
        "Basse grasse": {"fournaise.style": 2.0, "fournaise.drive": 5.0, "fournaise.highCut": 6_000.0},
        "Lo-fi": {"fournaise.style": 4.0, "fournaise.drive": 5.0, "fournaise.lowCut": 300.0, "fournaise.highCut": 4_000.0,
                  "fournaise.steep": 1.0},
        "Saturation parallèle": {"fournaise.style": 3.0, "fournaise.drive": 8.0, "fournaise.mix": 0.35},
    },
    # Écho Bande: the mode (0..11: 0-3 heads alone, 4-10 heads and spring,
    # 11 the spring alone), the time (ms) or the sync (6 = 1/8, 9 = 1/4,
    # 10 = 1/4.), the intensity (past 1 it runs away).
    "echo-bande": {
        "Slapback": {"echoband.mode": 0.0, "echoband.time": 90.0, "echoband.intensity": 0.1, "echoband.reverb": 0.0,
                     "echoband.mix": 0.45, "echoband.wow": 0.15},
        "Écho dub": {"echoband.mode": 3.0, "echoband.time": 300.0, "echoband.intensity": 0.75, "echoband.bass": -3.0,
                     "echoband.treble": -4.0, "echoband.wow": 0.4, "echoband.age": 0.5},
        "Ressort de surf": {"echoband.mode": 11.0, "echoband.reverb": 0.85, "echoband.mix": 0.5},
        "Trois têtes": {"echoband.mode": 10.0, "echoband.time": 140.0, "echoband.intensity": 0.5, "echoband.reverb": 0.25},
        "Emballement": {"echoband.mode": 10.0, "echoband.time": 80.0, "echoband.intensity": 1.05, "echoband.mix": 0.5},
        "Noire pointée": {"echoband.mode": 4.0, "echoband.sync": 10.0, "echoband.intensity": 0.4, "echoband.reverb": 0.2},
        "Croches": {"echoband.mode": 0.0, "echoband.sync": 6.0, "echoband.intensity": 0.35, "echoband.reverb": 0.0},
        "Bande usée": {"echoband.mode": 4.0, "echoband.time": 200.0, "echoband.age": 0.9, "echoband.wow": 0.7,
                       "echoband.intensity": 0.5},
    },
    # Multibande Pro: each band's mode (0 compress, 1 expand, 2 raise, 3 burst),
    # threshold, ratio, range, times, knee; a band off is left as it is.
    "multiband-pro": {
        "Mastering doux": {"multiband.threshold1": -18.0, "multiband.band1Ratio": 1.5, "multiband.band1Range": 6.0, "multiband.band1Knee": 12.0, "multiband.band1Attack": 30.0, "multiband.band1Release": 200.0, "multiband.threshold2": -18.0, "multiband.band2Ratio": 1.5, "multiband.band2Range": 6.0, "multiband.band2Knee": 12.0, "multiband.band2Attack": 30.0, "multiband.band2Release": 200.0, "multiband.threshold3": -18.0, "multiband.band3Ratio": 1.5, "multiband.band3Range": 6.0, "multiband.band3Knee": 12.0, "multiband.band3Attack": 20.0, "multiband.band3Release": 150.0, "multiband.threshold4": -18.0, "multiband.band4Ratio": 1.5, "multiband.band4Range": 6.0, "multiband.band4Knee": 12.0, "multiband.band4Attack": 10.0, "multiband.band4Release": 120.0},
        "Basse tenue": {"multiband.threshold1": -24.0, "multiband.band1Ratio": 4.0, "multiband.band1Range": 12.0, "multiband.band1Attack": 20.0, "multiband.band1Release": 150.0, "multiband.band2On": 0.0, "multiband.band3On": 0.0, "multiband.band4On": 0.0},
        "Sifflantes": {"multiband.band1On": 0.0, "multiband.band2On": 0.0, "multiband.band3On": 0.0, "multiband.threshold4": -30.0, "multiband.band4Ratio": 6.0, "multiband.band4Range": 12.0, "multiband.band4Attack": 1.0, "multiband.band4Release": 60.0, "multiband.band4Knee": 3.0, "multiband.cross3": 5000.0},
        "Gate de batterie": {"multiband.band1Mode": 1.0, "multiband.threshold1": -40.0, "multiband.band1Ratio": 3.0, "multiband.band1Range": 24.0, "multiband.band1Attack": 1.0, "multiband.band1Release": 80.0, "multiband.band2Mode": 1.0, "multiband.threshold2": -40.0, "multiband.band2Ratio": 3.0, "multiband.band2Range": 24.0, "multiband.band2Attack": 1.0, "multiband.band2Release": 80.0, "multiband.band3Mode": 1.0, "multiband.threshold3": -42.0, "multiband.band3Ratio": 3.0, "multiband.band3Range": 24.0, "multiband.band3Attack": 1.0, "multiband.band3Release": 60.0, "multiband.band4Mode": 1.0, "multiband.threshold4": -44.0, "multiband.band4Ratio": 3.0, "multiband.band4Range": 24.0, "multiband.band4Attack": 1.0, "multiband.band4Release": 60.0},
        "Voix pleine": {"multiband.band1On": 0.0, "multiband.band2Mode": 2.0, "multiband.threshold2": -36.0, "multiband.band2Ratio": 2.0, "multiband.band2Range": 6.0, "multiband.threshold3": -24.0, "multiband.band3Ratio": 3.0, "multiband.band3Range": 9.0, "multiband.band4On": 0.0},
        "Attaques en avant": {"multiband.band1On": 0.0, "multiband.band2On": 0.0, "multiband.band3Mode": 3.0, "multiband.threshold3": -24.0, "multiband.band3Ratio": 1.5, "multiband.band3Range": 6.0, "multiband.band3Attack": 5.0, "multiband.band3Release": 80.0, "multiband.band4Mode": 3.0, "multiband.threshold4": -26.0, "multiband.band4Ratio": 1.5, "multiband.band4Range": 6.0, "multiband.band4Attack": 3.0, "multiband.band4Release": 60.0},
        "Bus de batterie": {"multiband.threshold1": -20.0, "multiband.band1Ratio": 3.0, "multiband.band1Range": 9.0, "multiband.threshold2": -24.0, "multiband.band2Ratio": 2.0, "multiband.band2Range": 6.0, "multiband.band3Mode": 3.0, "multiband.threshold3": -20.0, "multiband.band3Ratio": 1.3, "multiband.band3Range": 4.0, "multiband.band3Attack": 5.0, "multiband.threshold4": -26.0, "multiband.band4Ratio": 2.0, "multiband.band4Range": 6.0},
    },
    # Velours: the depth (dB), sharpness and selectivity (0..1), the zone (Hz).
    "velours": {
        "Voix douce": {"velours.depth": 5.0, "velours.sharpness": 0.4, "velours.focusLow": 300.0, "velours.focusHigh": 10_000.0},
        "Guitare acoustique": {"velours.depth": 6.0, "velours.focusLow": 150.0, "velours.focusHigh": 6_000.0},
        "Bus agressif": {"velours.depth": 4.0, "velours.sharpness": 0.3, "velours.selectivity": 0.3},
        "Sifflantes": {"velours.depth": 8.0, "velours.focusLow": 4_000.0, "velours.focusHigh": 12_000.0, "velours.sharpness": 0.7,
                       "velours.selectivity": 0.7, "velours.attack": 1.0},
        "Résonances graves": {"velours.depth": 8.0, "velours.focusLow": 60.0, "velours.focusHigh": 400.0, "velours.release": 150.0},
        "Dur et précis": {"velours.mode": 1.0, "velours.depth": 10.0, "velours.sharpness": 0.8, "velours.selectivity": 0.8},
    },
    # Patine: the modules' amounts (0..1), the noise (0 record, 1 tape, 2 mains)
    # and distortion (0 valve, 1 tape, 2 transistor) kinds.
    "patine": {
        "Vieux disque": {"patine.noise": 0.6, "patine.noiseType": 0.0, "patine.wobble": 0.3, "patine.distort": 0.15,
                         "patine.space": 0.1, "patine.magnetic": 0.1, "patine.tone": -0.3},
        "Cassette usée": {"patine.noise": 0.4, "patine.noiseType": 1.0, "patine.wobble": 0.6, "patine.wobbleRate": 0.6,
                          "patine.magnetic": 0.6, "patine.distort": 0.1, "patine.space": 0.0, "patine.tone": -0.2},
        "Radio AM": {"patine.noise": 0.3, "patine.noiseType": 1.0, "patine.distort": 0.4, "patine.distortType": 2.0,
                     "patine.digital": 0.2, "patine.wobble": 0.1, "patine.space": 0.0, "patine.tone": -0.5, "patine.width": 0.0},
        "Lo-fi hip-hop": {"patine.noise": 0.5, "patine.noiseType": 0.0, "patine.wobble": 0.35, "patine.digital": 0.25,
                          "patine.space": 0.2, "patine.magnetic": 0.3, "patine.tone": -0.25},
        "Bande chaude": {"patine.noise": 0.15, "patine.noiseType": 1.0, "patine.distort": 0.3, "patine.distortType": 1.0,
                         "patine.magnetic": 0.4, "patine.wobble": 0.15, "patine.space": 0.0},
        "Échantillonneur 12 bits": {"patine.noise": 0.0, "patine.wobble": 0.0, "patine.distort": 0.1, "patine.digital": 0.6,
                                    "patine.space": 0.0, "patine.magnetic": 0.0},
        "Ronflement secteur": {"patine.noise": 0.5, "patine.noiseType": 2.0, "patine.wobble": 0.0, "patine.distort": 0.1,
                               "patine.space": 0.0, "patine.magnetic": 0.0},
        "Juste un peu": {"patine.magnitude": 0.4},
    },
    # Sonomètre: the target (0 streaming, 1 Apple Music, 4 EBU R128, 6 CD and club).
    "sonometre": {
        "Streaming": {"sonometre.target": 0.0},
        "Apple Music": {"sonometre.target": 1.0},
        "Radio et télé EBU": {"sonometre.target": 4.0},
        "CD et club": {"sonometre.target": 6.0, "sonometre.truePeak": -0.3},
    },
    # Tranche: each stage on or off, the equaliser, compressor, transients,
    # exciter (0 valve, 1 tape, 2 transistor), width.
    "tranche": {
        "Voix lead": {"tranche.lowCut": 90.0, "tranche.bell1Hz": 300.0, "tranche.bell1Db": -2.5, "tranche.bell2Hz": 3_500.0,
                      "tranche.bell2Db": 2.0, "tranche.highShelfDb": 2.0, "tranche.compThreshold": -22.0, "tranche.compRatio": 3.5,
                      "tranche.compMakeup": 4.0, "tranche.exciterOn": 1.0, "tranche.excite": 0.2, "tranche.exciterHz": 4_000.0},
        "Choeurs en retrait": {"tranche.lowCut": 150.0, "tranche.bell1Hz": 2_500.0, "tranche.bell1Db": -2.0, "tranche.highShelfDb": 1.5,
                               "tranche.compThreshold": -26.0, "tranche.compRatio": 4.0, "tranche.width": 1.4},
        "Grosse caisse": {"tranche.gateOn": 1.0, "tranche.gateThreshold": -35.0, "tranche.lowShelfHz": 60.0, "tranche.lowShelfDb": 3.0,
                          "tranche.bell1Hz": 350.0, "tranche.bell1Db": -4.0, "tranche.bell2Hz": 4_000.0, "tranche.bell2Db": 3.0,
                          "tranche.transientOn": 1.0, "tranche.attack": 0.5, "tranche.compThreshold": -14.0},
        "Caisse claire": {"tranche.gateOn": 1.0, "tranche.gateThreshold": -32.0, "tranche.lowCut": 80.0, "tranche.bell1Hz": 200.0,
                          "tranche.bell1Db": 2.0, "tranche.bell2Hz": 5_000.0, "tranche.bell2Db": 2.5, "tranche.transientOn": 1.0,
                          "tranche.attack": 0.4, "tranche.sustain": -0.2, "tranche.compThreshold": -16.0},
        "Basse ronde": {"tranche.lowShelfHz": 80.0, "tranche.lowShelfDb": 2.0, "tranche.bell1Hz": 250.0, "tranche.bell1Db": -3.0,
                        "tranche.bell2Hz": 900.0, "tranche.bell2Db": 1.5, "tranche.highCut": 8_000.0, "tranche.compThreshold": -20.0,
                        "tranche.compRatio": 4.0, "tranche.compMakeup": 3.0, "tranche.width": 0.0},
        "Guitare électrique": {"tranche.lowCut": 100.0, "tranche.bell1Hz": 500.0, "tranche.bell1Db": -2.0, "tranche.bell2Hz": 2_500.0,
                               "tranche.bell2Db": 1.5, "tranche.highCut": 12_000.0, "tranche.compThreshold": -20.0, "tranche.compRatio": 2.5},
        "Guitare acoustique": {"tranche.lowCut": 80.0, "tranche.bell1Hz": 200.0, "tranche.bell1Db": -3.0, "tranche.highShelfHz": 10_000.0,
                               "tranche.highShelfDb": 2.5, "tranche.compThreshold": -22.0, "tranche.compRatio": 2.5, "tranche.exciterOn": 1.0,
                               "tranche.excite": 0.15, "tranche.exciterHz": 6_000.0},
        "Piano": {"tranche.lowCut": 40.0, "tranche.bell1Hz": 400.0, "tranche.bell1Db": -1.5, "tranche.highShelfDb": 1.5,
                  "tranche.compThreshold": -24.0, "tranche.compRatio": 2.0},
        "Synthé brillant": {"tranche.lowCut": 120.0, "tranche.highShelfDb": 2.5, "tranche.exciterOn": 1.0, "tranche.excite": 0.3,
                            "tranche.compThreshold": -18.0, "tranche.width": 1.3},
        "Nettoyage seul": {"tranche.compOn": 0.0, "tranche.lowCut": 60.0, "tranche.bell1Hz": 300.0, "tranche.bell1Db": -2.0},
    },
    # Finition: the maximizer's gain and ceiling, the amounts, the tone, the
    # target the assistant aims at (0 streaming, 1 Apple Music, 4 EBU R128, 6 CD and club).
    "finition": {
        "Streaming équilibré": {"finition.gain": 6.0, "finition.ceiling": -1.0, "finition.dynamics": 0.3, "finition.target": 0.0},
        "Pop brillante": {"finition.gain": 8.0, "finition.ceiling": -1.0, "finition.dynamics": 0.4, "finition.high": 1.5,
                          "finition.excite": 0.3, "finition.width": 1.15, "finition.target": 0.0},
        "Électro puissante": {"finition.gain": 10.0, "finition.ceiling": -0.5, "finition.dynamics": 0.5, "finition.low": 1.5,
                              "finition.lowWidth": 0.5, "finition.target": 6.0},
        "Acoustique naturelle": {"finition.gain": 3.0, "finition.dynamics": 0.15, "finition.excite": 0.05, "finition.width": 1.0,
                                 "finition.target": 1.0},
        "Hip-hop grave": {"finition.gain": 8.0, "finition.dynamics": 0.4, "finition.low": 2.0, "finition.lowWidth": 0.4,
                          "finition.mid": -1.0, "finition.target": 0.0},
        "Rock dense": {"finition.gain": 9.0, "finition.dynamics": 0.55, "finition.mid": 1.0, "finition.excite": 0.25, "finition.target": 0.0},
        "Podcast": {"finition.gain": 6.0, "finition.dynamics": 0.6, "finition.lowCut": 70.0, "finition.mid": 1.5, "finition.width": 0.8,
                    "finition.target": 3.0},
        "Radio et télé EBU": {"finition.gain": 0.0, "finition.dynamics": 0.3, "finition.ceiling": -1.0, "finition.target": 4.0},
        "Club fort": {"finition.gain": 12.0, "finition.ceiling": -0.3, "finition.dynamics": 0.6, "finition.low": 1.0,
                      "finition.excite": 0.3, "finition.target": 6.0},
    },
    # Halo: the algorithm (0 concert hall, 1 bright hall, 2 plate, 3 room,
    # 4 chamber, 5 random space, 6 chorus space, 7 ambience, 8 sanctuary,
    # 9 nonlinear), the colour (0 1970s, 1 1980s, 2 now), decay (s), size.
    "halo": {
        "Grande salle": {"halo.mode": 0.0, "halo.decay": 3.2, "halo.size": 0.75, "halo.predelay": 25.0, "halo.mix": 0.3},
        "Salle brillante des années 80": {"halo.mode": 1.0, "halo.colour": 1.0, "halo.decay": 2.6, "halo.size": 0.65,
                                          "halo.damping": 12_000.0, "halo.mix": 0.3},
        "Plaque vocale": {"halo.mode": 2.0, "halo.decay": 1.8, "halo.predelay": 40.0, "halo.lowCut": 200.0, "halo.mix": 0.25},
        "Plaque vintage": {"halo.mode": 2.0, "halo.colour": 0.0, "halo.decay": 2.4, "halo.predelay": 20.0, "halo.mix": 0.3},
        "Petite pièce": {"halo.mode": 3.0, "halo.decay": 0.7, "halo.size": 0.4, "halo.mix": 0.25},
        "Chambre d'écho": {"halo.mode": 4.0, "halo.decay": 1.6, "halo.size": 0.55, "halo.mix": 0.3},
        "Espace qui dérive": {"halo.mode": 5.0, "halo.decay": 6.0, "halo.size": 0.85, "halo.modDepth": 0.8, "halo.mix": 0.35},
        "Nappe chorus": {"halo.mode": 6.0, "halo.decay": 4.5, "halo.size": 0.8, "halo.modDepth": 0.9, "halo.modRate": 0.4, "halo.mix": 0.4},
        "Air autour": {"halo.mode": 7.0, "halo.decay": 0.5, "halo.size": 0.35, "halo.mix": 0.2},
        "Sanctuaire": {"halo.mode": 8.0, "halo.decay": 12.0, "halo.size": 1.0, "halo.predelay": 60.0, "halo.mix": 0.4,
                       "halo.bassMultiplier": 1.5},
        "Caisse claire des années 80": {"halo.mode": 9.0, "halo.colour": 1.0, "halo.decay": 0.45, "halo.size": 0.5, "halo.mix": 0.35},
        "Croche pointée": {"halo.mode": 0.0, "halo.decay": 2.0, "halo.sync": 8.0, "halo.mix": 0.3},
        "Espace infini": {"halo.mode": 5.0, "halo.decay": 20.0, "halo.size": 1.0, "halo.modDepth": 0.6, "halo.mix": 0.5,
                          "halo.damping": 4_000.0},
    },
    # Baffle et micro: the cabinet (0 1x12 open, 1 2x12 open, 2 2x12 closed,
    # 3 4x12 modern, 4 4x12 vintage, 5 1x10, 6 1x15 bass, 7 8x10 bass), the
    # microphones (0 dynamic, 1 large dynamic, 2 ribbon, 3 large condenser,
    # 4 small condenser, 5 vintage valve, 6 broadcast).
    "cab-sim": {
        "4x12 classique": {"cab.model": 3.0, "cab.micA": 0.0, "cab.positionA": 0.3, "cab.distanceA": 2.0, "cab.room": 0.05},
        "4x12 dynamique et ruban": {"cab.model": 3.0, "cab.micA": 0.0, "cab.positionA": 0.25, "cab.distanceA": 2.0,
                                    "cab.micBOn": 1.0, "cab.micB": 2.0, "cab.positionB": 0.5, "cab.distanceB": 6.0,
                                    "cab.blend": 0.4},
        "4x12 vintage chaud": {"cab.model": 4.0, "cab.micA": 2.0, "cab.positionA": 0.5, "cab.distanceA": 8.0, "cab.room": 0.15},
        "Combo 2x12 clean": {"cab.model": 1.0, "cab.micA": 3.0, "cab.positionA": 0.35, "cab.distanceA": 15.0, "cab.room": 0.2},
        "Combo ouvert et pièce": {"cab.model": 0.0, "cab.micA": 1.0, "cab.positionA": 0.3, "cab.distanceA": 10.0,
                                  "cab.room": 0.4},
        "Petit combo lo-fi": {"cab.model": 5.0, "cab.micA": 0.0, "cab.positionA": 0.6, "cab.distanceA": 5.0,
                              "cab.highCut": 6_000.0},
        "Basse 8x10": {"cab.model": 7.0, "cab.micA": 1.0, "cab.positionA": 0.2, "cab.distanceA": 3.0, "cab.micBOn": 1.0,
                       "cab.micB": 3.0, "cab.positionB": 0.4, "cab.distanceB": 30.0, "cab.blend": 0.3, "cab.lowCut": 30.0},
        "Basse 1x15 ronde": {"cab.model": 6.0, "cab.micA": 2.0, "cab.positionA": 0.4, "cab.distanceA": 10.0, "cab.room": 0.1},
        "Grande salle sombre": {"cab.model": 4.0, "cab.micA": 2.0, "cab.positionA": 0.4, "cab.distanceA": 40.0, "cab.room": 0.5,
                                "cab.roomSize": 0.85, "cab.roomTone": 2_500.0},
        "Cabine sèche": {"cab.model": 2.0, "cab.micA": 0.0, "cab.positionA": 0.3, "cab.distanceA": 3.0, "cab.room": 0.3,
                         "cab.roomSize": 0.0, "cab.roomTone": 3_000.0},
        "Deux micros déphasés": {"cab.model": 3.0, "cab.micA": 0.0, "cab.positionA": 0.3, "cab.distanceA": 2.0,
                                 "cab.micBOn": 1.0, "cab.micB": 0.0, "cab.positionB": 0.3, "cab.distanceB": 12.0,
                                 "cab.blend": 0.5, "cab.align": 0.0},
    },
    # Micro: the model (0 dynamic … 6 broadcast, 7 lavalier, 8 telephone,
    # 9 megaphone, 10 old radio, 11 walkie-talkie), the pattern (0 its own,
    # 1 omni, 2 cardioid, 3 hyper, 4 figure 8), the preamp (0 transistor,
    # 1 valve, 2 transformer).
    "mic-sim": {
        "Voix studio": {"micsim.model": 3.0, "micsim.distance": 15.0, "micsim.lowCut": 80.0},
        "Voix proche et chaude": {"micsim.model": 6.0, "micsim.distance": 3.0, "micsim.lowCut": 60.0, "micsim.drive": 4.0,
                                  "micsim.preamp": 1.0},
        "Voix de scène": {"micsim.model": 0.0, "micsim.distance": 2.0, "micsim.lowCut": 100.0},
        "Vintage à lampe": {"micsim.model": 5.0, "micsim.distance": 20.0, "micsim.preamp": 1.0, "micsim.drive": 6.0},
        "Ruban doux": {"micsim.model": 2.0, "micsim.distance": 25.0},
        "Podcast": {"micsim.model": 6.0, "micsim.distance": 5.0, "micsim.lowCut": 90.0, "micsim.highCut": 15_000.0},
        "Téléphone": {"micsim.model": 8.0},
        "Mégaphone": {"micsim.model": 9.0, "micsim.room": 0.2},
        "Radio ancienne": {"micsim.model": 10.0},
        "Talkie-walkie": {"micsim.model": 11.0},
        "Voix lointaine": {"micsim.model": 3.0, "micsim.distance": 150.0, "micsim.room": 0.6, "micsim.roomSize": 0.6},
        "Dans le hall": {"micsim.model": 4.0, "micsim.distance": 120.0, "micsim.room": 0.8, "micsim.roomSize": 1.0,
                         "micsim.roomTone": 8_000.0},
        "Derrière le micro": {"micsim.model": 3.0, "micsim.angle": 150.0, "micsim.pattern": 2.0},
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
        for effect, sounds in PRESETS.items():
            if only and effect not in only:
                continue
            for name, values in sounds.items():
                answer = request({"cmd": "insert_device", "track": "Effet", "key": effect})
                node = next((item["id"] for item in answer.get("result", {}).get("nodes", [])
                             if item.get("type") == effect or item.get("typeKey") == effect), None)
                if node is None:
                    print("FAIL", effect, name, answer)
                    failures += 1
                    continue
                for suffix, value in values.items():
                    result = request({"cmd": "set_parameter", "track": "Effet", "key": "node.%s.%s" % (node, suffix), "value": value})
                    if not result.get("ok"):
                        print("FAIL", effect, name, suffix, result.get("error"))
                        failures += 1
                saved = request({"cmd": "presets", "track": "Effet", "nodeId": node, "action": "save", "name": name})
                print(("ok   " if saved.get("ok") else "FAIL ") + effect + " / " + name, flush=True)
                failures += 0 if saved.get("ok") else 1
                request({"cmd": "remove_device", "track": "Effet", "nodeId": node})
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
