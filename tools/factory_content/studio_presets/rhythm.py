"""Factory presets of Grains, Saccade, Boucleur.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name.
"""
from __future__ import annotations


def values(prefix: str, **settings: float) -> dict[str, float]:
    """A preset's values: each setting's binding suffix under the effect's prefix."""
    return {f"{prefix}.{name}": float(value) for name, value in settings.items()}


# Note values (1..12 of noteDivisions; 0 is free where a value may be):
# 1 1/32, 2 1/16T, 3 1/16, 4 1/8T, 5 1/16., 6 1/8, 7 1/4T, 8 1/8., 9 1/4,
# 10 1/4., 11 1/2, 12 1/1 (a bar for Saccade's interval).
N32, N16T, N16, N8T, N16D, N8, N4T, N8D, N4, N4D, N2, BAR = range(1, 13)

# Saccade's modes; Boucleur's lengths (0 free, then 1, 2, 4, 8, 16 bars) and speeds.
MIX, INSERT, REPEATS_ONLY = range(3)
FREE, BARS_1, BARS_2, BARS_4, BARS_8, BARS_16 = range(6)
HALF_SPEED, NORMAL_SPEED, DOUBLE_SPEED = range(3)


def grains(**settings: float) -> dict[str, float]:
    return values("grains", **settings)


def saccade(**settings: float) -> dict[str, float]:
    return values("saccade", **settings)


def boucleur(**settings: float) -> dict[str, float]:
    return values("boucleur", **settings)


PRESETS: dict[str, dict[str, dict[str, float]]] = {
    # Grains: time and spray in ms, pitch in semitones, density in grains a
    # second, size in ms, high cut in Hz.
    "grains": {
        "Shimmer Cloud": grains(time=300.0, pitch=12.0, spray=120.0, density=40.0, size=180.0, feedback=0.6, stereo=0.9,
                                highCut=12_000.0, mix=0.45),
        "Octave Down Haze": grains(time=350.0, pitch=-12.0, spray=80.0, density=30.0, size=220.0, feedback=0.4, stereo=0.7,
                                   highCut=6_000.0, mix=0.4),
        "Fifth Above": grains(time=280.0, pitch=7.0, spray=60.0, density=40.0, size=150.0, feedback=0.5, stereo=0.8, mix=0.35),
        "Granular Slapback": grains(time=90.0, spray=5.0, density=50.0, size=60.0, feedback=0.1, stereo=0.3, mix=0.35),
        "Scattered Echoes": grains(time=380.0, spray=300.0, density=12.0, size=70.0, feedback=0.45, stereo=1.0, mix=0.4),
        "Reverse Swells": grains(time=600.0, spray=150.0, density=8.0, size=400.0, reverseChance=1.0, feedback=0.35, stereo=0.6,
                                 mix=0.45),
        "Pitch Spray": grains(time=250.0, spray=200.0, density=35.0, size=100.0, pitchRandom=7.0, stereo=0.9, feedback=0.3, mix=0.4),
        "Dotted Eighth Grains": grains(sync=N8D, spray=15.0, density=30.0, size=110.0, feedback=0.45, stereo=0.6, mix=0.35),
        "Quarter Note Cloud": grains(sync=N4, spray=100.0, density=60.0, size=250.0, feedback=0.55, stereo=0.9, mix=0.4),
        "Endless Pad Bed": grains(time=800.0, spray=400.0, density=70.0, size=450.0, feedback=0.7, stereo=1.0, reverseChance=0.3,
                                  highCut=9_000.0, mix=0.6),
        "Glitch Stutter": grains(time=30.0, spray=40.0, density=80.0, size=20.0, pitchRandom=2.0, reverseChance=0.4, feedback=0.3,
                                 stereo=0.8, mix=0.5),
        "Dark Tape Grains": grains(time=450.0, spray=40.0, density=20.0, size=140.0, pitchRandom=0.3, feedback=0.65, highCut=3_500.0,
                                   stereo=0.5, mix=0.4),
    },
    # Saccade: the interval and the grid as note values, the offset and the
    # gate in sixteenths, filter in Hz.
    "saccade": {
        "Stutter Fill": saccade(interval=BAR, offset=12.0, gate=4.0, grid=N16, chance=1.0, mode=INSERT),
        "Snare Roll Build": saccade(interval=BAR, offset=12.0, gate=4.0, grid=N32, chance=1.0, mode=INSERT),
        "Half Bar Roll": saccade(interval=BAR, offset=8.0, gate=8.0, grid=N16, decay=0.08, chance=1.0, mode=INSERT),
        "Triplet Stutter": saccade(interval=BAR, offset=8.0, gate=8.0, grid=N8T, chance=1.0, mode=INSERT),
        "Tape Stop Repeat": saccade(interval=BAR, offset=12.0, gate=4.0, grid=N8, pitchDecay=0.6, decay=0.15, filter=6_000.0,
                                    mode=INSERT),
        "Pitch Dive Roll": saccade(interval=BAR, offset=12.0, gate=4.0, grid=N32, pitchDecay=0.35, decay=0.05, mode=INSERT),
        "Random Glitch": saccade(interval=N4, offset=2.0, gate=2.0, grid=N32, chance=0.25, variation=0.6, reverseChance=0.3,
                                 mode=INSERT),
        "Beat Juggle": saccade(interval=N2, offset=0.0, gate=8.0, grid=N4, chance=0.5, variation=0.3, mode=INSERT),
        "Lo-Fi Stammer": saccade(interval=N2, offset=4.0, gate=2.0, grid=N16, chance=0.6, variation=0.4, filter=1_800.0, mode=INSERT),
        "Reverse Echoes": saccade(interval=N2, offset=4.0, gate=4.0, grid=N8, reverseChance=1.0, decay=0.35, filter=5_000.0,
                                  mode=MIX, mix=0.8),
        "Fading Repeats": saccade(interval=BAR, offset=8.0, gate=8.0, grid=N8, decay=0.4, filter=4_000.0, mode=MIX),
        "Gated Repeats Only": saccade(interval=BAR, offset=0.0, gate=16.0, grid=N8, decay=0.25, mode=REPEATS_ONLY),
    },
    # Boucleur: a few setups; record, play, overdub and clear are played, not
    # stored.
    "boucleur": {
        "Free Loop": boucleur(bars=FREE, quantize=1.0),
        "One Bar Loop": boucleur(bars=BARS_1, quantize=1.0),
        "Two Bar Loop": boucleur(bars=BARS_2, quantize=1.0),
        "Four Bar Loop": boucleur(bars=BARS_4, quantize=1.0),
        "Eight Bar Loop": boucleur(bars=BARS_8, quantize=1.0),
        "Half-Time Loop": boucleur(bars=BARS_2, speed=HALF_SPEED, quantize=1.0),
        "Double-Time Loop": boucleur(bars=BARS_2, speed=DOUBLE_SPEED, quantize=1.0),
        "Reverse Loop": boucleur(bars=BARS_2, reverse=1.0, quantize=1.0),
        "Fading Layers": boucleur(bars=BARS_4, feedback=0.7, quantize=1.0),
        "Loop Only": boucleur(bars=FREE, dry=0.0, quantize=0.0),
    },
}
