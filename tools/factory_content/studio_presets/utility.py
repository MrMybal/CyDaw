"""Factory presets of Utilitaire, Stéréoscope.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name.
"""
from __future__ import annotations


def values(prefix: str, **settings: float) -> dict[str, float]:
    """A preset's values: each setting's binding suffix under the effect's prefix."""
    return {f"{prefix}.{name}": float(value) for name, value in settings.items()}


def utility(**settings: float) -> dict[str, float]:
    return values("utilitaire", **settings)


def scope(**settings: float) -> dict[str, float]:
    return values("stereoscope", **settings)


# Utilitaire's channel modes (UtilityDesign.h).
STEREO, LEFT_ONLY, RIGHT_ONLY, SWAPPED, MONO = range(5)
# Stéréoscope's views.
GONIOMETER, POLAR = range(2)

PRESETS: dict[str, dict[str, dict[str, float]]] = {
    "utilitaire": {
        "Mono Check": utility(channelMode=MONO),
        "Flip Left Polarity": utility(invertLeft=1),
        "Flip Right Polarity": utility(invertRight=1),
        "Flip Both Polarities": utility(invertLeft=1, invertRight=1),
        "Swap Sides": utility(channelMode=SWAPPED),
        "Left Only": utility(channelMode=LEFT_ONLY),
        "Right Only": utility(channelMode=RIGHT_ONLY),
        "Mono Bass 120 Hz": utility(monoBass=120),
        "Tight Low End": utility(monoBass=180, dcFilter=1),
        "Wide Image": utility(width=1.5, monoBass=100),
        "Narrow Image": utility(width=0.6),
        "Trim -6 dB": utility(gain=-6),
        "DC Cleanup": utility(dcFilter=1),
        "Delay 1 ms": utility(delay=1),
    },
    "stereoscope": {
        "Close Up": scope(zoom=2.5, persistence=0.5),
        "Long Trail": scope(persistence=0.95, history=20),
        "Quick Look": scope(persistence=0.1, history=5),
        "Polar Field": scope(displayMode=POLAR, persistence=0.6),
        "Mastering Watch": scope(zoom=1, persistence=0.7, history=30),
    },
}
