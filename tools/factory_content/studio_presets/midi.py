"""Factory presets of the MIDI effects: Arpège, Accords, Gamme, Écho de notes,
Aléatoire, Durée, Séquenceur, Roulement.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name.
"""
from __future__ import annotations


def values(prefix: str, **settings: float) -> dict[str, float]:
    """A preset's values: each setting's binding suffix under the effect's prefix."""
    return {f"{prefix}.{name}": float(value) for name, value in settings.items()}


def arp(**settings: float) -> dict[str, float]:
    return values("arpege", **settings)


def chords(**settings: float) -> dict[str, float]:
    return values("accords", **settings)


def scale(**settings: float) -> dict[str, float]:
    return values("gamme", **settings)


def echoes(**settings: float) -> dict[str, float]:
    return values("echonotes", **settings)


def chance(**settings: float) -> dict[str, float]:
    return values("alea", **settings)


def length(**settings: float) -> dict[str, float]:
    return values("duree", **settings)


def repeat(**settings: float) -> dict[str, float]:
    return values("roulement", **settings)


def sequence(notes: list[int], accents: list[int] | None = None, rests: list[int] | None = None,
             short: list[int] | None = None, **settings: float) -> dict[str, float]:
    """A sequencer pattern: one note offset a step (from the root), the
    accented steps louder, the rests off, the short steps half a step."""
    result = values("sequenceur", steps=len(notes), **settings)
    for index, note in enumerate(notes):
        step = index + 1
        result[f"sequenceur.step{step}On"] = 0.0 if rests and step in rests else 1.0
        result[f"sequenceur.step{step}Note"] = float(note)
        result[f"sequenceur.step{step}Velocity"] = 124.0 if accents and step in accents else 92.0
        result[f"sequenceur.step{step}Length"] = 0.5 if short and step in short else 1.0
    return result


# The note values a sync setting picks (EffectCatalogue.h: noteDivisions,
# 0 = free time).
FREE, N32, N16T, N16, N8T, N16D, N8, N4T, N8D, N4, N4D, N2, N1 = range(13)
# Arpège's ways.
UP, DOWN, UP_DOWN, DOWN_UP, AS_PLAYED, RANDOM, CHORD, CONVERGE, DIVERGE = range(9)
AS_PLAYED_VELOCITY, FIXED_VELOCITY, ACCENTED = range(3)
# Accords' chord kinds and strum ways.
MAJOR, MINOR, FIFTH, SUS2, SUS4, SEVENTH, MAJOR7, MINOR7, NINTH, ADD9, OCTAVES, CUSTOM = range(12)
STRUM_UP, STRUM_DOWN, STRUM_ALTERNATE = range(3)
# The scales (MidiEffectsDesign.h: scaleShapes) and the keys.
CHROMATIC, MAJOR_SCALE, NATURAL_MINOR, HARMONIC_MINOR, MELODIC_MINOR, MAJOR_PENTA, MINOR_PENTA, BLUES, DORIAN, PHRYGIAN, \
    LYDIAN, MIXOLYDIAN = range(12)
C, C_SHARP, D, D_SHARP, E, F, F_SHARP, G, G_SHARP, A, A_SHARP, B = range(12)
# Gamme's ways with notes outside the scale.
NEAREST, ABOVE, BELOW, REMOVE = range(4)
# Durée's ways.
FIXED_TIME, NOTE_VALUE, STRETCHED, LEGATO = range(4)
# Séquenceur's ways and directions.
HELD_KEY, WITH_TRANSPORT = range(2)
FORWARD, BACKWARD, PING_PONG, RANDOM_ORDER = range(4)

