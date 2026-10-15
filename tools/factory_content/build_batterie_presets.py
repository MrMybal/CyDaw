"""Batterie's factory sounds: a kit and its mix, each set through CyDAW 2's
control port on a Batterie card, heard offline (no sound out: the exported
plug-in's path renders a hit in memory), saved as a device preset and copied
into content/factory/Presets/device/batterie.

    python tools/factory_content/build_batterie_presets.py [--exe "<CyDAW 2.exe>"] [--port 47839]

The kits' samples are the free banks CyDAW downloads (Options > Banques de
sons): this reads where they are from the person's Locations file. A sound is
a kit and settings over the neutral mix: keys as in DrumKit.h ("snare.tune",
"kit.humanise"), and the kit's own microphones and controls (its MICROS tab,
the SFZ's controls) by their label (MICS: "Kick OH": 0..127), read from the
kit's list. A preset keeps the controls it sets; every other one goes back where
the kit starts it. The loudest piece of each sound is kept under -1 dBFS: the
kit's level comes down when it is louder.

Names are English and descriptive (no brand, product, artist or machine
model), without the characters a file name refuses: they become file names.
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

# The kits, as DrumKit.h lists them (and their English names, as the port answers).
RUSTY, JAZZ, SWIRLY, STICKS, BRUSHES, SEVENTIES = range(6)
KIT_NAMES = ("Rusty", "Jazz", "Swirly", "Unruly (sticks)", "Unruly (brushes)", "Seventies")

TOMS = ("tom1", "tom2", "tom3", "tom4")
CYMBALS = ("crash", "ride", "china")

# The pieces heard to keep the loudest under the ceiling (kick, snare, hats, toms, cymbals).
MEASURED = (36, 38, 42, 46, 47, 45, 43, 41, 49, 51, 52)
CEILING_DB = -1.0


def each(pieces, field, value):
    return {f"{piece}.{field}": value for piece in pieces}


def per(pieces, field, values):
    return {f"{piece}.{field}": value for piece, value in zip(pieces, values)}


# Pans are from the drummer's seat, as the kits' own: hi-hat and high toms on the
# left, floor tom and ride on the right.
SOUNDS = {
    # ---- The first sounds, as they were.
    "Raw Rock Kit": {"kit": RUSTY, "set": {}},
    "Dry Rock": {"kit": RUSTY, "set": {"kick.level": 2, "snare.level": 1.5, **each(TOMS, "decay", 0.75), "crash.level": -3,
                                       "hihat.level": -2}},
    "Tight Metal": {"kit": RUSTY, "set": {"kick.level": 3, "kick.decay": 0.6, "snare.level": 2, "snare.tune": 1,
                                          **each(TOMS, "tune", -1), **each(TOMS, "decay", 0.7), **each(CYMBALS, "level", -4)}},
    "Deep Toms": {"kit": RUSTY, "set": {**each(TOMS, "tune", -3), **each(TOMS, "level", 2), "kick.tune": -1}},
    "Dusty Hip-Hop": {"kit": RUSTY, "set": {"kick.level": 3, "kick.tune": -2, "snare.tune": -1, "snare.decay": 0.5,
                                            "hihat.level": -4, "kit.humanise": 0.3}},
    "Mellow Jazz": {"kit": JAZZ, "set": {"kick.level": -4, "ride.level": 2, "snare.level": -2, "kit.humanise": 0.3}},
    "Bright Jazz": {"kit": JAZZ, "set": {"kit.tune": 1, **each(CYMBALS, "level", 1), "hihat.level": 1}},
    "Classic Pop Kit": {"kit": SWIRLY, "set": {"snare.level": 1, "hihat.level": -1, "kick.level": 1}},
    "Tight Funk": {"kit": STICKS, "set": {"hihat.decay": 0.6, "snare.level": 2, "kick.decay": 0.7, "kit.humanise": 0.2}},
    "Open Stick Kit": {"kit": STICKS, "set": {**each(CYMBALS, "level", 1), "kick.level": 1}},
    "Intimate Brushes": {"kit": BRUSHES, "set": {"kick.level": -3, "snare.level": 1, "kit.humanise": 0.4}},
    "Seventies Kit": {"kit": SEVENTIES, "set": {"kit.tune": -1, **each(TOMS, "decay", 0.8)}},
    "Deep Lo-Fi Kit": {"kit": SEVENTIES, "set": {"kit.tune": -4, "kit.humanise": 0.5, "snare.decay": 0.6}},

    # ---- Rock kit.
    "Big Room Rock": {"kit": RUSTY, "set": {
        # Everything rings as recorded, toms up and spread, cymbals under the drums.
        "kick.level": 1.5, "kick.tune": -0.5, "snare.level": 1, "snare.tune": -0.5,
        "hihat.level": -2.5, "hihat.pan": -0.2,
        **each(TOMS, "level", 1.5), **each(TOMS, "tune", -1), **per(TOMS, "pan", (-0.4, -0.2, 0.15, 0.4)),
        "crash.level": -1.5, "crash.pan": -0.3, "ride.level": -1, "ride.pan": 0.3, "china.level": -3, "china.pan": 0.45,
        "kit.humanise": 0.15,
    }},
    "Dry Funk Pocket": {"kit": RUSTY, "set": {
        # Short and tuned up: a cracking snare, a tight kick, closed hats, muffled toms.
        "kick.level": 1, "kick.tune": 1, "kick.decay": 0.5, "snare.level": 1.5, "snare.tune": 2, "snare.decay": 0.55,
        "hihat.level": -1, "hihat.decay": 0.45, "hihat.pan": -0.15,
        **each(TOMS, "tune", 1), **each(TOMS, "decay", 0.55), **per(TOMS, "pan", (-0.25, -0.15, 0.1, 0.25)),
        "crash.level": -4, "crash.decay": 0.7, "ride.level": -3, "ride.decay": 0.8, "ride.pan": 0.2, "kit.humanise": 0.2,
    }},
    "Modern Metal Click": {"kit": RUSTY, "set": {
        # A short, tuned-up kick that cuts through fast double bass; low, long toms.
        "kick.level": 3, "kick.tune": 2, "kick.decay": 0.35, "snare.level": 2.5, "snare.tune": 2, "snare.decay": 0.65,
        "hihat.level": -3, "hihat.decay": 0.6, "hihat.pan": -0.2,
        **each(TOMS, "level", 2), **each(TOMS, "tune", -3), **each(TOMS, "decay", 0.85),
        **per(TOMS, "pan", (-0.35, -0.2, 0.15, 0.35)),
        "crash.level": -2, "crash.pan": -0.3, "ride.level": -4, "ride.pan": 0.3, "china.level": 1, "china.pan": 0.45,
        "kit.humanise": 0.03,
    }},
    "Punk Fast and Loose": {"kit": RUSTY, "set": {
        # Loud cymbals, a hard snare, a loose hand.
        "kick.level": 1, "snare.level": 2, "snare.tune": 1, "hihat.level": 0.5, "hihat.pan": -0.15,
        **each(TOMS, "level", 0.5), "crash.level": 2, "crash.pan": -0.25, "ride.level": 1, "ride.pan": 0.25,
        "china.level": 0, "kit.humanise": 0.4,
    }},
    "Stadium Power Toms": {"kit": RUSTY, "set": {
        # Toms tuned down, loud and wide, ringing out.
        **each(TOMS, "tune", -2.5), **each(TOMS, "level", 3), **per(TOMS, "pan", (-0.45, -0.2, 0.2, 0.45)),
        "kick.level": 1, "kick.tune": -1, "snare.level": 0.5, "snare.tune": -1,
        "crash.level": -2, "crash.pan": -0.3, "ride.pan": 0.3, "kit.humanise": 0.15,
    }},

    # ---- Jazz kit.
    "Soft Ballad": {"kit": JAZZ, "set": {
        "kick.level": -1, "kick.tune": -1, "snare.level": -1, "snare.tune": -1,
        "hihat.level": -3, "hihat.pan": -0.2, "ride.level": 1, "ride.pan": 0.25,
        **each(TOMS, "tune", -1.5), "crash.level": -2, "crash.pan": -0.25, "kit.humanise": 0.25,
    }},
    "Bebop Bright Ride": {"kit": JAZZ, "set": {
        # The ride leads, the kick only feathers, everything tuned up.
        "kit.tune": 1.5, "ride.level": 3, "ride.pan": 0.3, "kick.level": -3, "kick.decay": 0.8,
        "snare.level": -1, "snare.tune": 1, "hihat.level": -1, "hihat.pan": -0.2,
        **each(TOMS, "tune", 2), **each(TOMS, "level", -1), "crash.level": -1, "kit.humanise": 0.35,
    }},
    "Dry Studio Jazz": {"kit": JAZZ, "set": {
        # Cymbals and toms damped, the drums a little forward.
        "kick.level": -2, "kick.decay": 0.8, "snare.level": 0.5, "snare.decay": 0.75, "hihat.level": -1, "hihat.pan": -0.15,
        "ride.level": 0.5, "ride.decay": 0.85, "ride.pan": 0.2, "crash.level": -2, "crash.decay": 0.75,
        **each(TOMS, "decay", 0.7), "kit.humanise": 0.25,
    }},

    # ---- Pop kit.
    "Polished Pop": {"kit": SWIRLY, "set": {
        "kick.level": 1.5, "kick.decay": 0.8, "snare.level": 1, "snare.decay": 0.85, "hihat.level": -1.5, "hihat.pan": -0.2,
        **each(TOMS, "decay", 0.8), "crash.level": -2, "crash.pan": -0.25, "ride.level": -1, "ride.pan": 0.25,
        "kit.humanise": 0.08,
    }},
    "Indie Loose Kit": {"kit": SWIRLY, "set": {
        # Low, ringing toms, a slack snare, cymbals forward, a loose hand.
        "kick.level": 0.5, "kick.tune": -1, "snare.level": 0.5, "snare.tune": -1.5, "hihat.level": -1, "hihat.pan": -0.25,
        **each(TOMS, "tune", -2), **each(TOMS, "level", 1), **per(TOMS, "pan", (-0.3, -0.15, 0.1, 0.3)),
        "crash.level": 1, "crash.pan": -0.3, "ride.level": 0.5, "ride.pan": 0.3, "kit.humanise": 0.3,
    }},
    "Shoegaze Cymbal Wash": {"kit": SWIRLY, "set": {
        # Cymbals over the drums, all ringing, wide.
        "kick.level": -2, "snare.level": -2, "hihat.level": 1, "hihat.pan": -0.3,
        "crash.level": 3, "crash.pan": -0.4, "ride.level": 3, "ride.pan": 0.4, "china.level": 2,
        **each(TOMS, "tune", -1), "kit.humanise": 0.3,
    }},
    "Disco Tight Four": {"kit": SWIRLY, "set": {
        "kick.level": 2, "kick.tune": 0.5, "kick.decay": 0.55, "snare.level": 0.5, "snare.decay": 0.65,
        "hihat.level": 1, "hihat.decay": 0.8, "hihat.pan": -0.15, **each(TOMS, "decay", 0.6),
        "crash.level": -3, "kit.humanise": 0.1,
    }},

    # ---- Stick kit.
    "Live Stage Kit": {"kit": STICKS, "set": {
        "kick.level": 2, "kick.decay": 0.85, "snare.level": 1.5, "hihat.level": -1, "hihat.pan": -0.25,
        **each(TOMS, "level", 1), **per(TOMS, "pan", (-0.35, -0.2, 0.1, 0.35)),
        "crash.pan": -0.35, "ride.pan": 0.35, "kit.humanise": 0.3,
    }},
    "Reggae One Drop": {"kit": STICKS, "set": {
        # A high, short snare (the rim and cross-stick ring with it), a low kick.
        "kick.level": 1.5, "kick.tune": -1, "kick.decay": 0.75, "snare.level": 1, "snare.tune": 4, "snare.decay": 0.6,
        "hihat.level": -2, "hihat.decay": 0.7, **each(TOMS, "tune", 2), **each(TOMS, "decay", 0.75),
        "crash.level": -4, "ride.level": -2, "kit.humanise": 0.2,
    }},
    "Garage Rock Trashy": {"kit": STICKS, "set": {
        "kick.level": 1, "snare.level": 1, "snare.tune": -1, "hihat.level": 1, "hihat.pan": -0.2,
        "crash.level": 2, "crash.pan": -0.3, "ride.level": 1, "ride.pan": 0.3, "china.level": 1, "kit.humanise": 0.4,
    }},

    # ---- Brush kit.
    "Late Night Brushes": {"kit": BRUSHES, "set": {
        "kick.level": -3, "kick.decay": 0.7, "snare.tune": 0.5, "hihat.level": -2, "hihat.pan": -0.2,
        "ride.level": 2, "ride.pan": 0.3, "crash.level": -4, **each(TOMS, "level", -2), **each(TOMS, "decay", 0.85),
        "kit.tune": 0.5, "kit.humanise": 0.35,
    }},
    "Brushes Ballad": {"kit": BRUSHES, "set": {
        "kick.level": -3, "snare.level": 1.5, "snare.tune": -1, "hihat.level": -1, "hihat.pan": -0.15,
        **each(TOMS, "tune", -1), "ride.pan": 0.2, "crash.pan": -0.2, "kit.humanise": 0.3,
    }},

    # ---- Seventies kit (no cymbals).
    "Lo-Fi Choked Kit": {"kit": SEVENTIES, "set": {
        "kit.tune": -2, "kick.level": 2, "kick.decay": 0.55, "snare.level": 1, "snare.tune": -1, "snare.decay": 0.5,
        "hihat.level": -4, "hihat.decay": 0.4, **each(TOMS, "level", -2), **each(TOMS, "decay", 0.5),
        "kit.humanise": 0.45,
    }},
    "Vintage Soul Dry": {"kit": SEVENTIES, "set": {
        "kit.tune": 1, "kick.decay": 0.6, "snare.level": 1, "snare.tune": 1, "snare.decay": 0.7,
        "hihat.level": -1, "hihat.decay": 0.6, **each(TOMS, "tune", 1), **each(TOMS, "decay", 0.7), "kit.humanise": 0.3,
    }},
    "Seventies Loose Rock": {"kit": SEVENTIES, "set": {
        "kit.tune": -1, "kick.level": 1, "snare.level": 0.5, "hihat.level": -1, "hihat.pan": -0.2,
        **each(TOMS, "level", 1.5), **each(TOMS, "tune", -1), **per(TOMS, "pan", (-0.3, -0.15, 0.15, 0.3)),
        "kit.humanise": 0.3,
    }},
    "Boom Bap Break Kit": {"kit": SEVENTIES, "set": {
        "kick.level": 3, "kick.tune": -2, "kick.decay": 0.7, "snare.level": 1.5, "snare.tune": -2, "snare.decay": 0.6,
        "hihat.level": -5, "hihat.decay": 0.5, **each(TOMS, "level", -3), "kit.humanise": 0.35,
    }},
}


# The kits' own microphones and controls (the MICROS tab: the SFZ's controls, 0..127),
# by their label as the kit names them; a preset keeps the ones it sets, the others
# stay where the kit starts them. Each kit has its own:
#   Rock kit: "Kick mic", "Kick OH" (starts at 0), "Kick punch"; "Snare top", "Snare bottom",
#     "Snare OH", "Snare snap", "Snare punch", "Snare epic"; each tom's close and oh ("18" the
#     floor tom, "15", "14"), "Lo tom punch", "Hi tom punch"; "Hi-hat close", "Hi-hat OH";
#     "Crash close", "Crash OH", "Ride close", "Ride OH". Its overheads are its room.
#   Jazz kit: "Kick mic vol", "Kick mic bleed", "Snare mic vol", "Snare mic bleed", "OH mic vol"
#     (all start at 127).
#   Pop kit: each piece's room ("Kick ambience"... "Cymbal ambience"), the kick's "Reso head"
#     and "Batter head", the snare's "Top vol" and "Bottom vol", "Hi-hat vol", "Crash vol",
#     "Ride vol".
#   Stick and brush kits: "Kick out", "Kick in" (starts at 0), "Kick OH"; top, bottom and OH of
#     "14" (the snare and middle tom), "22" (the floor tom) and "13" (the high tom), "14 snap";
#     close and OH of the hi-hat, crash and ride (the close ones start at 0).
#   Seventies kit: "Kick vol", "Snare vol", "Hi-hat vol", "Tom vol", "Extra mics" (its room,
#     starts at 0).
ROCK_TOMS = ("18", "15", "14")


def toms_mic(mic, value):
    return {f"{tom} {mic}": value for tom in ROCK_TOMS}


MICS = {
    # ---- Rock kit ("Raw Rock Kit" stays as recorded).
    "Dry Rock": {"Kick mic": 115, "Snare top": 60, "Snare OH": 40, "Hi-hat OH": 80, **toms_mic("oh", 50),
                 "Crash OH": 85, "Ride OH": 85},
    "Tight Metal": {"Kick mic": 120, "Kick punch": 80, "Snare top": 80, "Snare punch": 50, "Snare OH": 45,
                    **toms_mic("close", 115), **toms_mic("oh", 50), "Lo tom punch": 50, "Hi tom punch": 50},
    "Deep Toms": {**toms_mic("close", 110), **toms_mic("oh", 100), "Lo tom punch": 40, "Hi tom punch": 30},
    "Dusty Hip-Hop": {"Kick mic": 115, "Snare top": 55, "Snare OH": 45, "Hi-hat close": 90, "Hi-hat OH": 70},
    # Room up: every overhead forward, the kick's included.
    "Big Room Rock": {"Kick mic": 110, "Kick OH": 75, "Snare top": 60, "Snare bottom": 100, "Snare OH": 110,
                      **toms_mic("oh", 115), "Hi-hat close": 80, "Hi-hat OH": 115, "Crash OH": 120, "Ride OH": 115},
    # Close mics up, overheads down.
    "Dry Funk Pocket": {"Kick mic": 120, "Kick OH": 0, "Snare top": 70, "Snare bottom": 90, "Snare OH": 30,
                        "Snare snap": 40, "Hi-hat close": 110, "Hi-hat OH": 50, **toms_mic("close", 110),
                        **toms_mic("oh", 35), "Crash close": 60, "Crash OH": 70, "Ride close": 70, "Ride OH": 70},
    "Modern Metal Click": {"Kick mic": 127, "Kick OH": 0, "Kick punch": 100, "Snare top": 100, "Snare bottom": 60,
                           "Snare OH": 40, "Snare punch": 70, "Snare snap": 50, **toms_mic("close", 120),
                           **toms_mic("oh", 45), "Lo tom punch": 80, "Hi tom punch": 80, "Hi-hat close": 100,
                           "Hi-hat OH": 60, "Crash OH": 90, "Ride OH": 85},
    "Punk Fast and Loose": {"Kick OH": 40, "Snare top": 60, "Snare OH": 100, **toms_mic("oh", 90), "Hi-hat OH": 110,
                            "Crash close": 70, "Crash OH": 120, "Ride OH": 110},
    "Stadium Power Toms": {**toms_mic("close", 120), **toms_mic("oh", 110), "Lo tom punch": 60, "Hi tom punch": 60,
                           "Kick OH": 50, "Snare OH": 90, "Snare epic": 30},

    # ---- Jazz kit: the overheads forward, the close mics and their bleed under them.
    "Mellow Jazz": {"Kick mic vol": 90, "Kick mic bleed": 100, "Snare mic vol": 100, "Snare mic bleed": 110,
                    "OH mic vol": 127},
    "Bright Jazz": {"Kick mic vol": 105, "Snare mic vol": 110, "OH mic vol": 127},
    "Soft Ballad": {"Kick mic vol": 110, "Kick mic bleed": 90, "Snare mic vol": 100, "Snare mic bleed": 90,
                    "OH mic vol": 127},
    "Bebop Bright Ride": {"Kick mic vol": 70, "Kick mic bleed": 100, "Snare mic vol": 90, "Snare mic bleed": 110,
                          "OH mic vol": 127},
    # The other way round: close mics, little bleed, overheads down.
    "Dry Studio Jazz": {"Kick mic vol": 127, "Kick mic bleed": 60, "Snare mic vol": 127, "Snare mic bleed": 60,
                        "OH mic vol": 60},

    # ---- Pop kit: heads and each piece's room.
    "Classic Pop Kit": {"Batter head": 80, "Top vol": 90},
    "Polished Pop": {"Reso head": 105, "Batter head": 85, "Top vol": 95, "Bottom vol": 75, "Kick ambience": 15,
                     "Snare ambience": 45, "Hi-hat ambience": 15, "Tom ambience": 30, "Cymbal ambience": 35},
    "Indie Loose Kit": {"Kick ambience": 70, "Snare ambience": 110, "Hi-hat ambience": 55, "Tom ambience": 100,
                        "Cymbal ambience": 85, "Batter head": 60, "Reso head": 100, "Top vol": 70, "Bottom vol": 90},
    "Shoegaze Cymbal Wash": {"Kick ambience": 90, "Snare ambience": 127, "Hi-hat ambience": 90, "Tom ambience": 120,
                             "Cymbal ambience": 127, "Crash vol": 115, "Ride vol": 110},
    "Disco Tight Four": {"Kick ambience": 0, "Snare ambience": 25, "Hi-hat ambience": 10, "Tom ambience": 15,
                         "Cymbal ambience": 20, "Reso head": 90, "Batter head": 100, "Top vol": 100, "Bottom vol": 70,
                         "Hi-hat vol": 110},

    # ---- Stick kit.
    "Tight Funk": {"Kick out": 115, "Kick in": 40, "Kick OH": 15, "14 top": 105, "14 bottom": 70, "14 OH": 60,
                   "22 OH": 70, "13 OH": 70, "Hi-hat close": 90, "Hi-hat OH": 50},
    "Open Stick Kit": {"Kick OH": 70, "14 OH": 120, "22 OH": 120, "13 OH": 120, "Hi-hat OH": 110, "Crash OH": 110,
                       "Ride OH": 110},
    "Live Stage Kit": {"Kick out": 115, "Kick in": 50, "Kick OH": 50, "14 top": 100, "14 bottom": 70, "14 OH": 90,
                       "22 top": 100, "22 OH": 100, "13 top": 95, "13 OH": 100, "Hi-hat close": 60, "Hi-hat OH": 90,
                       "Crash close": 40, "Crash OH": 90, "Ride close": 50, "Ride OH": 90},
    "Reggae One Drop": {"14 top": 110, "14 bottom": 50, "14 OH": 70, "14 snap": 50, "Kick out": 110, "Kick OH": 30,
                        "Hi-hat close": 70, "Hi-hat OH": 60},
    "Garage Rock Trashy": {"Kick in": 0, "Kick OH": 90, "14 top": 60, "14 OH": 120, "22 OH": 120, "13 OH": 120,
                           "Hi-hat close": 30, "Hi-hat OH": 120, "Crash OH": 120, "Ride OH": 115},

    # ---- Brush kit: the overheads carry the brushes.
    "Intimate Brushes": {"Kick out": 80, "Kick OH": 60, "14 top": 70, "14 OH": 115, "Hi-hat OH": 110, "Ride OH": 110},
    "Late Night Brushes": {"Kick out": 70, "Kick in": 0, "Kick OH": 60, "14 top": 60, "14 bottom": 40, "14 OH": 120,
                           "Hi-hat close": 0, "Hi-hat OH": 110, "Ride close": 30, "Ride OH": 115, "Crash OH": 90},
    "Brushes Ballad": {"14 top": 110, "14 bottom": 70, "14 OH": 70, "Kick out": 100, "Kick in": 40, "Kick OH": 30,
                       "Hi-hat close": 60, "Hi-hat OH": 80, "Ride close": 60, "Ride OH": 80},

    # ---- Seventies kit: each piece's mic and the room ("Extra mics").
    "Seventies Kit": {"Extra mics": 40},
    "Deep Lo-Fi Kit": {"Extra mics": 60, "Kick vol": 100, "Hi-hat vol": 80},
    "Lo-Fi Choked Kit": {"Extra mics": 0, "Kick vol": 100, "Snare vol": 110, "Hi-hat vol": 70, "Tom vol": 80},
    "Vintage Soul Dry": {"Extra mics": 0, "Kick vol": 90, "Snare vol": 110, "Hi-hat vol": 95, "Tom vol": 90},
    "Seventies Loose Rock": {"Extra mics": 110, "Kick vol": 80, "Snare vol": 95, "Hi-hat vol": 90, "Tom vol": 100},
    "Boom Bap Break Kit": {"Kick vol": 110, "Snare vol": 110, "Hi-hat vol": 70, "Tom vol": 70, "Extra mics": 30},
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


def patient(command: dict, tries: int = 10) -> dict:
    """A command that may outlast the port's 30 s (a kit decoding): the app
    still runs it, so it is asked again once it has the time."""
    for attempt in range(tries):
        try:
            return request(command)
        except RuntimeError as error:
            if "did not answer" not in str(error) or attempt == tries - 1:
                raise
            time.sleep(3)
    raise RuntimeError("unreachable")


def set_kit_and_mix(kit: int, settings: dict, change_kit: bool) -> dict:
    """The kit (when it changes) and the mix over the neutral one. A kit that
    takes longer than the port waits is waited for, then the mix sent again."""
    command = {"cmd": "batterie", "track": "Batterie", "init": True, "set": settings}
    if change_kit:
        command["kit"] = kit
    try:
        return request(command)
    except RuntimeError as error:
        if "did not answer" not in str(error):
            raise
    deadline = time.time() + 300
    while time.time() < deadline:
        time.sleep(3)
        try:
            state = request({"cmd": "batterie", "track": "Batterie"})
        except RuntimeError as error:
            if "did not answer" in str(error):
                continue
            raise
        if state.get("kit") == KIT_NAMES[kit]:
            command.pop("kit", None)
            return patient(command)
    raise RuntimeError(f"the kit {KIT_NAMES[kit]} did not load")


def set_mics(node: int, mics: dict) -> tuple[list[str], dict[str, float]]:
    """The kit's controls by label, every other one back where the kit starts
    it; the problems, and the controller numbers set (as "sampler.ccN")."""
    # Every controller cleared first: one an earlier kit set stays on the card
    # through a kit change, and a preset would keep it.
    listing = patient({"cmd": "sampler_controls", "track": "Batterie", "nodeId": node,
                       "set": {str(number): -1 for number in range(128)}})
    controls = {item["label"]: item for item in listing.get("controls", [])}
    problems = [f"no control \"{label}\" on this kit" for label in mics if label not in controls]
    values = {str(item["cc"]): -1 for item in controls.values()}
    chosen = {str(controls[label]["cc"]): float(value) for label, value in mics.items() if label in controls}
    values.update(chosen)
    answer = patient({"cmd": "sampler_controls", "track": "Batterie", "nodeId": node, "set": values})
    problems += [str(problem) for problem in answer.get("problems", [])]
    now = {str(item["cc"]): item for item in answer.get("controls", [])}
    for number, value in chosen.items():
        if number not in now or not now[number].get("set") or abs(float(now[number]["value"]) - value) > 0.01:
            problems.append(f"cc {number} did not take {value}")
    # Published with the card (the plan the plug-in path plays).
    patient({"cmd": "batterie", "track": "Batterie"})
    return problems, {f"sampler.cc{number}": value for number, value in chosen.items()}


def hear(note: int, seconds: float = 1.0) -> dict:
    return patient({"cmd": "instrument_export_check", "track": "Batterie", "note": note, "velocity": 0.9,
                    "seconds": seconds})


def loudest(heard: dict) -> tuple[int, float]:
    sounding = [(note, item.get("peakDb", -200.0)) for note, item in heard.items() if item.get("rms", 0.0) > 1.0e-4]
    return max(sounding, key=lambda pair: pair[1]) if sounding else (0, -200.0)


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
    unknown = sorted(set(MICS) - set(SOUNDS))
    if unknown:
        print(f"microphones for no sound: {unknown}")
        return 1
    bad = set('/\\:,*?"<>|')
    for name in SOUNDS:
        if set(name) & bad or name != name.strip():
            print(f"not a file name: {name!r}")
            return 1
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
        # One kit after another: each loads once.
        current = None
        stored = {}
        for name, sound in sorted(SOUNDS.items(), key=lambda item: item[1]["kit"]):
            answer = set_kit_and_mix(sound["kit"], sound["set"], sound["kit"] != current)
            current = sound["kit"]
            problems = list(answer.get("problems", []))
            if answer.get("kit") != KIT_NAMES[sound["kit"]]:
                problems.append(f"the kit is {answer.get('kit')}, not {KIT_NAMES[sound['kit']]}")
            if not problems:
                problems, stored[name] = set_mics(answer["nodeId"], MICS.get(name, {}))
            if problems:
                print("FAIL", name, problems, flush=True)
                failures += 1
                continue
            heard = {note: hear(note, 1.5 if note in (36, 38) else 1.0) for note in MEASURED}
            note, peak = loudest(heard)
            trim = 0.0
            if peak > CEILING_DB:
                # The whole kit down until its loudest piece is under the ceiling.
                level = float(answer["values"].get("kit.level", 0.0))
                trim = round(peak - CEILING_DB + 0.05, 2)
                patient({"cmd": "batterie", "track": "Batterie", "set": {"kit.level": round(level - trim, 2)}})
                for again in {36, 38, note}:
                    heard[again] = hear(again, 1.5 if again in (36, 38) else 1.0)
                note, peak = loudest(heard)
            ok = all(heard[n].get("rms", 0.0) > 1.0e-4 and heard[n].get("peakDb", 0.0) < 0.0 for n in (36, 38))
            ok = ok and peak < 0.0
            print(("ok   " if ok else "FAIL ") + f"{name}: {answer['kit']}, {len(stored[name])} mics, "
                  f"kick peak {heard[36].get('peakDb', -200):.1f} dB, "
                  f"snare peak {heard[38].get('peakDb', -200):.1f} dB, loudest {peak:.1f} dB (note {note})"
                  + (f", kit {-trim:+.2f} dB" if trim else ""), flush=True)
            failures += 0 if ok else 1
            patient({"cmd": "presets", "track": "Batterie", "nodeId": answer["nodeId"], "action": "save", "name": name})
        TARGET.mkdir(parents=True, exist_ok=True)
        for old in TARGET.glob("*.cypreset"):
            old.unlink()
        written = sorted((presets / "device" / "batterie").glob("*.cypreset"))
        for file in written:
            document = json.loads(file.read_text(encoding="utf-8"))
            document.pop("created", None)
            # The preset keeps the microphones set, and those alone.
            kept = {key: value for key, value in document["state"]["parameters"].items() if key.startswith("sampler.cc")}
            wanted = stored.get(document["name"], {})
            if set(kept) != set(wanted) or any(abs(float(kept[key]) - value) > 0.01 for key, value in wanted.items()):
                print(f"FAIL {document['name']}: the preset keeps {kept}, not {wanted}", flush=True)
                failures += 1
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
