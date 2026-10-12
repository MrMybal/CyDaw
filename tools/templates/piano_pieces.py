"""Four original solo-piano pieces for CyDAW's example projects, each written
here in a style a person asked for (none is a transcription; the themes,
harmonies and forms are this file's), and played rather than typed: the
score is written in beats, then a performance pass gives it the pedal, the
rubato, the melody voiced above the accompaniment, chords spread from the
bass up, dynamics shaped by phrase and the small unevenness of hands.

    15 "Calle Luna"        neo-classical, cinematic, a Latin tresillo groove
    16 "Greve de l'Ouest"  intimate, melancholic, flowing broken chords in 3/4
    17 "Neons"             dark and driving: pulsing octaves under a slow line
    18 "Cycles lents"      minimalist, three movements of repeated figures

Each piece function returns a Piece; build_v2_examples.py makes the project.
"""
from __future__ import annotations

import bisect
import math
import random
from dataclasses import dataclass, field

# ---- Notes -----------------------------------------------------------------------------------
NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def n(name):
    """'F#5' -> 78, 'Bb3' -> 58 (C4 = 60)."""
    pitch = NAMES[name[0]]
    rest = name[1:]
    while rest and rest[0] in "#b":
        pitch += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    return pitch + 12 * (int(rest) + 1)


def notes(text):
    return [n(word) for word in text.split()]


# ---- A score, and how it is played --------------------------------------------------------------
@dataclass
class Note:
    start: float
    length: float
    pitch: int
    level: float
    role: str  # "melody", "inner", "bass"


@dataclass
class Piece:
    title: str
    slug: str
    tempo: float
    numerator: int
    denominator: int
    instrument: str
    reverb: float
    gain_db: float
    events: list
    sections: list = field(default_factory=list)  # (name, first bar, bars)


class Score:
    def __init__(self, beats_per_bar):
        self.bar = beats_per_bar
        self.notes = []
        # Pedal: spans held down, with the beats it is changed at.
        self.pedal_spans = []
        self.pedal_changes = []
        # Dynamics (beat, level) and tempo (beat, factor), interpolated.
        self.dynamics = [(0.0, 0.6)]
        self.tempo = [(0.0, 1.0)]
        # Held moments: (beat, extra beats of time).
        self.fermatas = []

    def at(self, bar, beat=0.0):
        return bar * self.bar + beat

    def add(self, start, length, pitch, level, role="inner"):
        if 21 <= pitch <= 108 and length > 0:
            self.notes.append(Note(start, length, pitch, level, role))

    def chord(self, start, length, pitches, level, role="inner"):
        for pitch in pitches:
            self.add(start, length, pitch, level, role)

    def line(self, bar, items, level, role="melody", octave=0, doubled=False):
        """A melody over a bar: (beat, length, 'C#5' or None for a rest)."""
        for beat, length, name in items:
            if name is None:
                continue
            pitch = n(name) + 12 * octave
            self.add(self.at(bar, beat), length, pitch, level, role)
            if doubled:
                self.add(self.at(bar, beat), length, pitch - 12, level * 0.8, "inner")

    def pedal(self, start, end, changes):
        """Pedal down from start to end, changed (lifted and pressed again) at
        each of changes."""
        self.pedal_spans.append((start, end))
        self.pedal_changes.extend(c for c in changes if start < c < end)

    def dynamic(self, beat, level):
        self.dynamics.append((beat, level))

    def pace(self, beat, factor):
        self.tempo.append((beat, factor))

    def hold(self, beat, extra):
        self.fermatas.append((beat, extra))


def interpolate(points, x):
    points = sorted(points)
    if x <= points[0][0]:
        return points[0][1]
    for (a, va), (b, vb) in zip(points, points[1:]):
        if x <= b:
            return va + (vb - va) * (x - a) / max(1e-9, b - a)
    return points[-1][1]