PRESETS: dict[str, dict[str, dict[str, float]]] = {
    "arpege": {
        "Up Sixteenths": arp(rate=N16, mode=UP),
        "Down Sixteenths": arp(rate=N16, mode=DOWN),
        "Up and Down Two Octaves": arp(rate=N16, mode=UP_DOWN, octaves=2),
        "Eighth Note Pulse": arp(rate=N8, mode=UP, gate=0.5),
        "Triplet Rise": arp(rate=N8T, mode=UP, octaves=2),
        "Random Sparkle": arp(rate=N16, mode=RANDOM, octaves=3, gate=0.3),
        "Chord Stabs": arp(rate=N8, mode=CHORD, gate=0.25),
        "Swung Sixteenths": arp(rate=N16, mode=UP_DOWN, swing=0.3),
        "Accented Groove": arp(rate=N16, mode=UP, velocityMode=ACCENTED),
        "Fast Thirty-Seconds": arp(rate=N32, mode=UP_DOWN, gate=0.4, octaves=2),
        "Converging Lines": arp(rate=N16, mode=CONVERGE, octaves=2),
        "Diverging Lines": arp(rate=N16, mode=DIVERGE, octaves=2),
        "Latched Pad Arp": arp(rate=N8, mode=AS_PLAYED, latch=1, gate=1.2),
        "Even Velocity Run": arp(rate=N16, mode=UP, velocityMode=FIXED_VELOCITY, velocity=100),
    },
    "accords": {
        "Major Triad": chords(chord=MAJOR),
        "Minor Triad": chords(chord=MINOR),
        "Power Fifth": chords(chord=FIFTH),
        "Suspended Second": chords(chord=SUS2),
        "Suspended Fourth": chords(chord=SUS4),
        "Dominant Seventh": chords(chord=SEVENTH),
        "Major Seventh": chords(chord=MAJOR7),
        "Minor Seventh": chords(chord=MINOR7),
        "Ninth": chords(chord=NINTH),
        "Added Ninth": chords(chord=ADD9),
        "Octave Doubler": chords(chord=OCTAVES),
        "Strummed Guitar": chords(chord=MAJOR, strum=40, strumMode=STRUM_UP),
        "Harp Roll Down": chords(chord=MAJOR7, strum=80, strumMode=STRUM_DOWN),
        "Open Voicing Pad": chords(chord=MAJOR7, spread=1),
        "First Inversion": chords(chord=MAJOR, inversion=1),
        "C Major Diatonic Sevenths": chords(chord=SEVENTH, key=C, scale=MAJOR_SCALE),
        "A Minor Diatonic Triads": chords(chord=MAJOR, key=A, scale=NATURAL_MINOR),
        "Fifth and Octave": chords(chord=CUSTOM, interval1=7, interval2=12, interval3=0),
        "Soft Added Notes": chords(chord=MAJOR, velocity=0.55),
    },
    "gamme": {
        "C Major": scale(key=C, scale=MAJOR_SCALE),
        "A Minor": scale(key=A, scale=NATURAL_MINOR),
        "A Harmonic Minor": scale(key=A, scale=HARMONIC_MINOR),
        "C Major Pentatonic": scale(key=C, scale=MAJOR_PENTA),
        "A Minor Pentatonic": scale(key=A, scale=MINOR_PENTA),
        "E Blues": scale(key=E, scale=BLUES),
        "D Dorian": scale(key=D, scale=DORIAN),
        "E Phrygian": scale(key=E, scale=PHRYGIAN),
        "F Lydian": scale(key=F, scale=LYDIAN),
        "G Mixolydian": scale(key=G, scale=MIXOLYDIAN),
        "C Major Scale Notes Only": scale(key=C, scale=MAJOR_SCALE, mode=REMOVE),
        "Diatonic Third Up": scale(key=C, scale=MAJOR_SCALE, degrees=2),
        "Octave Up": scale(scale=CHROMATIC, transpose=12),
        "Octave Down": scale(scale=CHROMATIC, transpose=-12),
    },
    "echo-notes": {
        "Eighth Echoes": echoes(sync=N8, repeats=3, decay=0.3),
        "Dotted Eighth Echoes": echoes(sync=N8D, repeats=4, decay=0.25),
        "Quarter Echoes": echoes(sync=N4, repeats=3, decay=0.3),
        "Sixteenth Flutter": echoes(sync=N16, repeats=8, decay=0.12),
        "Triplet Echoes": echoes(sync=N8T, repeats=5, decay=0.2),
        "Rising Fifths": echoes(sync=N8, pitch=7, repeats=4, decay=0.2),
        "Octave Cascade": echoes(sync=N4, pitch=12, repeats=3, decay=0.25),
        "Falling Steps": echoes(sync=N16, pitch=-1, repeats=12, decay=0.08),
        "Free Slapback": echoes(sync=FREE, time=90, repeats=1, decay=0.4),
        "Long Free Trail": echoes(sync=FREE, time=600, repeats=8, decay=0.12),
    },
    "alea": {
        "Subtle Humanise": chance(timing=6, velocity=0.08),
        "Loose Player": chance(timing=15, velocity=0.18),
        "Unsteady Drummer": chance(timing=35, velocity=0.3),
        "Velocity Only": chance(timing=0, velocity=0.25),
        "Sparse Notes": chance(chance=0.6, velocity=0.1),
        "Few Dropped Notes": chance(chance=0.85, velocity=0.1),
        "Pitch Wander": chance(pitchChance=0.3, pitchRange=2, velocity=0.1),
        "Wide Pitch Scatter": chance(pitchChance=0.5, pitchRange=12, velocity=0.15),
        "Glitchy Chaos": chance(timing=40, chance=0.5, pitchChance=0.5, pitchRange=7, velocity=0.4),
    },
    "duree": {
        "Short Staccato": length(mode=FIXED_TIME, length=60),
        "Fixed Quarter Second": length(mode=FIXED_TIME, length=250),
        "Long Hold": length(mode=FIXED_TIME, length=1500),
        "Sixteenth Notes": length(mode=NOTE_VALUE, sync=N16),
        "Eighth Notes": length(mode=NOTE_VALUE, sync=N8),
        "Quarter Notes": length(mode=NOTE_VALUE, sync=N4),
        "Half Notes": length(mode=NOTE_VALUE, sync=N2),
        "Twice as Long": length(mode=STRETCHED, factor=2),
        "Half Again Longer": length(mode=STRETCHED, factor=1.5),
        "Legato Glue": length(mode=LEGATO),
    },
    "sequenceur": {
        "Octave Bounce": sequence([0, 12, 0, 12, 0, 12, 0, 12], accents=[1, 5]),
        "Minor Arp Line": sequence([0, 3, 7, 12, 7, 3, 0, -5], accents=[1]),
        "Major Climb": sequence([0, 4, 7, 11, 12, 11, 7, 4], accents=[1, 5]),
        "Fifths Drive": sequence([0, 7, 0, 7, 12, 7, 0, 7], accents=[1, 5], short=[2, 4, 6, 8]),
        "Offbeat Pulse": sequence([0, 0, 0, 0, 0, 0, 0, 0], rests=[1, 3, 5, 7], short=[2, 4, 6, 8]),
        "Eighth Note Bass": sequence([0, 0, 12, 0, 0, 10, 0, 7], accents=[1], short=[2, 5], rate=N8),
        "Acid Sixteen": sequence([0, 0, 12, 0, 3, 0, 15, 0, 0, 10, 0, 12, 7, 0, 3, 5],
                                 accents=[1, 4, 7, 11, 14], rests=[6, 13], short=[2, 5, 8, 10, 16], gate=0.6),
        "Ping-Pong Scale": sequence([0, 2, 3, 5, 7, 8, 10, 12], direction=PING_PONG),
        "Random Pentatonic": sequence([0, 3, 5, 7, 10, 12, 15, 17], direction=RANDOM_ORDER),
        "Backward Descent": sequence([0, 2, 4, 5, 7, 9, 11, 12], direction=BACKWARD),
        "Swung Groove": sequence([0, 0, 7, 0, 10, 0, 7, 12], accents=[1, 5], short=[2, 6], swing=0.35),
        "Transport Locked Groove": sequence([0, 12, 7, 12, 0, 12, 10, 12, 0, 12, 7, 12, 3, 12, 5, 7],
                                            accents=[1, 9], mode=WITH_TRANSPORT),
    },
    "roulement": {
        "Sixteenth Roll": repeat(sync=N16),
        "Thirty-Second Roll": repeat(sync=N32),
        "Triplet Roll": repeat(sync=N16T),
        "Eighth Repeat": repeat(sync=N8),
        "Crescendo Roll": repeat(sync=N16, ramp=0.8),
        "Fading Roll": repeat(sync=N16, ramp=-0.7),
        "Short Gate Stutter": repeat(sync=N32, gate=0.25),
        "Free Buzz": repeat(sync=FREE, rate=12),
        "Fast Free Buzz": repeat(sync=FREE, rate=24, gate=0.4),
        "Rising Hat Roll": repeat(sync=N16T, ramp=0.4, gate=0.4),
    },
}
