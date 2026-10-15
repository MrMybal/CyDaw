"""Factory presets of Empreinte, Givre.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name.
"""
from __future__ import annotations

# Empreinte's built-in spaces (SpaceDesign.h, empreinteRooms).
SMALL_ROOM, STUDIO, HALL, LARGE_HALL, CATHEDRAL, PLATE, SPRING, ECHO_CHAMBER, TUNNEL, CAVE = range(10)

# Note values of the sync settings (0 is free): 1 1/32, 2 1/16T, 3 1/16,
# 4 1/8T, 5 1/16., 6 1/8, 7 1/4T, 8 1/8., 9 1/4, 10 1/4., 11 1/2, 12 1/1.
SYNC_8, SYNC_4, SYNC_2 = 6, 9, 11


def empreinte(space: int, **settings: float) -> dict[str, float]:
    """An Empreinte preset: its space, then its settings by suffix."""
    values = {"empreinte.space": float(space)}
    values.update({f"empreinte.{name}": float(value) for name, value in settings.items()})
    return values


def givre(**settings: float) -> dict[str, float]:
    """A Givre preset: its settings by suffix."""
    return {f"givre.{name}": float(value) for name, value in settings.items()}


PRESETS: dict[str, dict[str, dict[str, float]]] = {
    "empreinte": {
        "Vocal Booth": empreinte(SMALL_ROOM, mix=0.18, length=0.7, tail=-3.0, lowCut=120.0, highCut=12_000.0),
        "Mono Room": empreinte(SMALL_ROOM, mix=0.2, width=0.0, lowCut=100.0),
        "Drum Room": empreinte(STUDIO, mix=0.25, early=3.0, tail=-4.0, lowCut=80.0, width=0.9),
        "Short Ambience": empreinte(STUDIO, mix=0.2, length=0.35, early=3.0, tail=-6.0),
        "Warm Concert Hall": empreinte(HALL, mix=0.3, predelay=20.0, highCut=11_000.0),
        "Grand Hall Vocals": empreinte(LARGE_HALL, mix=0.25, predelay=35.0, early=-3.0, lowCut=180.0, highCut=9_000.0),
        "Cathedral Tail": empreinte(CATHEDRAL, mix=0.4, predelay=40.0, early=-9.0, lowCut=120.0),
        "Wide Pad Wash": empreinte(CATHEDRAL, mix=0.6, early=-12.0, tail=2.0, lowCut=250.0),
        "Bright Plate": empreinte(PLATE, mix=0.25, predelay=12.0, lowCut=150.0),
        "Surf Spring": empreinte(SPRING, mix=0.35, lowCut=120.0, highCut=6_000.0),
        "Echo Chamber Vocals": empreinte(ECHO_CHAMBER, mix=0.28, predelay=15.0, lowCut=150.0),
        "Tunnel Flutter": empreinte(TUNNEL, mix=0.35, width=0.8),
        "Deep Cave": empreinte(CAVE, mix=0.4, predelay=60.0, tail=2.0),
        "Reverse Swell": empreinte(LARGE_HALL, mix=0.5, reverse=1.0, length=0.6, early=-6.0),
    },
    "givre": {
        "Spectral Blur Wash": givre(blur=0.7, scatter=0.3, mix=0.5),
        "Subtle Smear": givre(blur=0.25, smear=0.15, scatter=0.1, mix=0.25),
        "Smeared Choir": givre(blur=0.5, smear=0.6, scatter=0.3, mix=0.5),
        "Frozen Shimmer Up": givre(shift=12.0, blur=0.6, scatter=0.4, mix=0.4),
        "Octave Down Haze": givre(shift=-12.0, blur=0.5, tone=-0.4, mix=0.4),
        "Glass Fifth": givre(shift=7.0, smear=0.2, tone=0.4, scatter=0.2, mix=0.35),
        "Slight Detune Haze": givre(shift=0.15, blur=0.3, scatter=0.25, mix=0.4),
        "Dark Ice": givre(tone=-0.7, blur=0.8, smear=0.4, mix=0.5),
        "Bright Frost": givre(tone=0.6, scatter=0.6, blur=0.5, mix=0.45),
        "Frozen Noise Bed": givre(scatter=1.0, blur=0.9, smear=0.8, mix=0.6),
        "Endless Pad Freeze": givre(freeze=1.0, blur=0.6, smear=0.15, scatter=0.35, mix=0.7),
        "Rhythmic Freeze": givre(freeze=1.0, sync=SYNC_4, blur=0.2, scatter=0.15, mix=1.0),
    },
}
