"""Factory presets of Harmonie, Justesse.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name.
"""
from __future__ import annotations


def harmonie(**settings: float) -> dict[str, float]:
    """A Harmonie preset's values: each setting's binding suffix under "harmonie"."""
    return {f"harmonie.{name}": float(value) for name, value in settings.items()}


def justesse(**settings: float) -> dict[str, float]:
    """A Justesse preset's values: each setting's binding suffix under "justesse"."""
    return {f"justesse.{name}": float(value) for name, value in settings.items()}


# Harmonie's modes (PitchDesign.h): grains for chords, the voice kept, attacks kept.
SMOOTH, VOICE, PERCUSSIVE = range(3)

# Justesse's keys (C = 0) and scales (PitchDesign.h).
C, C_SHARP, D, D_SHARP, E, F, F_SHARP, G, G_SHARP, A, A_SHARP, B = range(12)
(CHROMATIC, MAJOR, NATURAL_MINOR, HARMONIC_MINOR, MELODIC_MINOR, MAJOR_PENTATONIC, MINOR_PENTATONIC, BLUES, DORIAN, PHRYGIAN,
 LYDIAN, MIXOLYDIAN) = range(12)

PRESETS: dict[str, dict[str, dict[str, float]]] = {
    "harmonie": {
        "Octave Up": harmonie(mode=VOICE, shiftA=12, levelA=0.8, levelB=0, dry=1),
        "Octave Down": harmonie(mode=VOICE, shiftA=-12, levelA=0.8, levelB=0, dry=1),
        "Fifth Harmony": harmonie(mode=VOICE, shiftA=7, levelA=0.7, levelB=0, dry=1),
        "Major Third Above": harmonie(mode=VOICE, shiftA=4, levelA=0.7, levelB=0, dry=1),
        "Minor Third Above": harmonie(mode=VOICE, shiftA=3, levelA=0.7, levelB=0, dry=1),
        "Third and Fifth": harmonie(mode=VOICE, shiftA=4, levelA=0.6, panA=-0.45, shiftB=7, levelB=0.5, panB=0.45, dry=1,
                                    output=-3),
        "Octave Sandwich": harmonie(mode=VOICE, shiftA=12, levelA=0.45, panA=-0.5, shiftB=-12, levelB=0.5, panB=0.5, dry=1,
                                    output=-3),
        "Slapback Fourth": harmonie(mode=VOICE, shiftA=5, levelA=0.6, levelB=0, dry=1, delay=120, feedback=0.25),
        "Wide Doubler": harmonie(mode=SMOOTH, window=40, shiftA=0, fineA=9, levelA=0.7, panA=-0.85, shiftB=0, fineB=-9, levelB=0.7,
                                 panB=0.85, dry=1, delay=14, output=-3),
        "Shimmer Fifths": harmonie(mode=SMOOTH, window=80, shiftA=12, levelA=0.5, panA=-0.6, shiftB=19, levelB=0.35, panB=0.6, dry=1,
                                   delay=250, feedback=0.45, output=-3),
        "Sub Octave Bass": harmonie(mode=SMOOTH, window=90, shiftA=-12, levelA=0.9, levelB=0, dry=1),
        "Chipmunk": harmonie(mode=SMOOTH, window=35, shiftA=12, levelA=1, levelB=0, dry=0),
        "Giant Voice": harmonie(mode=SMOOTH, window=50, shiftA=-10, levelA=1, levelB=0, dry=0),
        "Drums Tuned Up": harmonie(mode=PERCUSSIVE, window=30, shiftA=5, levelA=1, levelB=0, dry=0),
    },
    "justesse": {
        "Natural Correction": justesse(speed=60, amount=0.9, humanize=0.6, scale=CHROMATIC),
        "Gentle Chromatic": justesse(speed=120, amount=0.7, humanize=0.8, scale=CHROMATIC),
        "Fast Pop Tune": justesse(speed=15, amount=1, humanize=0.3, key=C, scale=MAJOR),
        "Hard Tune C Major": justesse(speed=0, amount=1, humanize=0, key=C, scale=MAJOR),
        "Hard Tune C Minor": justesse(speed=0, amount=1, humanize=0, key=C, scale=NATURAL_MINOR),
        "Hard Tune A Minor": justesse(speed=0, amount=1, humanize=0, key=A, scale=NATURAL_MINOR),
        "Robot Chromatic": justesse(speed=0, amount=1, humanize=0, scale=CHROMATIC),
        "Live Vocal Safety": justesse(speed=40, amount=0.8, humanize=0.7, sensitivity=0.35, scale=CHROMATIC),
        "Half Correction": justesse(speed=30, amount=0.5, humanize=0.4, scale=CHROMATIC),
        "Low Voice": justesse(speed=50, humanize=0.5, lowPitch=50, highPitch=500, scale=CHROMATIC),
        "High Voice": justesse(speed=50, humanize=0.5, lowPitch=150, highPitch=1200, scale=CHROMATIC),
        "Blues in E": justesse(speed=80, amount=0.9, humanize=0.7, key=E, scale=BLUES),
        "Octave Up Tuned": justesse(speed=20, humanize=0.3, transpose=12, scale=CHROMATIC),
        "Octave Down Tuned": justesse(speed=20, humanize=0.3, transpose=-12, scale=CHROMATIC),
    },
}