def perform(score, tempo, seed, roll=(0.004, 0.014), lead=0.018, jitter=0.006, pedal_lag=0.12, loudness=1.0):
    """The score as played: (pitch, start, length, velocity) in beats of the
    project's tempo. loudness scales every velocity."""
    chance = random.Random(seed)
    beat_seconds = 60.0 / tempo
    to_beats = lambda seconds: seconds / beat_seconds
    changes = sorted(score.pedal_changes)
    spans = sorted(score.pedal_spans)

    def pedalled_end(key_up):
        for start, end in spans:
            if start <= key_up < end:
                index = bisect.bisect_left(changes, key_up - 1e-6)
                following = [c for c in changes[index:] if c < end]
                return (following[0] if following else end) + pedal_lag
        return key_up

    # Sounding ends: the pedal holds what the hand let go.
    played = []
    for note in score.notes:
        played.append([note, note.start, max(note.start + note.length, pedalled_end(note.start + note.length))])
    # One key, one sound: a note struck again stops the last one just before.
    by_pitch = {}
    for item in sorted(played, key=lambda item: item[1]):
        last = by_pitch.get(item[0].pitch)
        if last is not None and last[2] > item[1] - 0.01:
            last[2] = max(last[1] + 0.03, item[1] - 0.01)
        by_pitch[item[0].pitch] = item
    # Chords: from the bass up, a few milliseconds apart; the melody a hair
    # ahead of its accompaniment; every onset a little uneven.
    onsets = {}
    for item in played:
        onsets.setdefault(round(item[1], 3), []).append(item)
    offsets = {}
    for start, group in onsets.items():
        group.sort(key=lambda item: item[0].pitch)
        spread = 0.0
        for item in group:
            offset = spread + chance.gauss(0.0, jitter)
            if item[0].role == "melody":
                offset -= lead
            offsets[id(item)] = offset
            spread += chance.uniform(*roll) if len(group) > 2 else 0.0
    # Rubato: score beats to played beats, through the tempo curve and the
    # held moments.
    step = 1.0 / 32.0
    length = max(item[2] for item in played) + 8.0
    grid = [0.0]
    position = 0.0
    fermatas = sorted(score.fermatas)
    for index in range(int(length / step) + 2):
        x = index * step
        position += step / max(0.2, interpolate(score.tempo, x + step * 0.5))
        for beat, extra in fermatas:
            if x <= beat < x + step:
                position += extra
        grid.append(position)

    def warp(beat):
        index = beat / step
        low = int(index)
        if low + 1 >= len(grid):
            return grid[-1] + (beat - low * step)
        return grid[low] + (grid[low + 1] - grid[low]) * (index - low)

    events = []
    for item in played:
        note, start, end = item
        level = note.level * interpolate(score.dynamics, start) * loudness
        # The dynamics mapped onto the keys from 0.3 up: a sampled piano's
        # softest layers are 30 dB down, a pianissimo must still be heard.
        raw = max(0.0, min(1.0, level * (1.0 + chance.gauss(0.0, 0.045))))
        velocity = 0.3 + 0.7 * raw
        begin = max(0.0, warp(start) + to_beats(offsets[id(item)]))
        finish = warp(end)
        events.append((note.pitch, round(begin, 5), round(max(0.04, finish - begin), 5), round(velocity, 4)))
    events.sort(key=lambda event: (event[1], event[0]))
    return events


# ---- 15: Calle Luna -------------------------------------------------------------------------------
# B minor, 92 BPM. A melody over a 3+3+2 left hand; it opens alone, finds its
# groove, climbs to a full-voiced climax in the relative major, breaks off,
# and comes back quietly up high.
LUNA_A = [  # Bm(add9) | Gmaj7 | Em7 | F#7sus4 F#7, twice
    [(0.5, 0.5, "B4"), (1, 0.5, "D5"), (1.5, 1.5, "F#5"), (3, 0.5, "E5"), (3.5, 0.5, "D5")],
    [(0, 1.5, "D5"), (1.5, 0.5, "B4"), (2, 0.5, "C#5"), (2.5, 1.5, "D5")],
    [(0, 1, "E5"), (1, 0.5, "G5"), (1.5, 1.5, "F#5"), (3, 0.5, "E5"), (3.5, 0.5, "D5")],
    [(0, 2, "C#5"), (2, 1, "A#4"), (3, 1, "C#5")],
    [(0.5, 0.5, "B4"), (1, 0.5, "D5"), (1.5, 1, "F#5"), (2.5, 0.5, "A5"), (3, 1, "B5")],
    [(0, 1.5, "A5"), (1.5, 0.5, "F#5"), (2, 2, "D5")],
    [(0, 0.5, "G5"), (0.5, 0.5, "F#5"), (1, 1, "E5"), (2, 0.5, "D5"), (2.5, 0.5, "E5"), (3, 1, "B4")],
    [(0, 1.5, "C#5"), (1.5, 0.5, "E5"), (2, 1, "A#4"), (3, 1, "F#4")],
]
LUNA_A_CHORDS = [("B", "B2", ["D4", "F#4", "C#5"]), ("G", "G2", ["B3", "D4", "F#4"]), ("E", "E2", ["G3", "B3", "D4"]),
                 ("F#", "F#2", ["B3", "C#4", "E4"])]
LUNA_B = [  # G | D/F# | Em7 | Bm/D | G | A | F#sus4 | F#7
    [(0, 1.5, "B5"), (1.5, 0.5, "A5"), (2, 1, "G5"), (3, 1, "D5")],
    [(0, 1.5, "F#5"), (1.5, 0.5, "E5"), (2, 0.5, "D5"), (2.5, 0.5, "E5"), (3, 1, "F#5")],
    [(0, 1.5, "G5"), (1.5, 0.5, "B5"), (2, 1, "A5"), (3, 0.5, "G5"), (3.5, 0.5, "F#5")],
    [(0, 3, "F#5"), (3, 0.5, "D5"), (3.5, 0.5, "E5")],
    [(0, 1.5, "B5"), (1.5, 1.5, "D6"), (3, 1, "B5")],
    [(0, 1.5, "C#6"), (1.5, 0.5, "B5"), (2, 1, "A5"), (3, 1, "E5")],
    [(0, 2, "B5"), (2, 1, "C#6"), (3, 1, "B5")],
    [(0, 3, "A#5"), (3, 0.5, "C#5"), (3.5, 0.5, "D5")],
]
LUNA_B_CHORDS = [("G2", ["B3", "D4", "G4"]), ("F#2", ["A3", "D4", "F#4"]), ("E2", ["G3", "B3", "D4"]),
                 ("D2", ["F#3", "B3", "D4"]), ("G2", ["B3", "D4", "G4"]), ("A2", ["C#4", "E4", "A4"]),
                 ("F#2", ["B3", "C#4", "F#4"]), ("F#2", ["A#3", "C#4", "E4"])]


