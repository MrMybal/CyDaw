"""CyDAW Electro: a bank of electronic drums CyDAW makes itself, and the kits that play it.

    python tools/factory_content/build_electro_banks.py [--out content/factory]

Nothing here is recorded or downloaded. Each sound is a recipe for DrumSynth.h (a kick for
Kick's engine, KickSynth.h): BankLists/cydaw-electro.json lists them, each file with the exact
size of the WAV CyDAW will write (16-bit, mono, 48 kHz: 44 + 2 bytes a frame) and its recipe as a
fourth element. banks.json names the bank with "generator": "cydaw-drums"; in CyDAW, Options >
Sound banks > Generate (or a double click on a kit) makes the files in the banks' folder.

The kits (Instruments/Electro/<kit>/<kit>.cysampler) map them the General MIDI way (C2 kick, D2
snare, F#2 closed hat…), one-shot, their hats in one choke group (the closed one cuts the open one);
the catalogue lists them under "Electronic drums". The sounds,
their recipes and the kits are CyDAW's own, released CC0.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

from build_factory_content import CATEGORIES, write_licence  # noqa: E402
from build_kick_presets import SOUNDS as KICK_PRESETS  # noqa: E402

BANK = "cydaw-electro"
RATE = 48_000
CC0 = "https://creativecommons.org/publicdomain/zero/1.0/"

# ---- The sounds ----------------------------------------------------------------------------------

KICK_CHOICES = {
    "bodyShape": ["Sine", "Triangle", "Round", "Hard"],
    "clickType": ["None", "Noise", "Click", "Blip"],
    "subOctave": ["Same", "-1 octave"],
    "driveType": ["Soft", "Hard", "Fold", "Tube"],
}
KICK_DEFAULTS = {"attack": 0.4, "hold": 30.0, "decay": 420.0, "length": 1_500.0, "subLevel": -60.0, "subDecay": 600.0}

# Kicks beyond Kick's presets.
EXTRA_KICKS = {
    "Electro Punch": {"pitchStart": 420, "pitchEnd": 56, "pitchTime": 40, "pitchCurve": 3.0, "hold": 15, "decay": 260,
                      "clickType": "Click", "clickLevel": -8, "clickDecay": 2, "driveType": "Hard", "drive": 4},
    "Psytrance": {"pitchStart": 1_500, "pitchEnd": 45, "pitchTime": 30, "pitchCurve": 2.0, "hold": 20, "decay": 230,
                  "decayCurve": 2.2, "clickType": "Click", "clickLevel": -6, "clickDecay": 2, "lowCut": 30},
    "Garage": {"pitchStart": 240, "pitchEnd": 58, "pitchTime": 50, "pitchCurve": 3.0, "hold": 10, "decay": 240,
               "clickType": "Noise", "clickLevel": -10, "clickDecay": 6, "clickTone": 6_000, "driveType": "Soft", "drive": 2},
    "Synthwave": {"bodyShape": "Triangle", "pitchStart": 280, "pitchEnd": 50, "pitchTime": 90, "pitchCurve": 3.0,
                  "hold": 30, "decay": 420, "clickType": "Noise", "clickLevel": -12, "clickDecay": 8, "highCut": 9_000},
    "Distorted Industrial": {"pitchStart": 700, "pitchEnd": 48, "pitchTime": 120, "pitchCurve": 5.0, "hold": 100,
                             "decay": 600, "driveType": "Fold", "drive": 20, "driveTone": 3_000, "clickType": "Noise",
                             "clickLevel": -4, "clickDecay": 10},
    "Soft Pillow": {"bodyShape": "Round", "pitchStart": 110, "pitchEnd": 55, "pitchTime": 60, "pitchCurve": 2.0,
                    "hold": 10, "decay": 300, "clickType": "None", "highCut": 2_500},
}


def kick_values(settings: dict) -> dict:
    values = {}
    for key, value in settings.items():
        if key == "output":
            continue  # a bank's sound is brought to -1 dBFS anyway
        if isinstance(value, str):
            value = KICK_CHOICES[key].index(value)
        values[key] = value
    return values


def kick_seconds(values: dict) -> float:
    """KickSynth.cpp's lengthSeconds, and 20 ms after."""
    get = lambda key: float(values.get(key, KICK_DEFAULTS.get(key, 0.0)))  # noqa: E731
    body = get("attack") + get("hold") + get("decay")
    if get("subLevel") > -59.5:
        body = max(body, get("subDecay"))
    return min(body, get("length")) / 1000.0 + 0.02


