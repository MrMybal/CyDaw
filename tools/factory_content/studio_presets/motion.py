"""Factory presets of Pompe, Décaleur, Anneau, Rotor.

Values by binding suffix ("<prefix>.<suffix>"); what is not given keeps its
default. English names only, no brands, no "/" in a name. The helpers below
write an effect's main settings whole, so a preset says what it does.
"""
from __future__ import annotations

# Note values of a sync (0 is free): EffectCatalogue.h's noteDivisions.
FREE, N32, N16T, N16, N8T, N16D, N8, N4T, N8D, N4, N4D, N2, N1 = range(13)
# Pompe's shapes and triggers (MotionDesign.h).
SOFT, HARD, CUT, CHOP, RAMP_UP, RAMP_DOWN, SINE_PUMP, DRAWN = range(8)
TEMPO, KEY = range(2)
# Décaleur's modes; the LFOs' waves (Décaleur's and Anneau's).
SINGLE, BOTH, OPPOSITE = range(3)
SINE, TRIANGLE, SAW, SQUARE = range(4)
# Anneau's carriers and modes.
CARRIER_SINE, CARRIER_TRIANGLE, CARRIER_SQUARE, CARRIER_SAW = range(4)
RING, AMPLITUDE = range(2)
# Rotor's speeds.
STOP, SLOW, FAST = range(3)


def pump(shape: int, sync: int, depth: float, *, curve: float = 0.4, hold: float = 0.05, phase: float = 0.0,
         offset: float = 0.0, rate: float = 2.0, smoothing: float = 1.5, trigger: int = TEMPO, threshold: float = -24.0,
         mix: float = 1.0, output: float = 0.0, points: list[float] | None = None) -> dict:
    """A Pompe: its curve over a note value (or `rate` Hz when free); in key
    mode each hit over the threshold runs it once."""
    values = {"pompe.shape": float(shape), "pompe.sync": float(sync), "pompe.depth": float(depth), "pompe.curve": float(curve),
              "pompe.hold": float(hold), "pompe.phase": float(phase), "pompe.stereoOffset": float(offset),
              "pompe.rate": float(rate), "pompe.smoothing": float(smoothing), "pompe.trigger": float(trigger),
              "pompe.threshold": float(threshold), "pompe.mix": float(mix), "pompe.output": float(output)}
    if points is not None:
        assert len(points) == 16
        for index, level in enumerate(points, start=1):
            values["pompe.point%d" % index] = float(level)
    return values


def shift(hz: float, *, mode: int = SINGLE, fine: float = 0.0, spread: float = 0.0, feedback: float = 0.0, delay: float = 0.0,
          lfo_depth: float = 0.0, lfo_rate: float = 0.5, lfo_shape: int = SINE, sync: int = FREE, mix: float = 1.0,
          output: float = 0.0) -> dict:
    """A Décaleur: every frequency moved by `hz` (+ fine), fed back through
    `delay` ms (0: the shortest) for spirals."""
    return {"decaleur.shift": float(hz), "decaleur.mode": float(mode), "decaleur.fine": float(fine), "decaleur.spread": float(spread),
            "decaleur.feedback": float(feedback), "decaleur.delay": float(delay), "decaleur.lfoDepth": float(lfo_depth),
            "decaleur.lfoRate": float(lfo_rate), "decaleur.lfoShape": float(lfo_shape), "decaleur.sync": float(sync),
            "decaleur.mix": float(mix), "decaleur.output": float(output)}


def ring(hz: float, *, shape: int = CARRIER_SINE, mode: int = RING, mix: float = 0.5, drive: float = 0.0, low_cut: float = 20.0,
         lfo_depth: float = 0.0, lfo_rate: float = 0.5, lfo_shape: int = SINE, sync: int = FREE, stereo: float = 0.0,
         output: float = 0.0) -> dict:
    """An Anneau: the sound times a carrier at `hz`, its LFO in octaves."""
    return {"anneau.frequency": float(hz), "anneau.shape": float(shape), "anneau.mode": float(mode), "anneau.mix": float(mix),
            "anneau.drive": float(drive), "anneau.lowCut": float(low_cut), "anneau.lfoDepth": float(lfo_depth),
            "anneau.lfoRate": float(lfo_rate), "anneau.lfoShape": float(lfo_shape), "anneau.sync": float(sync),
            "anneau.stereo": float(stereo), "anneau.output": float(output)}


def rotary(speed: int, *, slow: float = 0.8, fast: float = 6.8, acceleration: float = 1.2, balance: float = 0.0,
           doppler: float = 0.5, drive: float = 3.0, crossover: float = 800.0, width: float = 0.8, room: float = 0.25,
           mix: float = 1.0, output: float = 0.0) -> dict:
    """A Rotor: its speed (stop, slow, fast), the rotors' rates and inertia,
    the cabinet and its preamp."""
    return {"rotor.speed": float(speed), "rotor.slow": float(slow), "rotor.fast": float(fast),
            "rotor.acceleration": float(acceleration), "rotor.balance": float(balance), "rotor.doppler": float(doppler),
            "rotor.drive": float(drive), "rotor.crossover": float(crossover), "rotor.width": float(width), "rotor.room": float(room),
            "rotor.mix": float(mix), "rotor.output": float(output)}