def tresillo(score, bar, bass, level, full=True):
    """3+3+2 under a bar: the octave, the fifth, the octave again."""
    root = n(bass)
    score.chord(score.at(bar, 0), 1.5, [root - 12, root] if full else [root], level, "bass")
    score.add(score.at(bar, 1.5), 1.5, root + 7, level * 0.82, "bass")
    score.add(score.at(bar, 3), 1.0, root + (12 if full else 0), level * 0.9, "bass")


def calle_luna():
    s = Score(4)
    sections = []
    # Intro (bars 0-3): the opening phrase alone, an octave up, the bass
    # touched under it; held on the dominant.
    sections.append(("intro", 0, 4))
    for bar in range(2):
        s.line(bar, LUNA_A[bar], 0.5, octave=1)
        s.add(s.at(bar), 4, n(LUNA_A_CHORDS[bar][1]) - 12, 0.42, "bass")
        s.add(s.at(bar), 4, n(LUNA_A_CHORDS[bar][1]), 0.36, "bass")
    s.line(2, LUNA_A[2], 0.5, octave=1)
    s.chord(s.at(2), 4, notes("E2 B2 G3"), 0.36, "bass")
    s.chord(s.at(3), 4, notes("F#1 F#2 C#3 A#3 E4"), 0.4)
    s.line(3, [(0, 2, "C#6"), (2, 2, "A#5")], 0.48)
    for bar in range(4):
        s.pedal(s.at(bar), s.at(bar + 1) - 0.05, [])
    s.dynamic(0, 0.62)
    s.pace(0, 0.88)
    s.pace(s.at(3), 0.8)
    s.hold(s.at(3, 3.5), 1.5)

    # A (4-11): the groove comes in under the melody.
    # A' (12-19): the melody in octaves, chords between, growing.
    for section, first in (("A", 4), ("A'", 12)):
        sections.append((section, first, 8))
        for index in range(8):
            bar = first + index
            name, bass, upper = LUNA_A_CHORDS[index % 4]
            tresillo(s, bar, bass, 0.58 if section == "A" else 0.66)
            if index % 4 == 3:  # sus4 then its resolution
                s.chord(s.at(bar, 0.5), 1.5, notes("B3 C#4 E4"), 0.34)
                s.chord(s.at(bar, 2.5), 1.5, notes("A#3 C#4 E4"), 0.36)
            else:
                s.chord(s.at(bar, 0.5), 1.0, [n(x) for x in upper if n(x) < 66], 0.32)
                s.chord(s.at(bar, 2.5), 1.0, [n(x) for x in upper if n(x) < 66], 0.3)
            s.line(bar, LUNA_A[index], 0.74 if section == "A" else 0.8, doubled=section == "A'")
            changes = [s.at(bar, 2)] if index % 4 == 3 else []
            s.pedal(s.at(bar), s.at(bar + 1), changes + [s.at(bar, 0.08)])
    s.dynamic(s.at(4), 0.6)
    s.dynamic(s.at(11, 3), 0.66)
    s.dynamic(s.at(12), 0.68)
    s.dynamic(s.at(19, 3), 0.84)
    for first in (4, 8, 12, 16):
        s.pace(s.at(first), 1.0)
        s.pace(s.at(first + 2), 1.025)
        s.pace(s.at(first + 3, 2), 0.99)
        s.pace(s.at(first + 4) - 0.25, 0.9)

    # B (20-27): the relative major, octaves in both hands, full chords.
    sections.append(("B", 20, 8))
    for index in range(8):
        bar = 20 + index
        bass, upper = LUNA_B_CHORDS[index]
        root = n(bass)
        s.chord(s.at(bar, 0), 1.5, [root - 12, root], 0.78, "bass")
        s.chord(s.at(bar, 1.5), 1.5, [root - 5, root + 7], 0.66, "bass")
        s.chord(s.at(bar, 3), 1.0, [root - 12, root], 0.72, "bass")
        for item in LUNA_B[index]:
            beat, length, name = item
            top = n(name)
            s.add(s.at(bar, beat), length, top, 0.92, "melody")
            s.add(s.at(bar, beat), length, top - 12, 0.72, "inner")
            if length >= 1:
                fill = [p for p in (n(x) + 12 * k for x in upper for k in (0, 1)) if top - 12 < p < top - 2]
                s.chord(s.at(bar, beat), length, sorted(set(fill))[-2:], 0.5)
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(bar, 0.08)])
    s.dynamic(s.at(20), 0.86)
    s.dynamic(s.at(23, 3), 0.9)
    s.dynamic(s.at(24), 0.94)
    s.dynamic(s.at(26), 1.0)
    s.dynamic(s.at(27, 3), 0.86)
    s.pace(s.at(20), 1.02)
    s.pace(s.at(24), 1.03)
    s.pace(s.at(27), 0.97)
    s.pace(s.at(28) - 0.25, 0.86)

    # Bridge (28-31): the left hand stops; a falling arpeggio, slower and
    # slower, held at the end.
    sections.append(("bridge", 28, 4))
    bridge = [("G2", "B5 F#5 D5 B4 A4 F#4 D4 B3"), ("F#2", "A#5 E5 C#5 A#4 F#4 E4 C#4 A#3"),
              ("E2", "F#5 D5 B4 G4 F#4 D4 B3 G3"), ("F#2", "G5 E5 C#5 A#4 G4 E4 C#4 A#3")]
    for index, (bass, falling) in enumerate(bridge):
        bar = 28 + index
        s.add(s.at(bar), 4, n(bass), 0.5, "bass")
        s.add(s.at(bar), 4, n(bass) - 12, 0.44, "bass")
        for step, pitch in enumerate(notes(falling)):
            s.add(s.at(bar, step * 0.5), 0.5, pitch, 0.56 if step == 0 else 0.44 - step * 0.012,
                  "melody" if step == 0 else "inner")
        s.pedal(s.at(bar), s.at(bar + 1), [])
    s.dynamic(s.at(28), 0.62)
    s.dynamic(s.at(31, 3), 0.46)
    s.pace(s.at(28), 0.95)
    s.pace(s.at(31), 0.78)
    s.pace(s.at(32) - 0.1, 0.6)
    s.hold(s.at(31, 3.5), 2.0)

    # A'' (32-39): the melody back an octave up, quiet; the bass only touches.
    sections.append(("A''", 32, 8))
    for index in range(8):
        bar = 32 + index
        name, bass, upper = LUNA_A_CHORDS[index % 4]
        root = n(bass)
        s.add(s.at(bar, 0), 2.5, root, 0.46, "bass")
        s.add(s.at(bar, 1.5), 1.5, root + 7, 0.36, "bass")
        s.chord(s.at(bar, 2), 2, [n(x) for x in upper], 0.26)
        s.line(bar, LUNA_A[index], 0.62, octave=1)
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(bar, 0.08)] + ([s.at(bar, 2)] if index % 4 == 3 else []))
    s.dynamic(s.at(32), 0.5)
    s.dynamic(s.at(36), 0.56)
    s.dynamic(s.at(39, 3), 0.44)
    s.pace(s.at(32), 0.96)
    s.pace(s.at(35, 3), 0.9)
    s.pace(s.at(36), 0.97)
    s.pace(s.at(39, 2), 0.86)

    # Coda (40-43): the first notes once more, then the opening chord, held.
    sections.append(("coda", 40, 4))
    s.line(40, [(0.5, 0.5, "B5"), (1, 0.5, "D6"), (1.5, 2.5, "F#6")], 0.5)
    s.chord(s.at(40), 4, notes("B1 B2 F#3"), 0.4, "bass")
    s.chord(s.at(41), 4, notes("G1 G2 D3 B3 F#4"), 0.36)
    s.line(41, [(0, 1.5, "D6"), (1.5, 0.5, "B5"), (2, 2, "C#6")], 0.46)
    s.chord(s.at(42), 2, notes("E2 B2 G3 D4"), 0.34)
    s.chord(s.at(42, 2), 2, notes("F#2 C#3 A#3 E4"), 0.34)
    s.line(42, [(0, 2, "E6"), (2, 2, "A#5")], 0.42)
    s.chord(s.at(43), 8, notes("B0 B1 F#2 D3 C#4 F#4 B4 D5 F#5"), 0.4)
    s.add(s.at(43, 0.25), 8, n("B5"), 0.38, "melody")
    for bar in range(40, 43):
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(42, 2)])
    s.pedal(s.at(43), s.at(45), [])
    s.dynamic(s.at(40), 0.5)
    s.dynamic(s.at(43), 0.42)
    s.pace(s.at(40), 0.88)
    s.pace(s.at(43), 0.7)
    s.hold(s.at(42, 3.5), 1.0)
    events = perform(s, 92, seed=15)
    return Piece("Calle Luna", "15-calle-luna", 92, 4, 4, "grand-steinway", 0.2, -2.0, events, sections)