def seconds_of(kind: str, v: dict) -> float:
    """How long a sound is kept: its decay, and a little after."""
    get = lambda key, default: float(v.get(key, default))  # noqa: E731
    if kind == "snare":
        return max(get("toneDecay", 120), get("noiseDecay", 180)) / 1000 + 0.05
    if kind == "clap":
        return (get("spacing", 9) * get("bursts", 3) + get("decay", 220)) / 1000 + 0.05
    if kind in ("hat", "cymbal", "tambourine"):
        default = {"hat": 60, "cymbal": 1_600, "tambourine": 220}[kind]
        return max(0.12, get("attack", 0) / 1000 + get("decay", default) / 1000 + 0.05)
    if kind in ("tom", "conga"):
        return get("decay", 350 if kind == "tom" else 220) / 1000 + 0.05
    if kind == "rim":
        return max(0.12, get("decay", 22) / 1000 * 4)
    if kind == "cowbell":
        return get("decay", 300) / 1000 + 0.05
    if kind == "shaker":
        return (get("attack", 12) + get("decay", 90) + get("spacing", 35) * (get("shakes", 1) - 1)) / 1000 + 0.06
    if kind == "zap":
        return (get("time", 120) + get("decay", 160)) / 1000 + 0.05
    if kind == "impact":
        return max(get("decay", 1_500), get("noiseDecay", 300)) / 1000 + 0.05
    raise ValueError(kind)