PRESETS: dict[str, dict[str, dict[str, float]]] = {
    "pompe": {
        "Sidechain Pump Quarter": pump(SOFT, N4, 0.85),
        "Gentle Breath": pump(SOFT, N4, 0.45, curve=0.2, hold=0.0, smoothing=3.0),
        "Hard Pump Eighths": pump(HARD, N8, 0.9, curve=0.3, hold=0.06),
        "Half Note Swell": pump(SOFT, N2, 0.8, curve=-0.3, hold=0.1),
        "Kick Ducker Keyed": pump(SOFT, N4, 0.9, curve=0.5, hold=0.03, trigger=KEY, threshold=-24.0),
        "Fast Key Duck": pump(CUT, N8, 1.0, curve=0.0, hold=0.0, trigger=KEY, threshold=-20.0),
        "Trance Gate Sixteenths": pump(CHOP, N16, 1.0, curve=0.0, hold=0.0, smoothing=2.0),
        "Stutter Chop Eighths": pump(CHOP, N8, 0.95, curve=0.0, hold=0.0, smoothing=1.0),
        "Reverse Swell": pump(RAMP_UP, N2, 1.0, curve=-0.6, hold=0.0, smoothing=4.0),
        "Fade Away Bars": pump(RAMP_DOWN, N1, 0.9, curve=0.0, hold=0.0, smoothing=4.0),
        "Wide Stereo Pump": pump(SOFT, N4, 0.8, offset=0.25, smoothing=2.0),
        "Sine Tremolo Triplets": pump(SINE_PUMP, N4T, 0.6, curve=0.0, hold=0.0, smoothing=2.0),
        "Drawn Bounce": pump(DRAWN, N4, 0.85, curve=0.0, hold=0.0,
                             points=[0.0, 0.6, 0.9, 0.5, 0.2, 0.7, 1.0, 0.8, 0.6, 0.9, 1.0, 1.0, 0.9, 1.0, 1.0, 1.0]),
        "Free Wobble 3 Hz": pump(SINE_PUMP, FREE, 0.7, curve=0.0, hold=0.0, rate=3.0, smoothing=2.0),
    },
    "decaleur": {
        "Subtle Detune Shimmer": shift(0.0, fine=2.5, mix=0.5),
        "Barber Pole Up": shift(0.0, fine=0.8, feedback=0.65, mix=0.5),
        "Barber Pole Down": shift(0.0, fine=-0.8, feedback=0.65, mix=0.5),
        "Stereo Widener Shift": shift(0.0, mode=OPPOSITE, fine=4.0, mix=0.4),
        "Inharmonic Lift": shift(100.0),
        "Dark Drop": shift(-150.0),
        "Robot Bell": shift(180.0, mode=BOTH, mix=0.7),
        "Clangorous Both Sides": shift(300.0, mode=BOTH, mix=0.8),
        "Spiral Ascent": shift(25.0, feedback=0.8, delay=120.0, mix=0.5),
        "Spiral Descent": shift(-30.0, feedback=0.8, delay=150.0, mix=0.5),
        "Wobbling Shift": shift(0.0, lfo_depth=60.0, lfo_rate=0.3, mix=0.6),
        "Tempo Siren": shift(200.0, lfo_depth=400.0, lfo_shape=TRIANGLE, sync=N2, mix=0.7),
    },
    "anneau": {
        "Classic Ring": ring(440.0, mix=0.6),
        "Robot Voice": ring(70.0, mix=1.0, low_cut=80.0),
        "Metallic Bell": ring(1270.0, mix=0.7),
        "Glass Chime": ring(2400.0, mix=0.5, low_cut=300.0),
        "Fast Tremolo": ring(8.0, mode=AMPLITUDE, mix=1.0),
        "Square Buzz": ring(330.0, shape=CARRIER_SQUARE, mix=0.5, drive=6.0),
        "Gritty Saw Ring": ring(110.0, shape=CARRIER_SAW, mix=0.6, drive=12.0, low_cut=120.0),
        "Sweeping Sci-Fi": ring(300.0, mix=0.7, lfo_depth=1.5, lfo_rate=0.2, lfo_shape=TRIANGLE, stereo=0.25),
        "Stereo Shimmer Ring": ring(1800.0, mix=0.4, lfo_depth=0.3, lfo_rate=0.5, stereo=0.5),
        "Stepped Laser Eighths": ring(600.0, mix=0.6, lfo_depth=1.0, lfo_shape=SQUARE, sync=N8),
        "Subtle Sidebands": ring(880.0, mode=AMPLITUDE, mix=0.3),
        "Low Rumble Ring": ring(35.0, mix=0.5),
    },
    "rotor": {
        "Organ Chorale": rotary(SLOW),
        "Organ Tremolo": rotary(FAST),
        "Overdriven Rock Organ": rotary(SLOW, drive=15.0, balance=0.1, room=0.3),
        "Gospel Fast Spin": rotary(FAST, fast=7.2, acceleration=0.9, drive=6.0, balance=0.15),
        "Lazy Rotor": rotary(SLOW, acceleration=4.0),
        "Still Cabinet Tone": rotary(STOP, drive=4.0, room=0.35),
        "Wide Stereo Spin": rotary(FAST, fast=6.2, width=1.0, room=0.4),
        "Guitar Rotary": rotary(SLOW, slow=1.0, crossover=1000.0, drive=5.0, balance=0.3, doppler=0.6),
        "Subtle Rotary Mix": rotary(SLOW, mix=0.4),
        "Horn Only Shimmer": rotary(FAST, balance=1.0, doppler=0.8),
        "Deep Drum Swirl": rotary(SLOW, slow=0.6, balance=-0.6, crossover=600.0),
        "Clean Jazz Chorale": rotary(SLOW, slow=0.7, drive=0.0, doppler=0.4, room=0.15),
    },
}