# ---- 16: Greve de l'Ouest -------------------------------------------------------------------------
# A minor, 3/4, 66 BPM. Broken chords flowing in the left hand, a plain tune
# of repeated notes over them; it opens out in the middle, then goes back to
# almost nothing.
GREVE_CHORDS = {  # root (bass), third, fifth above, colour
    "Am": ("A2", 3, 7, 14), "Fmaj7": ("F2", 4, 7, 11), "C": ("C3", 4, 7, 14), "G/B": ("B1", 3, 8, 12),
    "Dm7": ("D2", 3, 7, 10), "E": ("E2", 4, 7, 12), "Esus": ("E2", 5, 7, 12), "Am/C": ("C3", 4, 9, 12),
    "E7": ("E2", 4, 7, 10), "Bm7b5": ("B1", 3, 6, 10),
}
GREVE_A = [
    ("Am", [(0, 1, "E5"), (1, 0.5, "E5"), (1.5, 0.5, "D5"), (2, 1, "C5")]),
    ("Fmaj7", [(0, 1, "C5"), (1, 2, "A4")]),
    ("C", [(0, 1, "G4"), (1, 1, "C5"), (2, 1, "E5")]),
    ("G/B", [(0, 2, "D5"), (2, 1, "B4")]),
    ("Am", [(0, 1, "E5"), (1, 0.5, "E5"), (1.5, 0.5, "F5"), (2, 1, "E5")]),
    ("Fmaj7", [(0, 1.5, "A5"), (1.5, 0.5, "G5"), (2, 1, "F5")]),
    ("Dm7", [(0, 1, "F5"), (1, 0.5, "E5"), (1.5, 0.5, "D5"), (2, 1, "C5")]),
    ("E", [(0, 2, "B4"), (2, 1, "G#4")]),
]
GREVE_B = [
    ("Dm7", [(0, 2, "A5"), (2, 1, "F5")]),
    ("Am/C", [(0, 1.5, "E5"), (1.5, 0.5, "D5"), (2, 1, "C5")]),
    ("Fmaj7", [(0, 1, "C6"), (1, 1, "A5"), (2, 1, "F5")]),
    ("E7", [(0, 2, "G#5"), (2, 1, "E5")]),
    ("Dm7", [(0, 1, "F5"), (1, 1, "A5"), (2, 1, "D6")]),
    ("Am", [(0, 2, "C6"), (2, 0.5, "B5"), (2.5, 0.5, "A5")]),
    ("Bm7b5", [(0, 1, "A5"), (1, 1, "F5"), (2, 1, "D5")]),
    ("E", [(0, 3, "E5")]),
]