SNARES = {
    "808 Snare": {"tone": 180, "tone2": 330, "toneDecay": 120, "noiseDecay": 160, "snappy": 0.55, "noiseTone": 5_500, "bend": 1.3},
    "909 Snare": {"tone": 190, "tone2": 340, "toneDecay": 90, "noiseDecay": 220, "snappy": 0.75, "noiseTone": 6_500,
                  "noiseCut": 1_500, "drive": 3},
    "Trap Snare": {"tone": 210, "toneDecay": 70, "noiseDecay": 260, "snappy": 0.8, "noiseTone": 4_500, "drive": 4},
    "Techno Snare": {"tone": 160, "toneDecay": 60, "noiseDecay": 140, "snappy": 0.7, "noiseTone": 3_500, "drive": 8},
    "House Snare": {"tone": 200, "toneDecay": 100, "noiseDecay": 200, "snappy": 0.6, "noiseTone": 5_000},
    "Lo-Fi Snare": {"tone": 170, "toneDecay": 120, "noiseDecay": 180, "snappy": 0.5, "noiseTone": 2_500, "noiseCut": 600},
    "DnB Snare": {"tone": 240, "tone2": 420, "toneDecay": 80, "noiseDecay": 240, "snappy": 0.8, "noiseTone": 7_000,
                  "drive": 6, "bend": 1.6},
    "Tight Snare": {"tone": 260, "toneDecay": 50, "noiseDecay": 90, "snappy": 0.65, "noiseTone": 8_000},
    "Fat Snare": {"tone": 150, "tone2": 260, "toneDecay": 160, "noiseDecay": 300, "snappy": 0.6, "noiseTone": 4_000, "drive": 5},
    "Electro Snare": {"tone": 220, "toneDecay": 60, "noiseDecay": 120, "snappy": 0.85, "noiseTone": 9_000, "noiseCut": 3_000},
}
CLAPS = {
    "808 Clap": {"spacing": 9, "bursts": 3, "decay": 220, "tone": 1_100, "q": 1.3},
    "909 Clap": {"spacing": 8, "bursts": 4, "decay": 260, "tone": 1_300, "q": 1.1},
    "House Clap": {"spacing": 11, "bursts": 3, "decay": 300, "tone": 1_500},
    "Trap Clap": {"spacing": 7, "bursts": 4, "decay": 200, "tone": 1_800, "q": 1.6},
    "Tight Clap": {"spacing": 5, "bursts": 2, "decay": 120, "tone": 2_000},
    "Big Clap": {"spacing": 13, "bursts": 5, "decay": 450, "tone": 1_000, "q": 0.9},
    "Snap": {"spacing": 3, "bursts": 1, "decay": 70, "tone": 2_600, "q": 2.0},
}
HATS = {
    "808 Closed Hat": {"decay": 50, "tone": 10_000, "cut": 7_000},
    "808 Open Hat": {"decay": 420},
    "808 Pedal Hat": {"decay": 90, "attack": 3},
    "909 Closed Hat": {"decay": 45, "noise": 0.5, "tone": 11_000},
    "909 Open Hat": {"decay": 380, "noise": 0.5},
    "Trap Closed Hat": {"decay": 30, "tone": 12_000, "cut": 8_000},
    "Trap Open Hat": {"decay": 300, "tone": 12_000},
    "Techno Hat": {"decay": 70, "tone": 8_000, "noise": 0.4, "cut": 6_000},
    "House Open Hat": {"decay": 260, "noise": 0.35, "tone": 9_500},
    "Lo-Fi Hat": {"decay": 60, "tone": 6_000, "cut": 4_000, "noise": 0.6},
    "Dark Hat": {"pitch": 0.8, "decay": 80, "tone": 6_500, "cut": 5_000},
    "Bright Hat": {"pitch": 1.25, "decay": 40, "tone": 13_000, "cut": 9_000},
}
CYMBALS = {
    "808 Cymbal": ("cymbal", {"decay": 1_600}),
    "Crash": ("cymbal", {"decay": 2_200, "noise": 0.6, "tone": 7_000, "cut": 3_000}),
    "Ride": ("cymbal", {"decay": 1_400, "noise": 0.15, "tone": 9_000, "pitch": 1.1, "cut": 5_000}),
    "Ride Bell": ("cymbal", {"decay": 900, "noise": 0.05, "tone": 6_000, "pitch": 1.5}),
    "Splash": ("cymbal", {"decay": 700, "pitch": 1.2, "noise": 0.5}),
    "China": ("cymbal", {"decay": 1_200, "pitch": 0.85, "noise": 0.7, "tone": 5_000}),
}
TOMS = {
    "808 Low Tom": {"start": 120, "end": 80, "time": 120, "decay": 500, "noise": 0.05},
    "808 Mid Tom": {"start": 170, "end": 115, "time": 120, "decay": 450, "noise": 0.05},
    "808 High Tom": {"start": 240, "end": 165, "time": 120, "decay": 400, "noise": 0.05},
    "909 Low Tom": {"start": 160, "end": 95, "time": 70, "decay": 350, "noise": 0.2},
    "909 Mid Tom": {"start": 220, "end": 135, "time": 70, "decay": 320, "noise": 0.2},
    "909 High Tom": {"start": 300, "end": 185, "time": 70, "decay": 280, "noise": 0.2},
    "Electro Tom": {"start": 400, "end": 90, "time": 150, "decay": 400, "noise": 0.05},
    "Syn Tom Drop": {"start": 600, "end": 60, "time": 300, "decay": 500, "noise": 0.0},
}
PERCUSSION = {
    "808 Rim": ("rim", {}),
    "909 Rim": ("rim", {"tone": 2_200, "tone2": 700, "decay": 18, "noise": 0.45}),
    "Claves": ("rim", {"tone": 2_500, "tone2": 2_500, "decay": 45, "noise": 0.02}),
    "Wood Block": ("rim", {"tone": 1_200, "tone2": 800, "decay": 40, "noise": 0.05}),
    "808 Cowbell": ("cowbell", {}),
    "Cowbell Short": ("cowbell", {"decay": 120, "f1": 587, "f2": 845}),
    "808 Conga High": ("conga", {}),
    "808 Conga Mid": ("conga", {"start": 330, "end": 240}),
    "808 Conga Low": ("conga", {"start": 250, "end": 180, "decay": 260}),
    "Bongo": ("conga", {"start": 520, "end": 420, "time": 10, "decay": 140, "noise": 0.4}),
    "Shaker": ("shaker", {}),
    "Shaker Long": ("shaker", {"attack": 25, "decay": 160, "shakes": 2, "spacing": 60}),
    "Maracas": ("shaker", {"attack": 4, "decay": 60, "tone": 8_000}),
    "Tambourine": ("tambourine", {}),
    "Tambourine Short": ("tambourine", {"decay": 110}),
    "Perc Blip": ("zap", {"start": 1_500, "end": 900, "time": 20, "decay": 60}),
    "Perc Metal": ("hat", {"pitch": 0.5, "decay": 160, "tone": 3_000, "cut": 1_500, "noise": 0.0}),
}
FX = {
    "Zap Down": ("zap", {}, None),
    "Zap Fast": ("zap", {"start": 6_000, "end": 200, "time": 40, "decay": 80}, None),
    "Laser": ("zap", {"start": 3_000, "end": 300, "time": 300, "decay": 350}, None),
    "Pew": ("zap", {"start": 2_500, "end": 600, "time": 80, "decay": 100}, None),
    "Riser 2s": ("sweep", {"from": 300, "to": 9_000, "q": 3, "rise": 1}, 2.0),
    "Riser 4s": ("sweep", {"from": 200, "to": 12_000, "q": 4, "rise": 1}, 4.0),
    "Downlifter": ("sweep", {"from": 9_000, "to": 200, "q": 3, "rise": 0}, 2.0),
    "Impact": ("impact", {}, None),
    "Impact Short": ("impact", {"decay": 700, "noiseDecay": 150}, None),
    "Sub Drop": ("impact", {"low": 40, "decay": 2_500, "noise": 0.0}, None),
    "Noise Hit": ("impact", {"low": 60, "noise": 1.0, "noiseDecay": 400, "decay": 300, "tone": 6_000}, None),
}


