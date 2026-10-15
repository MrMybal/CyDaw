"""Tirette's factory sounds (a drawbar organ): a few settings over the defaults each, "Init" first.

Keys as in the model's table; choices by their English names ("Tonewheel", "Third", "C3", "Fast"); toggles 0 or 1.
A registration is written as organists write it, sixteen-foot first: bars("88 8000 000").
"""
from __future__ import annotations


def bars(registration: str) -> dict:
    """The nine drawbars (16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'), each 0..8, from their digits."""
    digits = [int(character) for character in registration if character.isdigit()]
    assert len(digits) == 9, registration
    return {f"bar{index + 1}": value for index, value in enumerate(digits)}


PRESETS = {
    "Init": {},
    "Full Organ": {
        # Every drawbar out, a little drive, the chorus and the slow cabinet.
        **bars("88 8888 888"), "drive.amount": 0.3, "tone": 0.8, "vibrato.mode": "C3", "rotary.mode": "Slow",
    },
    "Jazz Ballad": {
        # A soft, round registration with the deep chorus, barely driven.
        **bars("83 8000 000"), "vibrato.mode": "C3", "rotary.mode": "Slow", "drive.amount": 0.05, "tone": 0.6,
        "click": 0.25, "amp.release": 0.04,
    },
    "Gospel Drive": {
        # Big and bright with the 1' on top, pushed into the preamp, the cabinet spinning fast.
        **bars("88 8800 008"), "drive.amount": 0.45, "tone": 0.8, "click": 0.5, "vibrato.mode": "C3",
        "rotary.mode": "Fast",
    },
    "Rock Overdrive": {
        # A growling registration through a hot preamp, the third-harmonic percussion on the attack.
        **bars("88 8600 000"), "drive.amount": 0.72, "tone": 0.85, "click": 0.5, "perc.on": 1, "perc.harmonic": "Third",
        "perc.decay": "Fast", "vibrato.mode": "Off", "rotary.mode": "Slow",
    },
    "Soft Flute": {
        # Eight and four foot alone, a gentle onset, a medium vibrato and a half-wet slow cabinet.
        **bars("00 8500 000"), "vibrato.mode": "V2", "rotary.mode": "Slow", "rotary.mix": 0.5, "drive.amount": 0.0,
        "drive.output": 6.0, "tone": 0.45, "click": 0.0, "amp.attack": 0.03, "amp.release": 0.08,
    },
    "Percussive Jazz": {
        # The classic three drawbars with the fast third-harmonic percussion and plenty of key click.
        **bars("88 8000 000"), "perc.on": 1, "perc.harmonic": "Third", "perc.decay": "Fast", "perc.volume": "Normal",
        "vibrato.mode": "C3", "rotary.mode": "Slow", "drive.amount": 0.12, "click": 0.45,
    },
    "Church Pipe": {
        # A pipe plenum: sixteen, eight, four and two foot with a mixture on top; chiff and a hall-like release.
        **bars("60 8705 035"), "organ.type": "Pipe", "vibrato.mode": "Off", "rotary.mode": "Off", "drive.amount": 0.0,
        "tone": 0.75, "click": 0.35, "amp.attack": 0.005, "amp.release": 0.35,
    },
    "Transistor Combo": {
        # Bright, square partials from the dividers with a quick vibrato, no cabinet.
        **bars("00 8808 004"), "organ.type": "Transistor", "vibrato.mode": "V2", "vibrato.rate": 6.0, "rotary.mode": "Off",
        "drive.amount": 0.2, "tone": 0.9, "click": 0.2, "amp.release": 0.03,
    },
    "Reggae Bubble": {
        # Short, round chords for off-beat stabs, the cabinet half wet.
        **bars("68 8400 000"), "vibrato.mode": "Off", "rotary.mode": "Slow", "rotary.mix": 0.6, "drive.amount": 0.1,
        "drive.output": 3.0, "tone": 0.6, "click": 0.3, "amp.release": 0.02,
    },
    "Theatre": {
        # Flute pipes with a deep, slow vibrato and a long release.
        **bars("80 8604 002"), "organ.type": "Pipe", "vibrato.mode": "V3", "vibrato.rate": 6.3, "rotary.mode": "Off",
        "drive.amount": 0.0, "tone": 0.6, "click": 0.1, "amp.attack": 0.008, "amp.release": 0.3,
    },
    "Bright Mellow": {
        # A mellow body with a sparkling top: sixteen and eight foot, then the 1' alone.
        **bars("80 8000 008"), "vibrato.mode": "C2", "rotary.mode": "Slow", "drive.amount": 0.1, "tone": 0.9,
    },
    "Slow Rotary Pad": {
        # A swelling, soft registration through a wide, deep slow cabinet.
        **bars("87 8503 000"), "amp.attack": 0.4, "amp.release": 1.0, "vibrato.mode": "C1", "rotary.mode": "Slow",
        "rotary.depth": 0.8, "rotary.width": 1.0, "click": 0.0, "drive.amount": 0.0, "tone": 0.55,
    },
    "Fast Rotary Lead": {
        # Driven and bright, the percussion on the attack, the cabinet at full speed.
        **bars("88 8864 000"), "perc.on": 1, "perc.harmonic": "Third", "perc.decay": "Fast", "drive.amount": 0.55,
        "tone": 0.85, "click": 0.55, "vibrato.mode": "Off", "rotary.mode": "Fast",
    },
    "Bass Pedal": {
        # One note at a time, sixteen and eight foot, dark, mostly dry.
        **bars("80 6000 000"), "voice.mode": "Mono", "vibrato.mode": "Off", "rotary.mode": "Slow", "rotary.mix": 0.35,
        "rotary.balance": 0.3, "drive.amount": 0.15, "drive.output": 3.0, "tone": 0.4, "click": 0.35, "amp.release": 0.06,
    },
    "Sixteen and Eight": {
        # The two foundation drawbars alone, deep chorus and slow cabinet.
        **bars("80 8000 000"), "vibrato.mode": "C3", "rotary.mode": "Slow",
    },
    "Funk Comp": {
        # Clicky, percussive chords with the second-harmonic percussion, short release, a bit of grit.
        **bars("88 8800 000"), "perc.on": 1, "perc.harmonic": "Second", "perc.decay": "Fast", "drive.amount": 0.35,
        "tone": 0.85, "click": 0.65, "vibrato.mode": "Off", "rotary.mode": "Slow", "amp.release": 0.02,
    },
    "Gentle Hymn": {
        # Soft eight and four foot pipes, a slow onset and a long release.
        **bars("00 8600 000"), "organ.type": "Pipe", "vibrato.mode": "Off", "rotary.mode": "Off", "drive.amount": 0.0,
        "tone": 0.5, "click": 0.15, "amp.attack": 0.03, "amp.release": 0.4,
    },
    "Blues Shuffle": {
        # A thick registration with the fast third percussion, warm drive and the chorus.
        **bars("88 8400 000"), "perc.on": 1, "perc.harmonic": "Third", "perc.decay": "Fast", "drive.amount": 0.4,
        "click": 0.5, "vibrato.mode": "C3", "rotary.mode": "Slow",
    },
    "Soul Ballad": {
        # Sixteen and eight foot with a touch of 1', the chorus and the slow cabinet.
        **bars("80 8000 004"), "vibrato.mode": "C3", "rotary.mode": "Slow", "drive.amount": 0.15, "tone": 0.65,
        "amp.release": 0.06,
    },
    "Screaming Lead": {
        # All drawbars into a fully driven preamp, the cabinet spinning up fast.
        **bars("88 8888 888"), "drive.amount": 0.88, "tone": 0.75, "click": 0.4, "vibrato.mode": "Off",
        "rotary.mode": "Fast", "rotary.accel": 0.5,
    },
    "Pipe Principal": {
        # Eight, four and two foot pipes, a clear chiff on each note.
        **bars("00 8605 000"), "organ.type": "Pipe", "vibrato.mode": "Off", "rotary.mode": "Off", "drive.amount": 0.0,
        "tone": 0.7, "click": 0.55, "amp.release": 0.3,
    },
    "Combo Vibrato": {
        # Transistor partials with the deepest vibrato and an open tone.
        **bars("00 8880 000"), "organ.type": "Transistor", "vibrato.mode": "V3", "rotary.mode": "Off",
        "drive.amount": 0.1, "tone": 0.95, "click": 0.15,
    },
    "Grand Cathedral": {
        # Nearly every pipe drawn, a long release as in a large room.
        **bars("84 8868 468"), "organ.type": "Pipe", "vibrato.mode": "Off", "rotary.mode": "Off", "drive.amount": 0.0,
        "tone": 0.8, "click": 0.35, "amp.release": 0.8,
    },
    "Eerie Swell": {
        # Only the mutations and the 1', swelling in and out through a deep vibrato and the fast cabinet.
        **bars("00 0080 888"), "amp.attack": 0.9, "amp.release": 1.6, "vibrato.mode": "V3", "rotary.mode": "Fast",
        "rotary.depth": 0.8, "click": 0.0, "drive.amount": 0.0, "tone": 0.6,
    },
    "Upper Partials Chime": {
        # The top four drawbars with the slow third percussion ringing over them.
        **bars("00 0008 888"), "perc.on": 1, "perc.harmonic": "Third", "perc.decay": "Slow", "perc.volume": "Normal",
        "vibrato.mode": "Off", "rotary.mode": "Slow", "click": 0.2, "tone": 0.7, "amp.release": 0.6,
    },
}