def flowing(score, bar, chord, level, thin=False):
    """Six eighths: the bass, its fifth, the third an octave up, the fifth,
    the colour, the fifth."""
    bass, third, fifth, colour = GREVE_CHORDS[chord]
    root = n(bass)
    pattern = [root, root + fifth, root + 12 + third, root + fifth, root + colour + (12 if colour < 12 else 0), root + fifth]
    if thin:
        pattern = [root, None, root + 12 + third, None, root + fifth + 12, None]
    for step, pitch in enumerate(pattern):
        if pitch is not None:
            accent = 1.0 if step == 0 else 0.78 if step == 2 else 0.7
            score.add(score.at(bar, step * 0.5), 0.5, pitch, level * accent, "bass" if step == 0 else "inner")


def greve_ouest():
    s = Score(3)
    sections = [("intro", 0, 2), ("A", 2, 8), ("A'", 10, 8), ("B", 18, 8), ("A''", 26, 8), ("outro", 34, 4)]
    for bar in range(2):
        flowing(s, bar, "Am", 0.44)
    for first, variant in ((2, "A"), (10, "A'"), (18, "B"), (26, "A''")):
        tune = GREVE_B if variant == "B" else GREVE_A
        for index, (chord, line) in enumerate(tune):
            bar = first + index
            flowing(s, bar, chord, {"A": 0.44, "A'": 0.5, "B": 0.56, "A''": 0.38}[variant], thin=variant == "A''")
            if variant == "A''":
                s.line(bar, line, 0.5, octave=1)
            else:
                s.line(bar, line, {"A": 0.64, "A'": 0.7, "B": 0.78}[variant], doubled=variant != "A")
            # A grace note before the long notes of the second time through.
            if variant == "A'" and index in (1, 5):
                beat, length, name = line[0]
                s.add(s.at(bar, beat) - 0.12, 0.12, n(name) + 2, 0.42, "inner")
    for bar in range(0, 38):
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(bar, 0.06)])
    # Outro: the first chord, the second, then the first with its ninth, held.
    flowing(s, 34, "Am", 0.4)
    s.line(34, [(0, 1, "E5"), (1, 0.5, "E5"), (1.5, 0.5, "D5"), (2, 1, "C5")], 0.5)
    flowing(s, 35, "Fmaj7", 0.36)
    s.line(35, [(0, 3, "A4")], 0.46)
    s.chord(s.at(36), 6, notes("A1 A2 E3 C4 E4 B4"), 0.34)
    s.add(s.at(36, 0.6), 5.4, n("E5"), 0.36, "melody")
    s.pedal(s.at(36), s.at(38) + 0.5, [])
    # Dynamics: quiet, opening out in B, back to almost nothing.
    for beat, level in ((0, 0.58), (s.at(10), 0.62), (s.at(17, 2), 0.7), (s.at(18), 0.72), (s.at(22), 0.82),
                        (s.at(25, 2), 0.68), (s.at(26), 0.52), (s.at(33, 2), 0.46), (s.at(36), 0.42)):
        s.dynamic(beat, level)
    # Rubato: each four bars lean forward and settle; the ends of sections
    # breathe; the last bars slow right down.
    for first in range(2, 34, 4):
        s.pace(s.at(first), 0.98)
        s.pace(s.at(first + 2), 1.035)
        s.pace(s.at(first + 3, 2), 0.95)
    for end in (10, 18, 26, 34):
        s.pace(s.at(end) - 0.5, 0.82)
        s.pace(s.at(end) + 0.25, 0.97)
        s.hold(s.at(end) - 0.2, 0.35)
    s.pace(s.at(34), 0.9)
    s.pace(s.at(36), 0.7)
    s.hold(s.at(35, 2.8), 1.2)
    events = perform(s, 66, seed=16, lead=0.022, loudness=1.12)
    return Piece("Greve de l'Ouest", "16-greve-de-l-ouest", 66, 3, 4, "grand-kawai", 0.26, -1.0, events, sections)