def sounds() -> dict[str, dict]:
    """Every file of the bank: its path and recipe."""
    made: dict[str, dict] = {}

    def add(path: str, kind: str, values: dict, seconds: float) -> None:
        milliseconds = math.ceil(round(seconds * 1000, 6))
        made[path] = {"kind": kind, "values": values, "seed": zlib.crc32(path.encode("utf-8")) & 0x7FFFFFFF,
                      "seconds": milliseconds / 1000}

    for name, settings in list(KICK_PRESETS.items()) + list(EXTRA_KICKS.items()):
        if name == "Init":
            continue
        values = kick_values(settings)
        add(f"Kicks/{name}.wav", "kick", values, kick_seconds(values))
    for group, table, kind in (("Snares", SNARES, "snare"), ("Claps", CLAPS, "clap"), ("Hats", HATS, "hat"),
                               ("Toms", TOMS, "tom")):
        for name, values in table.items():
            add(f"{group}/{name}.wav", kind, values, seconds_of(kind, values))
    for name, (kind, values) in CYMBALS.items():
        add(f"Cymbals/{name}.wav", kind, values, seconds_of(kind, values))
    for name, (kind, values) in PERCUSSION.items():
        add(f"Percussion/{name}.wav", kind, values, seconds_of(kind, values))
    for name, (kind, values, seconds) in FX.items():
        add(f"FX/{name}.wav", kind, values, seconds if seconds else seconds_of(kind, values))
    return made


def wav_size(recipe: dict) -> int:
    frames = round(recipe["seconds"] * RATE)
    return 44 + 2 * frames


README = """CyDAW Electro
=============

Electronic drums made by CyDAW on this computer: kicks, snares, claps, hats, cymbals, toms,
percussion and effects. Nothing here was recorded or downloaded: each sound comes from a recipe
CyDAW ships (BankLists/cydaw-electro.json in its factory content), played by its own drum and
Kick engines and written as a 16-bit, 48 kHz mono WAV, its peak at -1 dBFS.

The kits under Electronic drums in the instruments browser play them (808, 909, techno, house,
trap, lo-fi, hard dance, drum and bass, a collection of kicks, percussion and effects), mapped the
General MIDI way: C2 kick, C#2 rim, D2 snare, D#2 clap, F#2 closed hat, A#2 open hat, C#3 crash.

Generate them again from Options > Sound banks at any time: the same recipes give the same sounds.
"""

LICENCE = """CyDAW Electro - licence
=======================

The sounds of this folder, the recipes they are made from and the kits that play them are part
of CyDAW and are dedicated to the public domain under Creative Commons Zero 1.0 (CC0 1.0):
no rights reserved. Use them, change them, sample them and share them, in music you sell too,
without credit or permission.

https://creativecommons.org/publicdomain/zero/1.0/
"""

# ---- The kits -------------------------------------------------------------------------------------

