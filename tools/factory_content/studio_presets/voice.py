"""Factory presets of Vocodeur, Dé-Esseur, Accordeur.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name.
"""
from __future__ import annotations


def values(prefix: str, **settings: float) -> dict[str, float]:
    """A preset's values: each setting's binding suffix under the effect's prefix."""
    return {f"{prefix}.{name}": float(value) for name, value in settings.items()}


# The vocoder's carriers (VoiceDesign.h).
NOISE, SAW, FOLLOW, EXTERNAL = range(4)
# The de-esser's modes and detectors.
BAND_ONLY, WHOLE = range(2)
HIGH_PASS, BAND = range(2)
# The tuner's displays.
NEEDLE, STROBE = range(2)


def vocoder(**settings: float) -> dict[str, float]:
    return values("vocoder", **settings)


def deesser(**settings: float) -> dict[str, float]:
    return values("deesser", **settings)


def tuner(**settings: float) -> dict[str, float]:
    return values("tuner", **settings)


PRESETS: dict[str, dict[str, dict[str, float]]] = {
    "vocoder": {
        "Robot Voice": vocoder(carrier=SAW, pitch=45, detune=0.0, bands=16, unvoiced=0.2, attack=2, release=40, stereo=0.2),
        "Classic Synth Vocoder": vocoder(carrier=FOLLOW, bands=20, detune=0.35, unvoiced=0.4, stereo=0.5),
        "Whisper Choir": vocoder(carrier=NOISE, bands=32, unvoiced=0.6, stereo=0.9, release=120, tone=0.3),
        "Talking Pad": vocoder(carrier=EXTERNAL, bands=24, attack=8, release=150, unvoiced=0.3, stereo=0.7),
        "Monotone Announcer": vocoder(carrier=SAW, pitch=52, detune=0.1, bands=12, highBand=6_000, tone=-0.3, stereo=0.0),
        "Chipmunk Formants": vocoder(carrier=FOLLOW, formant=7, bands=24, tone=0.2),
        "Giant Formants": vocoder(carrier=FOLLOW, formant=-7, bands=24, tone=-0.4, unvoiced=0.2),
        "Crisp Forty Bands": vocoder(carrier=FOLLOW, bands=40, bandwidth=0.8, attack=1, release=30, tone=0.4, unvoiced=0.5),
        "Lo-Fi Radio Speaker": vocoder(carrier=SAW, pitch=48, bands=8, lowBand=300, highBand=3_500, tone=-0.2, stereo=0.0),
        "Wide Stereo Choir": vocoder(carrier=FOLLOW, bands=28, detune=0.8, stereo=1.0, release=90),
        "Slow Breathing Swell": vocoder(carrier=NOISE, bands=20, attack=60, release=600, unvoiced=0.8, mix=0.8),
        "Gated Machine": vocoder(carrier=SAW, pitch=40, gate=-40, attack=0.5, release=15, bands=16, detune=0.0),
        "Half Wet Blend": vocoder(carrier=FOLLOW, mix=0.5),
    },
    "de-esser": {
        "Gentle Vocal": deesser(threshold=-30, frequency=6_500, range=6, release=70),
        "Bright Sibilance Tamer": deesser(threshold=-34, frequency=7_500, range=12, release=50, attack=0.5),
        "Female Vocal": deesser(detectorType=BAND, frequency=7_500, bandwidth=1.2, threshold=-32, range=10),
        "Male Vocal": deesser(detectorType=BAND, frequency=5_000, bandwidth=1.0, threshold=-30, range=8),
        "Whole Sound Soft": deesser(mode=WHOLE, threshold=-28, frequency=6_000, range=6, release=80),
        "Narrow Band Surgical": deesser(detectorType=BAND, bandwidth=0.4, frequency=6_800, threshold=-36, range=14, attack=0.3,
                                        release=40),
        "Harsh Cymbals": deesser(frequency=9_000, threshold=-26, range=8, release=120),
        "Podcast Speech": deesser(frequency=5_500, threshold=-32, range=10, release=60),
        "Acoustic Guitar Squeak": deesser(detectorType=BAND, frequency=3_500, bandwidth=0.8, threshold=-38, range=8, release=40),
        "Unlinked Stereo Bus": deesser(link=0.0, threshold=-30, frequency=7_000, range=6),
        "Heavy Reduction": deesser(mode=WHOLE, threshold=-40, range=18, frequency=6_000, release=90),
        "Find The Sibilance": deesser(listen=1, detectorType=BAND, bandwidth=1.0, frequency=6_500),
    },
    "tuner": {
        "Concert Pitch 440": tuner(reference=440),
        "Concert Pitch 442": tuner(reference=442),
        "Orchestra 443": tuner(reference=443),
        "Alternative 432": tuner(reference=432),
        "Baroque 415": tuner(reference=415),
        "Choir Pitch 466": tuner(reference=466),
        "Strobe 440": tuner(reference=440, displayMode=STROBE),
        "Silent Tuning": tuner(mute=1),
        "Silent Strobe 442": tuner(reference=442, mute=1, displayMode=STROBE),
    },
}