# ---- 17: Neons ----------------------------------------------------------------------------------
# F# minor, 96 BPM. Octaves pulsing in eighths the whole way, a slow line over
# them that comes back higher, then in full chords, then breaks off.
NEON_CHORDS = [("F#2", "F#m", ["F#3", "A3", "C#4"]), ("D2", "Dmaj7", ["F#3", "A3", "C#4"]),
               ("B1", "Bm7", ["F#3", "A3", "D4"]), ("C#2", "C#7", ["E#3", "G#3", "B3"])]
NEON_LINE = [
    [(0, 2, "C#5"), (2, 1, "A4"), (3, 1, "B4")],
    [(0, 3, "C#5"), (3, 1, "A4")],
    [(0, 2, "D5"), (2, 1, "C#5"), (3, 1, "B4")],
    [(0, 4, "G#4")],
    [(0, 1, "A4"), (1, 1, "C#5"), (2, 2, "F#5")],
    [(0, 2, "E5"), (2, 2, "C#5")],
    [(0, 1, "D5"), (1, 1, "F#5"), (2, 1, "E5"), (3, 1, "D5")],
    [(0, 2, "C#5"), (2, 1, "B4"), (3, 1, "G#4")],
]


def pulse(score, bar, bass, level, swell=0.0):
    """Eight octaves to the bar: the first leaned on, the rest breathing."""
    root = n(bass)
    for step in range(8):
        accent = 1.0 if step == 0 else 0.8 if step == 4 else 0.72 if step % 2 == 0 else 0.62
        score.chord(score.at(bar, step * 0.5), 0.5, [root, root + 12], level * accent * (1.0 + swell * step / 8.0), "bass")


def neons():
    s = Score(4)
    sections = [("intro", 0, 4), ("line", 4, 8), ("higher", 12, 8), ("full", 20, 8), ("break", 28, 4), ("end", 32, 4)]
    for bar in range(4):
        pulse(s, bar, NEON_CHORDS[bar % 4][0], 0.5, swell=0.05)
    for bar in range(4, 12):
        bass, name, upper = NEON_CHORDS[bar % 4]
        pulse(s, bar, bass, 0.54)
        s.line(bar, NEON_LINE[bar - 4], 0.72)
    for bar in range(12, 20):
        bass, name, upper = NEON_CHORDS[bar % 4]
        pulse(s, bar, bass, 0.6)
        line = NEON_LINE[bar - 12]
        s.line(bar, line, 0.8, octave=1)
        # A second voice a sixth below the long notes.
        for beat, length, note_name in line:
            if length >= 2:
                s.add(s.at(bar, beat + 0.5), length - 0.5, n(note_name) + 12 - 9, 0.48)
        # The fifth answering on the offbeats.
        for beat in (1.5, 3.5):
            s.add(s.at(bar, beat), 0.5, n(bass) + 19, 0.4)
    for bar in range(20, 28):
        bass, name, upper = NEON_CHORDS[bar % 4]
        pulse(s, bar, bass, 0.74, swell=0.08)
        s.add(s.at(bar), 4, n(bass) - 12, 0.7, "bass")
        line = NEON_LINE[bar - 20]
        for beat, length, note_name in line:
            top = n(note_name) + 12
            chord_tones = sorted({n(x) + 12 * k for x in upper for k in (1, 2)})
            fill = [p for p in chord_tones if top - 12 < p < top - 2][-2:]
            s.add(s.at(bar, beat), length, top, 0.92, "melody")
            s.chord(s.at(bar, beat), length, fill + [top - 12], 0.62)
    for bar in range(28, 32):
        pulse(s, bar, NEON_CHORDS[bar % 4][0], 0.56 - (bar - 28) * 0.05)
    for bar in range(32, 35):
        pulse(s, bar, NEON_CHORDS[(bar - 32) % 4][0], 0.38 - (bar - 32) * 0.04)
    s.line(32, [(0, 2, "C#6"), (2, 1, "A5"), (3, 1, "B5")], 0.48)
    s.line(33, [(0, 4, "C#6")], 0.44)
    s.line(34, [(0, 2, "D6"), (2, 2, "C#6")], 0.4)
    s.chord(s.at(35), 8, notes("F#1 F#2 C#3 A3 C#4 F#4 A4 C#5"), 0.34)
    for bar in range(36):
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(bar, 0.06)])
    s.pedal(s.at(35), s.at(38), [])
    for beat, level in ((0, 0.62), (s.at(4), 0.66), (s.at(12), 0.72), (s.at(19, 3), 0.84), (s.at(20), 0.9),
                        (s.at(26), 1.0), (s.at(27, 3), 0.94), (s.at(28), 0.74), (s.at(31, 3), 0.5), (s.at(35), 0.42)):
        s.dynamic(beat, level)
    s.pace(0, 1.0)
    s.pace(s.at(20), 1.015)
    s.pace(s.at(27, 2), 0.96)
    s.pace(s.at(28), 1.0)
    s.pace(s.at(32), 0.95)
    s.pace(s.at(35), 0.75)
    s.hold(s.at(34, 3.5), 1.0)
    events = perform(s, 96, seed=17, roll=(0.003, 0.009), jitter=0.004)
    return Piece("Neons", "17-neons", 96, 4, 4, "grand-steinway", 0.24, -3.0, events, sections)