# Level of each family against the kick (dB): every sound peaks at -1 dBFS in the bank.
LEVELS = {"Kicks": 0.0, "Snares": -2.0, "Claps": -3.0, "Hats": -9.0, "Cymbals": -11.0, "Toms": -4.0,
          "Percussion": -6.0, "FX": -6.0}
OPEN = {"808 Open Hat", "909 Open Hat", "Trap Open Hat", "House Open Hat"}

TOMS_808 = {41: "Toms/808 Low Tom", 43: "Toms/808 Low Tom", 45: "Toms/808 Mid Tom", 47: "Toms/808 Mid Tom",
            48: "Toms/808 High Tom", 50: "Toms/808 High Tom"}
TOMS_909 = {41: "Toms/909 Low Tom", 43: "Toms/909 Low Tom", 45: "Toms/909 Mid Tom", 47: "Toms/909 Mid Tom",
            48: "Toms/909 High Tom", 50: "Toms/909 High Tom"}
CONGAS = {62: "Percussion/808 Conga High", 63: "Percussion/808 Conga Mid", 64: "Percussion/808 Conga Low"}

KITS = [
    {"key": "electro-808", "folder": "Electro 808", "fr": "Boîte à rythmes 808", "en": "808 drum machine", "map": {
        35: "Kicks/808 Short", 36: "Kicks/808 Long", 37: "Percussion/808 Rim", 38: "Snares/808 Snare",
        39: "Claps/808 Clap", 40: "Snares/Electro Snare", 42: "Hats/808 Closed Hat", 44: "Hats/808 Pedal Hat",
        46: "Hats/808 Open Hat", 49: "Cymbals/808 Cymbal", 51: "Cymbals/Ride", 56: "Percussion/808 Cowbell",
        70: "Percussion/Maracas", 75: "Percussion/Claves", **TOMS_808, **CONGAS}},
    {"key": "electro-909", "folder": "Electro 909", "fr": "Boîte à rythmes 909", "en": "909 drum machine", "map": {
        35: "Kicks/House Classic", 36: "Kicks/909 Punch", 37: "Percussion/909 Rim", 38: "Snares/909 Snare",
        39: "Claps/909 Clap", 40: "Snares/Tight Snare", 42: "Hats/909 Closed Hat", 44: "Hats/808 Pedal Hat",
        46: "Hats/909 Open Hat", 49: "Cymbals/Crash", 51: "Cymbals/Ride", 53: "Cymbals/Ride Bell",
        54: "Percussion/Tambourine", 56: "Percussion/Cowbell Short", **TOMS_909}},
    {"key": "electro-techno", "folder": "Electro Techno", "fr": "Kit techno", "en": "Techno kit", "map": {
        35: "Kicks/Techno Rumble", 36: "Kicks/Techno Hard", 37: "Percussion/909 Rim", 38: "Snares/Techno Snare",
        39: "Claps/Tight Clap", 40: "Claps/Snap", 42: "Hats/Techno Hat", 44: "Hats/Dark Hat", 46: "Hats/909 Open Hat",
        49: "Cymbals/Crash", 51: "Cymbals/Ride", 56: "Percussion/Cowbell Short", 60: "Percussion/Perc Metal",
        61: "Percussion/Perc Blip", 41: "Toms/Electro Tom", 45: "Toms/Syn Tom Drop",
        84: "FX/Riser 2s", 85: "FX/Riser 4s", 86: "FX/Downlifter", 87: "FX/Impact", 88: "FX/Noise Hit", 89: "FX/Zap Fast",
        90: "FX/Laser"}},
    {"key": "electro-house", "folder": "Electro House", "fr": "Kit house", "en": "House kit", "map": {
        35: "Kicks/Deep House", 36: "Kicks/House Classic", 37: "Percussion/808 Rim", 38: "Snares/House Snare",
        39: "Claps/House Clap", 40: "Claps/Snap", 42: "Hats/909 Closed Hat", 44: "Hats/808 Pedal Hat",
        46: "Hats/House Open Hat", 49: "Cymbals/Crash", 51: "Cymbals/Ride", 54: "Percussion/Tambourine",
        56: "Percussion/808 Cowbell", 60: "Percussion/Bongo", 69: "Percussion/Shaker Long", 70: "Percussion/Shaker",
        75: "Percussion/Claves", **CONGAS}},
    {"key": "electro-trap", "folder": "Electro Trap", "fr": "Kit trap", "en": "Trap kit", "map": {
        35: "Kicks/Sub Kick", 36: "Kicks/Trap Boom", 37: "Percussion/808 Rim", 38: "Snares/Trap Snare",
        39: "Claps/Trap Clap", 40: "Snares/Fat Snare", 42: "Hats/Trap Closed Hat", 44: "Hats/Bright Hat",
        46: "Hats/Trap Open Hat", 49: "Cymbals/Crash", 56: "Percussion/808 Cowbell", 75: "Percussion/Claves",
        **TOMS_808, 84: "FX/Riser 2s", 85: "FX/Downlifter", 86: "FX/Impact", 87: "FX/Sub Drop", 88: "FX/Pew"}},
    {"key": "electro-lofi", "folder": "Electro Lo-Fi", "fr": "Kit lo-fi", "en": "Lo-fi kit", "map": {
        35: "Kicks/Acoustic Felt", 36: "Kicks/Lo-Fi Dusty", 37: "Percussion/Wood Block", 38: "Snares/Lo-Fi Snare",
        39: "Claps/Snap", 40: "Snares/Fat Snare", 42: "Hats/Lo-Fi Hat", 44: "Hats/Dark Hat", 46: "Hats/808 Open Hat",
        51: "Cymbals/Ride", 54: "Percussion/Tambourine Short", 70: "Percussion/Maracas", 75: "Percussion/Claves"}},
    {"key": "electro-hard", "folder": "Electro Hard Dance", "fr": "Kit hard dance", "en": "Hard dance kit", "map": {
        35: "Kicks/Gabber", 36: "Kicks/Hardstyle", 37: "Percussion/909 Rim", 38: "Snares/909 Snare",
        39: "Claps/Big Clap", 40: "Snares/Electro Snare", 42: "Hats/909 Closed Hat", 46: "Hats/909 Open Hat",
        49: "Cymbals/Crash", 51: "Cymbals/Ride", 84: "FX/Riser 4s", 85: "FX/Impact", 86: "FX/Noise Hit", 87: "FX/Laser"}},
    {"key": "electro-dnb", "folder": "Electro Drum and Bass", "fr": "Kit drum and bass", "en": "Drum and bass kit", "map": {
        35: "Kicks/Garage", 36: "Kicks/Drum & Bass", 37: "Percussion/909 Rim", 38: "Snares/DnB Snare",
        39: "Claps/Tight Clap", 40: "Snares/Tight Snare", 42: "Hats/Bright Hat", 44: "Hats/808 Pedal Hat",
        46: "Hats/Trap Open Hat", 49: "Cymbals/Splash", 51: "Cymbals/Ride", 52: "Cymbals/China", 70: "Percussion/Shaker",
        **TOMS_909}},
]


