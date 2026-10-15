"""Factory presets of Résonateurs, Corps, Peigne.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name.
"""
from __future__ import annotations


def values(prefix: str, **settings: float) -> dict[str, float]:
    """A preset's values: each setting's binding suffix under the effect's prefix."""
    return {f"{prefix}.{name}": float(value) for name, value in settings.items()}


# Résonateurs: its modes, and its chords as the intervals II..V over the root.
STRINGS, PLUCKED, METAL = range(3)
MAJOR = (4, 7, 12, 16)
MINOR = (3, 7, 12, 15)
FIFTH = (7, 12, 19, 24)
OCTAVES = (12, -12, 24, -24)
SUS4 = (5, 7, 12, 17)
SEVENTH = (4, 7, 10, 16)
NINTH = (4, 7, 10, 14)


def chord(intervals: tuple[int, int, int, int], **settings: float) -> dict[str, float]:
    """A Résonateurs preset on a chord."""
    second, third, fourth, fifth = intervals
    return values("resonateurs", interval2=second, interval3=third, interval4=fourth, interval5=fifth, **settings)


# Corps: its objects.
BEAM, MARIMBA, STRING, MEMBRANE, PLATE, PIPE, TUBE = range(7)

# Peigne: its modes and the sync's note values (0 is free): 11 1/2, 12 1/1.
FEEDBACK, FEEDFORWARD = range(2)
SYNC_2, SYNC_BAR = 11, 12

PRESETS: dict[str, dict[str, dict[str, float]]] = {
    "resonateurs": {
        "Major Chord Strings": chord(MAJOR, note=48, decay=0.75, colour=0.55, mix=0.5, mode=STRINGS),
        "Minor Chord Strings": chord(MINOR, note=45, decay=0.75, colour=0.5, mix=0.5, mode=STRINGS),
        "Open Fifths": chord(FIFTH, note=43, decay=0.8, colour=0.6, mix=0.45, width=0.7),
        "Octave Drone": chord(OCTAVES, note=50, decay=0.95, colour=0.4, mix=0.35, width=0.8, gain4=0.45, gain5=0.35),
        "Plucked Harp": chord(MAJOR, note=55, decay=0.6, colour=0.7, mix=0.45, mode=PLUCKED, tone=0.2),
        "Sympathetic Strings": chord((7, 12, 14, 19), note=50, decay=0.9, colour=0.85, mix=0.3, tone=0.4, mode=STRINGS),
        "Bell Tower": chord(FIFTH, note=60, decay=0.85, colour=0.75, mix=0.4, mode=METAL, width=0.9),
        "Gamelan Metal": chord((2, 7, 9, 14), note=55, decay=0.7, colour=0.65, mix=0.5, mode=METAL),
        "Suspended Shimmer": chord(SUS4, note=57, decay=1.0, colour=0.8, mix=0.35, tone=0.5, width=1.0),
        "Dark Ninth": chord(NINTH, note=46, decay=0.85, colour=0.15, mix=0.45, tone=-0.4),
        "Seventh Glow": chord(SEVENTH, note=48, decay=0.8, colour=0.6, mix=0.4, width=0.75),
        "Tuned Drum Ring": chord(FIFTH, note=40, decay=0.25, colour=0.5, mix=0.3, mode=STRINGS, gain4=0.3, gain5=0.2),
        "Metal Rod Pair": chord((12, 19, 24, -12), note=52, decay=0.75, colour=0.9, mix=0.4, mode=METAL, tone=0.3),
    },
    "corps": {
        "Wooden Box": values("corps", type=PLATE, tune=180, material=0.1, decay=0.3, position=0.4, mix=0.35),
        "Glass Bowl": values("corps", type=PLATE, tune=520, material=1.0, decay=0.9, inharmonic=0.2, position=0.2, mix=0.4),
        "Metal Plate Hit": values("corps", type=PLATE, tune=140, material=0.95, decay=0.75, position=0.6, mix=0.45, width=0.8),
        "Marimba Bar": values("corps", type=MARIMBA, tune=262, material=0.15, decay=0.45, position=0.1, mix=0.5),
        "Steel Beam": values("corps", type=BEAM, tune=110, material=0.9, decay=0.7, position=0.5, mix=0.4),
        "Guitar Body String": values("corps", type=STRING, tune=110, material=0.3, decay=0.55, position=0.8, mix=0.35),
        "Tom Skin": values("corps", type=MEMBRANE, tune=120, material=0.3, decay=0.4, position=0.35, mix=0.4),
        "Big Drum Membrane": values("corps", type=MEMBRANE, tune=60, material=0.4, decay=0.55, position=0.25, mix=0.45, lowCut=30),
        "Organ Pipe": values("corps", type=PIPE, tune=220, material=0.7, decay=0.6, position=0.2, mix=0.4),
        "Hollow Tube": values("corps", type=TUBE, tune=160, material=0.5, decay=0.5, position=0.3, mix=0.4),
        "Bamboo Tube": values("corps", type=TUBE, tune=330, material=0.1, decay=0.35, position=0.1, mix=0.4),
        "Wide Bell": values("corps", type=BEAM, tune=300, material=1.0, decay=0.85, inharmonic=0.4, width=1.0, mix=0.35),
    },
    "peigne": {
        "Tuned Comb": values("peigne", frequency=220, feedback=0.85, damping=9000, mix=0.5),
        "Octave Down Comb": values("peigne", frequency=220, feedback=-0.85, damping=8000, mix=0.5),
        "Flanged Comb": values("peigne", frequency=800, feedback=0.7, lfoDepth=0.6, lfoRate=0.2, spread=0.3, mix=0.5),
        "Metallic Ring": values("peigne", frequency=1200, feedback=0.95, damping=16000, mix=0.4),
        "Dark Resonance": values("peigne", frequency=110, feedback=0.92, damping=1500, mix=0.45),
        "Feedforward Notches": values("peigne", mode=FEEDFORWARD, frequency=300, feedback=0.9, mix=1.0),
        "Phase Sweep Notches": values("peigne", mode=FEEDFORWARD, frequency=500, feedback=0.8, lfoDepth=0.8, lfoRate=0.15, mix=1.0),
        "Synced Sweep": values("peigne", frequency=400, feedback=0.8, lfoDepth=0.7, sync=SYNC_2, spread=0.5, mix=0.6),
        "Wide Stereo Comb": values("peigne", frequency=330, feedback=0.75, spread=1.0, mix=0.5),
        "Robot Voice": values("peigne", frequency=150, feedback=0.9, damping=6000, mix=0.7),
        "Hollow Tube Echo": values("peigne", frequency=70, feedback=-0.9, damping=3000, mix=0.5),
        "Fast Wobble Comb": values("peigne", frequency=600, feedback=0.85, lfoDepth=0.25, lfoRate=6.0, mix=0.6),
        "Bar Long Drift": values("peigne", frequency=260, feedback=0.88, lfoDepth=0.5, sync=SYNC_BAR, spread=0.6, mix=0.5),
    },
}