# ---- 18: Cycles lents -------------------------------------------------------------------------------
# Three movements of repeated figures, 72 BPM. I: G minor, a broken chord that
# turns over and over, then a tune over it. II: D minor, triplets against
# eighths. III: C minor, a slow chorale. Played evenly, as this music wants:
# little rubato, dynamics in terraces, the pedal changed with each chord.

def broken(score, bar, triad, level, low, beats=4, figure=(0, 1, 2, 1)):
    """A broken chord in eighths: the triad's notes in the figure's order,
    above `low`."""
    tones = sorted(p for p in (pc + 12 * k for pc in triad for k in range(9)) if p >= low)[:3]
    for step in range(beats * 2):
        score.add(score.at(bar, step * 0.5), 0.5, tones[figure[step % len(figure)]],
                  level * (1.0 if step % 4 == 0 else 0.86 if step % 2 == 0 else 0.8), "inner")


def triplets(score, bar, triad, level, low):
    tones = sorted(p for p in (pc + 12 * k for pc in triad for k in range(9)) if p >= low)[:4]
    figure = [0, 1, 2, 3, 2, 1]
    for step in range(12):
        score.add(score.at(bar, step / 3.0), 1 / 3.0, tones[figure[step % 6]],
                  level * (1.0 if step % 3 == 0 else 0.8), "inner")


def pcs(names):
    return [NAMES[x[0]] + (1 if "#" in x else -1 if "b" in x[1:] else 0) for x in names.split()]