def kick_collection() -> dict:
    kicks = [path[: -len(".wav")] for path in sounds() if path.startswith("Kicks/")]
    return {"key": "electro-kicks", "folder": "Electro Kicks", "fr": "Collection de kicks", "en": "Kick collection",
            "map": {36 + index: path for index, path in enumerate(kicks)}}


def percussion_and_fx() -> dict:
    paths = [path[: -len(".wav")] for path in sounds() if path.startswith(("Percussion/", "Cymbals/", "Toms/"))]
    effects = [path[: -len(".wav")] for path in sounds() if path.startswith("FX/")]
    mapping = {48 + index: path for index, path in enumerate(paths)}
    mapping.update({84 + index: path for index, path in enumerate(effects)})
    return {"key": "electro-perc-fx", "folder": "Electro Percussion and FX", "fr": "Percussions et effets électro", "en": "Electro percussion and FX",
            "map": mapping}


NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(note: int) -> str:
    return f"{NAMES[note % 12]}{note // 12 - 1}"


def folder_name(kit: dict) -> str:
    return kit["folder"]


# ---- Writing -------------------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=ROOT / "content" / "factory")
    args = parser.parse_args()
    out = args.out
    made = sounds()
    files = [["LICENCE.txt", len(LICENCE.encode("utf-8")), 0, {"text": LICENCE}],
             ["README.txt", len(README.encode("utf-8")), 0, {"text": README}]]
    for path, recipe in made.items():
        files.append([path, wav_size(recipe), 0, recipe])
    # One file a line, as the other lists.
    body = ",\n".join(json.dumps(entry, ensure_ascii=False) for entry in files)
    (out / "BankLists").mkdir(parents=True, exist_ok=True)
    (out / "BankLists" / f"{BANK}.json").write_text('{"files": [\n' + body + "\n]}\n", encoding="utf-8")
    total = sum(entry[1] for entry in files)

    # The registry: this bank replaced or added, the others as they are.
    registry_path = out / "banks.json"
    registry = json.loads(registry_path.read_text(encoding="utf-8"))
    entry = {
        "id": BANK,
        "title": "CyDAW Electro",
        "author": "CyDAW",
        "licence": "CC0-1.0",
        "licenceUrl": CC0,
        "homepage": "",
        "repository": "",
        "commit": "",
        "generator": "cydaw-drums",
        "rate": RATE,
        "description": {
            "fr": "Batteries électroniques générées par CyDAW, rien à télécharger : kicks, caisses claires, claps, "
                  "charleys, cymbales, toms, percussions, effets.",
            "en": "Electronic drums CyDAW generates, nothing to download: kicks, snares, claps, hats, cymbals, toms, "
                  "percussion, effects.",
        },
        "list": f"BankLists/{BANK}.json",
        "count": len(files),
        "bytes": total,
    }
    banks = [bank for bank in registry["banks"] if bank["id"] != BANK] + [entry]
    registry["banks"] = banks
    registry_path.write_text(json.dumps(registry, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")

    # The kits.
    catalogue_path = out / "catalogue.json"
    catalogue = json.loads(catalogue_path.read_text(encoding="utf-8"))
    kits = KITS + [kick_collection(), percussion_and_fx()]
    entries = []
    for kit in kits:
        name = folder_name(kit)
        zones = []
        for note, path in sorted(kit["map"].items()):
            family = path.split("/")[0]
            if f"{path}.wav" not in made:
                raise SystemExit(f"{kit['key']}: no sound {path}")
            gain = LEVELS[family] + (1.0 if path.split("/")[1] in OPEN else 0.0)
            zone = {"file": f"bank:{BANK}/{path}.wav", "root": note, "keys": [note, note], "velocities": [1, 127], "gain": gain}
            # The hats choke each other: the closed or the pedal one cuts the open one.
            if family == "Hats" and kit["key"] != "electro-perc-fx":
                zone.update({"group": 1, "offBy": 1})
            zones.append(zone)
        document = {"format": "cydaw-sampler", "version": 1, "name": kit["en"],
                    "settings": {"one-shot": 1, "velocity-db": 10}, "zones": zones}
        folder = out / "Instruments" / "Electro" / name
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f"{name}.cysampler").write_text(json.dumps(document, indent=1, ensure_ascii=False), encoding="utf-8")
        notes = sorted(kit["map"])
        size = sum(wav_size(made[f"{path}.wav"]) for path in set(kit["map"].values()))
        entries.append({
            "key": kit["key"],
            "names": {"fr": kit["fr"], "en": kit["en"]},
            "category": "electro-drums",
            "map": f"Instruments/Electro/{name}/{name}.cysampler",
            "settings": {"one-shot": 1, "velocity-db": 10},
            "envelope": {"attack": 1, "release": 300},
            "source": BANK,
            "bank": BANK,
            "melodic": False,
            "licence": "CC0-1.0",
            "range": [note_name(notes[0]), note_name(notes[-1])],
            "report": {"key": kit["key"], "zones": len(zones), "files": len(set(kit["map"].values())),
                       "megabytes": round(size / 1e6, 1),
                       "description": f"{len(zones)} zones · {note_name(notes[0])}–{note_name(notes[-1])} · generated"},
        })
        print(json.dumps(entries[-1]["report"], ensure_ascii=False), flush=True)
    ours = {kit["key"] for kit in kits}
    instruments = [item for item in catalogue["instruments"] if item["key"] not in ours] + entries
    catalogue = {"format": "cydaw-factory-catalogue", "version": 1, "categories": CATEGORIES, "instruments": instruments}
    catalogue_path.write_text(json.dumps(catalogue, indent=1, ensure_ascii=False), encoding="utf-8")
    write_licence(out, catalogue)
    print(json.dumps({"bank": BANK, "files": len(files), "megabytes": round(total / 1e6, 1), "kits": len(entries)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