def cycles_lents():
    s = Score(4)
    sections = []
    # I (bars 0-25): Gm | Eb | Cm | D.
    one = [("G2", pcs("G Bb D")), ("Eb2", pcs("Eb G Bb")), ("C2", pcs("C Eb G")), ("D2", pcs("D F# A"))]
    tune = [  # over the cycle, twice
        [(0, 2, "D5"), (2, 1, "C5"), (3, 1, "Bb4")], [(0, 3, "G4"), (3, 1, "Bb4")],
        [(0, 2, "C5"), (2, 1, "Eb5"), (3, 1, "D5")], [(0, 4, "A4")],
        [(0, 2, "G5"), (2, 1, "F5"), (3, 1, "Eb5")], [(0, 3, "Bb4"), (3, 1, "D5")],
        [(0, 2, "Eb5"), (2, 1, "G5"), (3, 1, "F#5")], [(0, 4, "D5")],
    ]
    sections.append(("I. figure", 0, 8))
    for bar in range(8):
        bass, triad = one[bar % 4]
        s.chord(s.at(bar), 4, [n(bass) - 12, n(bass)], 0.5, "bass")
        broken(s, bar, triad, 0.5, n("D4"))
    sections.append(("I. tune", 8, 8))
    for bar in range(8, 16):
        bass, triad = one[bar % 4]
        s.add(s.at(bar), 4, n(bass) - 12, 0.5, "bass")
        broken(s, bar, triad, 0.42, n("G3"))
        s.line(bar, tune[bar - 8], 0.74, doubled=True)
    sections.append(("I. figure", 16, 8))
    for bar in range(16, 24):
        bass, triad = one[bar % 4]
        s.chord(s.at(bar), 4, [n(bass) - 12, n(bass)], 0.46, "bass")
        broken(s, bar, triad, 0.46 if bar < 20 else 0.38, n("D4"))
    s.chord(s.at(24), 8, notes("G1 G2 D3 Bb3 D4 G4"), 0.4)
    for bar in range(26):
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(bar, 0.06)])
    for beat, level in ((0, 0.6), (s.at(8), 0.66), (s.at(12), 0.72), (s.at(16), 0.62), (s.at(20), 0.54), (s.at(24), 0.46)):
        s.dynamic(beat, level)
    s.pace(s.at(23), 1.0)
    s.pace(s.at(24), 0.8)
    s.hold(s.at(25, 3), 2.0)

    # II (bars 26-49): Dm | Bb | Dm | A, triplets, then eighths against them.
    two = [("D2", pcs("D F A")), ("Bb1", pcs("Bb D F")), ("D2", pcs("D F A")), ("A1", pcs("A C# E"))]
    start = 26
    sections.append(("II. triplets", start, 8))
    sections.append(("II. two against three", start + 8, 8))
    sections.append(("II. octaves", start + 16, 6))
    for index in range(22):
        bar = start + index
        bass, triad = two[index % 4]
        triplets(s, bar, triad, 0.46 if index < 8 else 0.5, n("A3"))
        if index < 8:
            s.add(s.at(bar), 4, n(bass), 0.48, "bass")
        elif index < 16:
            # Eighths in the left hand: two against three.
            low = sorted(p for p in (pc + 12 * k for pc in triad for k in range(9)) if p >= n(bass))[:3]
            for step in range(8):
                s.add(s.at(bar, step * 0.5), 0.5, low[(0, 1, 2, 1)[step % 4]], 0.4 * (1.0 if step % 2 == 0 else 0.85), "bass")
        else:
            s.chord(s.at(bar), 2, [n(bass) - 12, n(bass)], 0.64, "bass")
            s.chord(s.at(bar, 2), 2, [n(bass) - 12, n(bass)], 0.56, "bass")
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(bar, 0.06)])
    s.chord(s.at(start + 22), 8, notes("D1 D2 A2 F3 A3 D4"), 0.42)
    s.pedal(s.at(start + 22), s.at(start + 24), [])
    for beat, level in ((s.at(start), 0.58), (s.at(start + 8), 0.66), (s.at(start + 16), 0.8), (s.at(start + 20), 0.86),
                        (s.at(start + 21, 3), 0.7), (s.at(start + 22), 0.5)):
        s.dynamic(beat, level)
    s.pace(s.at(start), 1.0)
    s.pace(s.at(start + 21), 0.97)
    s.pace(s.at(start + 22), 0.8)
    s.hold(s.at(start + 23, 3), 2.0)

    # III (bars 50-61): Cm | Ab | Fm | G, a chorale in half notes, twice;
    # quieter the second time, the last chord with its ninth.
    three = [("C2", ["G3", "C4", "Eb4", "G4"], ["Eb3", "G3", "C4"]), ("Ab1", ["Ab3", "C4", "Eb4", "Ab4"], ["C3", "Eb3", "Ab3"]),
             ("F2", ["Ab3", "C4", "F4", "Ab4"], ["C3", "F3", "Ab3"]), ("G1", ["G3", "B3", "D4", "G4"], ["B2", "D3", "G3"])]
    top = [["G5", "Eb5"], ["Eb5", "C5"], ["C5", "Ab4"], ["B4", "D5"]]
    start = 50
    sections.append(("III. chorale", start, 12))
    for index in range(12):
        bar = start + index
        bass, upper, middle = three[index % 4]
        quiet = index >= 8
        for half in range(2):
            beat = half * 2
            s.chord(s.at(bar, beat), 2, [n(bass) - (12 if half == 0 else 0), n(bass) + 12], 0.52 if not quiet else 0.42, "bass")
            s.chord(s.at(bar, beat), 2, [n(x) for x in upper], 0.42 if not quiet else 0.34)
            s.add(s.at(bar, beat), 2, n(top[index % 4][half]) + (12 if quiet else 0), 0.7 if not quiet else 0.56, "melody")
        s.pedal(s.at(bar), s.at(bar + 1), [s.at(bar, 0.06), s.at(bar, 2.06)])
    s.chord(s.at(start + 12), 8, notes("C1 C2 G2 Eb3 D4 G4 C5 D5"), 0.4)
    s.pedal(s.at(start + 12), s.at(start + 14), [])
    for beat, level in ((s.at(start), 0.64), (s.at(start + 4), 0.7), (s.at(start + 8), 0.56), (s.at(start + 12), 0.44)):
        s.dynamic(beat, level)
    s.pace(s.at(start), 0.97)
    s.pace(s.at(start + 11), 0.92)
    s.pace(s.at(start + 12), 0.72)
    s.hold(s.at(start + 11, 3.5), 1.0)
    events = perform(s, 72, seed=18, roll=(0.003, 0.008), lead=0.012, jitter=0.004, loudness=1.22)
    return Piece("Cycles lents", "18-cycles-lents", 72, 4, 4, "grand-kawai", 0.22, -1.0, events, sections)


PIECES = {"luna": calle_luna, "greve": greve_ouest, "neons": neons, "cycles": cycles_lents}
