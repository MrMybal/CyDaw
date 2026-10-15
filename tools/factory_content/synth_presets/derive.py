"""Dérive's factory sounds: each a few settings over the defaults ("Init" first), keys as in the model's table,
choices by their English names. tools/factory_content/build_synth_presets.py sets, measures and saves them.

The defaults worth knowing: stereo (thickness 0.3), osc 1 a saw, osc 2 a saw 6 ct up, the 24 dB low-pass at 1.4 kHz,
envelope 2 on the cutoff (0.3), drift 0.35, the LFO's amount at 0; the slots: 1 LFO -> Pitch 0.1, 2 Velocity ->
Cutoff 0.15, 3 Mod Wheel -> LFO Amount 1.0 (the wheel brings the vibrato in).
"""
from __future__ import annotations


def mod(slot: int, source: str, destination: str, amount: float) -> dict:
    return {f"mod{slot}.source": source, f"mod{slot}.dest": destination, f"mod{slot}.amount": amount}


PRESETS = {
    "Init": {},
    "Warm Pad": {
        # Four drifting copies, the saw doubled against itself and swayed slowly by the LFO.
        "unison.mode": "Unison", "unison.thickness": 0.45, "osc1.shape": 0.3, "osc2.detune": 9, "f.cutoff": 900,
        "f.resonance": 0.12, "filter.env": 0.15, "filter.key": 0.3, "env2.attack": 1.2, "env2.decay": 2.0,
        "env2.sustain": 0.6, "env2.release": 2.0, "amp.attack": 0.9, "amp.decay": 1.5, "amp.sustain": 0.85,
        "amp.release": 2.2, "drift": 0.45, "lfo1.shape": "Triangle", "lfo1.rate": 0.25, "lfo1.amount": 1.0,
        "filter.lfo": 0.1, **mod(1, "LFO", "Osc 1 Shape", 0.3), **mod(3, "Mod Wheel", "Cutoff", 0.3),
    },
    "Drifting Keys": {
        "osc1.wave": "Triangle", "osc1.shape": 0.25, "osc2.wave": "Sine", "osc2.octave": 1, "osc2.detune": 3,
        "osc2.level": 0.3, "f.type": "Low-pass I 12 dB", "f.cutoff": 2_400, "f.resonance": 0.1, "filter.env": 0.25,
        "env2.decay": 0.9, "env2.sustain": 0.2, "amp.attack": 0.003, "amp.decay": 2.2, "amp.sustain": 0.35,
        "amp.release": 0.6, "amp.velocity": 0.6, "drift": 0.6, "unison.thickness": 0.35,
    },
    "Soft Bass": {
        # A filtered triangle over a sine an octave down that skips the filter.
        "voice.mode": "Mono", "unison.mode": "Single", "osc1.wave": "Triangle", "osc1.octave": -1, "osc2.wave": "Sine",
        "osc2.octave": -1, "osc2.detune": 0, "osc2.level": 0.6, "osc2.filter": 0, "f.cutoff": 500, "f.resonance": 0.1,
        "filter.env": 0.2, "env2.decay": 0.4, "env2.sustain": 0.2, "amp.attack": 0.004, "amp.decay": 0.8,
        "amp.sustain": 0.85, "amp.release": 0.15, "drift": 0.15, **mod(2, "Velocity", "Cutoff", 0.1),
    },
    "Bouncy Pluck": {
        "osc1.wave": "Pulse", "osc1.shape": 0.35, "osc2.detune": 7, "osc2.level": 0.4, "f.cutoff": 380,
        "f.resonance": 0.35, "filter.env": 0.55, "filter.key": 0.6, "env2.attack": 0.001, "env2.decay": 0.22,
        "env2.sustain": 0.0, "env2.release": 0.25, "amp.attack": 0.001, "amp.decay": 0.5, "amp.sustain": 0.0,
        "amp.release": 0.35, "unison.thickness": 0.4,
    },
    "Lo-Fi Lead": {
        # A saturated sine over a square an octave down, a little hiss, wandering pitch.
        "voice.mode": "Legato", "voice.glide": 0.05, "unison.mode": "Single", "osc1.wave": "Saturated",
        "osc1.shape": 0.7, "osc2.wave": "Rectangle", "osc2.octave": -1, "osc2.detune": 4, "osc2.level": 0.35,
        "noise.level": 0.08, "f.type": "Low-pass I 12 dB", "f.cutoff": 2_200, "f.resonance": 0.3, "filter.hpf": 180,
        "filter.env": 0.2, "drift": 0.7, "lfo1.shape": "Wander", "lfo1.rate": 3.0, "lfo1.amount": 1.0,
        **mod(1, "LFO", "Pitch", 0.07), **mod(3, "Mod Wheel", "Cutoff", 0.3), "amp.release": 0.2,
    },
    "Cycling Arp": {
        # Envelope 2 cycles on sixteenths: quick rises, long falls, the filter plucked again and again.
        "osc1.shape": 0.2, "osc2.wave": "Rectangle", "osc2.octave": 1, "osc2.level": 0.3, "f.cutoff": 300,
        "f.resonance": 0.4, "filter.env": 0.55, "env2.cycle": 1, "env2.sync": 3, "env2.tilt": -0.8, "env2.hold": 0.0,
        "amp.sustain": 0.9, "amp.release": 0.3, "unison.thickness": 0.45, **mod(2, "Velocity", "Cutoff", 0.1),
    },
    "Shark Bass": {
        "voice.mode": "Mono", "unison.mode": "Single", "osc1.wave": "Shark Tooth", "osc1.shape": 0.6,
        "osc1.octave": -1, "osc2.octave": -1, "osc2.detune": -5, "osc2.level": 0.45, "f.cutoff": 420,
        "f.resonance": 0.45, "filter.env": 0.45, "env2.decay": 0.3, "env2.sustain": 0.15, "amp.release": 0.12,
        "drift": 0.2,
    },
    "Dreamy Strings": {
        "osc1.shape": 0.5, "osc2.detune": 12, "unison.mode": "Unison", "unison.thickness": 0.6,
        "f.type": "Low-pass I 12 dB", "f.cutoff": 3_200, "f.resonance": 0.05, "filter.hpf": 140, "filter.env": 0.05,
        "amp.attack": 0.6, "amp.decay": 1.0, "amp.sustain": 0.9, "amp.release": 1.5, "lfo1.shape": "Triangle",
        "lfo1.rate": 0.4, "lfo1.amount": 1.0, **mod(1, "LFO", "Osc 1 Shape", 0.3),
        **mod(3, "Pressure", "Cutoff", 0.15), "drift": 0.5,
    },
    "Saturated Lead": {
        "voice.mode": "Legato", "voice.glide": 0.04, "osc1.wave": "Saturated", "osc1.shape": 0.9, "osc2.detune": 5,
        "f.cutoff": 2_800, "f.resonance": 0.25, "filter.env": 0.25, "env2.sustain": 0.5, "amp.release": 0.25,
        "unison.thickness": 0.4, "lfo1.rate": 5.5, **mod(1, "LFO", "Pitch", 0.12),
    },
    "Wobbly Tape Keys": {
        # Every voice's pitch swaying together, as a worn tape does.
        "osc1.wave": "Sine", "osc1.shape": 0.35, "osc2.wave": "Triangle", "osc2.octave": 1, "osc2.level": 0.25,
        "osc2.detune": 0, "f.type": "Low-pass I 12 dB", "f.cutoff": 3_000, "f.resonance": 0.05, "filter.hpf": 60,
        "filter.env": 0.1, "amp.decay": 2.5, "amp.sustain": 0.3, "amp.release": 0.8, "amp.velocity": 0.6,
        "lfo1.rate": 0.7, "lfo1.amount": 1.0, **mod(1, "LFO", "Pitch", 0.09), **mod(3, "Mod Wheel", "Noise", 0.1),
        "drift": 0.8, "noise.level": 0.03,
    },
    "Breathing Pad": {
        # Envelope 2 cycles slowly (a hold at the top): the filter and a breath of noise rise and fall.
        "osc1.wave": "Shark Tooth", "osc1.shape": 0.3, "osc2.detune": 8, "unison.mode": "Unison",
        "unison.thickness": 0.5, "f.cutoff": 500, "f.resonance": 0.25, "filter.env": 0.35, "env2.cycle": 1,
        "env2.rate": 0.2, "env2.tilt": 0.2, "env2.hold": 0.2, "amp.attack": 1.5, "amp.decay": 2.0,
        "amp.sustain": 1.0, "amp.release": 2.5, "noise.level": 0.05, **mod(2, "Envelope 2", "Noise", 0.1),
        "drift": 0.5,
    },
    "Sub Kick Bass": {
        # A sine whose pitch envelope 2 drops on every note.
        "voice.mode": "Mono", "unison.mode": "Single", "osc1.wave": "Sine", "osc1.shape": 0.15, "osc1.octave": -1,
        "osc2.level": 0.0, "f.cutoff": 800, "f.resonance": 0.0, "filter.env": 0.2, "env2.attack": 0.001,
        "env2.decay": 0.06, "env2.sustain": 0.0, "env2.release": 0.05, **mod(1, "Envelope 2", "Pitch", 0.55),
        "amp.attack": 0.001, "amp.decay": 0.9, "amp.sustain": 0.6, "amp.release": 0.15, "drift": 0.05,
    },
    "Sample Hold Bleeps": {
        # Random steps on the pitch and the cutoff, a cycling envelope striking on sixteenths.
        "osc1.wave": "Pulse", "osc1.shape": 0.2, "osc2.wave": "Rectangle", "osc2.octave": 1, "osc2.level": 0.25,
        "f.cutoff": 700, "f.resonance": 0.6, "filter.env": 0.3, "lfo1.shape": "Sample & Hold", "lfo1.sync": 3,
        "lfo1.amount": 1.0, "filter.lfo": 0.5, "env2.cycle": 1, "env2.sync": 3, "env2.tilt": -1.0,
        **mod(1, "LFO", "Pitch", 0.4), **mod(3, "Mod Wheel", "Resonance", 0.3), "amp.attack": 0.002,
        "amp.sustain": 0.85, "amp.release": 0.2, "unison.mode": "Single",
    },
    "Gentle Bells": {
        # A biting sine whose bite envelope 2 rings out, a detuned triangle two octaves up.
        "osc1.wave": "Sine", "osc1.shape": 0.6, "osc1.octave": 1, "osc2.wave": "Triangle", "osc2.octave": 2,
        "osc2.detune": 20, "osc2.level": 0.3, "f.type": "Low-pass I 12 dB", "f.cutoff": 5_000, "f.resonance": 0.05,
        "filter.env": 0.1, "env2.attack": 0.001, "env2.decay": 0.4, "env2.sustain": 0.0,
        **mod(1, "Envelope 2", "Osc 1 Shape", 0.4), "amp.attack": 0.001, "amp.decay": 2.5, "amp.sustain": 0.0,
        "amp.release": 2.5, "amp.velocity": 0.7, "drift": 0.2, "unison.thickness": 0.5,
    },
    "Wide Unison": {
        "osc2.detune": 10, "osc2.level": 0.6, "unison.mode": "Unison", "unison.thickness": 1.0, "f.cutoff": 4_000,
        "f.resonance": 0.1, "filter.env": 0.1, "amp.attack": 0.01, "amp.sustain": 0.9, "amp.release": 0.5,
        "drift": 0.4,
    },
    "Brass Section": {
        "osc2.detune": 8, "f.cutoff": 700, "filter.env": 0.45, "env2.attack": 0.08, "env2.decay": 0.5,
        "env2.sustain": 0.45, "amp.attack": 0.04, "amp.sustain": 0.9, "amp.release": 0.25, "unison.thickness": 0.5,
        **mod(2, "Velocity", "Cutoff", 0.2), "drift": 0.4,
    },
    "Hollow Lead": {
        "voice.mode": "Legato", "voice.glide": 0.03, "unison.mode": "Single", "osc1.wave": "Rectangle",
        "osc1.shape": 0.5, "osc2.wave": "Rectangle", "osc2.octave": -1, "osc2.level": 0.3, "f.cutoff": 1_800,
        "f.resonance": 0.3, "filter.env": 0.3, "env2.decay": 0.4, "env2.sustain": 0.4, **mod(1, "LFO", "Pitch", 0.12),
    },
    "Acid Drift": {
        "voice.mode": "Legato", "voice.glide": 0.08, "unison.mode": "Single", "osc2.level": 0.0, "f.cutoff": 300,
        "f.resonance": 0.8, "filter.env": 0.6, "env2.decay": 0.2, "env2.sustain": 0.0, "env2.release": 0.1,
        "amp.sustain": 0.9, "amp.release": 0.06, "drift": 0.25, **mod(2, "Velocity", "Cutoff", 0.25),
        **mod(3, "Mod Wheel", "Resonance", 0.2),
    },
    "Soft Organ": {
        # Two sines an octave apart, a gentle tremolo the wheel speeds up.
        "osc1.wave": "Sine", "osc2.wave": "Sine", "osc2.octave": 1, "osc2.detune": 0, "osc2.level": 0.5,
        "f.cutoff": 6_000, "f.resonance": 0.0, "filter.env": 0.0, "amp.attack": 0.004, "amp.decay": 0.1,
        "amp.sustain": 1.0, "amp.release": 0.1, "amp.velocity": 0.1, "lfo1.rate": 6.0, "lfo1.amount": 1.0,
        **mod(1, "LFO", "Amp", 0.15), **mod(2, "Velocity", "Cutoff", 0.0), **mod(3, "Mod Wheel", "LFO Rate", 0.3),
        "drift": 0.3, "unison.thickness": 0.4,
    },
    "Windy Whistle": {
        # Noise alone through a ringing low-pass that follows the keys and wanders.
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.level": 0.9, "unison.mode": "Single",
        "f.type": "Low-pass I 12 dB", "f.cutoff": 800, "f.resonance": 0.9, "filter.key": 1.0, "filter.env": 0.0,
        "lfo1.shape": "Wander", "lfo1.rate": 0.3, "lfo1.amount": 1.0, "filter.lfo": 0.35,
        **mod(1, "LFO", "Pitch", 0.0), "amp.attack": 1.2, "amp.sustain": 1.0, "amp.release": 2.0,
    },
    "Pumping Chords": {
        # Envelope 2 cycles on quarters, instant up and slow down, and ducks the volume with it.
        "osc1.shape": 0.25, "osc2.detune": 7, "unison.thickness": 0.4, "f.cutoff": 1_600, "filter.env": 0.0,
        "env2.cycle": 1, "env2.sync": 9, "env2.tilt": -1.0, "env2.hold": 0.0, **mod(1, "Envelope 2", "Amp", -0.85),
        "amp.attack": 0.01, "amp.sustain": 1.0, "amp.release": 0.4,
    },
    "Pressure Swell Lead": {
        # Pressing harder opens the filter and brings the vibrato in.
        "voice.mode": "Legato", "voice.glide": 0.03, "osc1.wave": "Shark Tooth", "osc1.shape": 0.4, "osc2.detune": 6,
        "f.cutoff": 900, "f.resonance": 0.3, "amp.attack": 0.02, "amp.release": 0.3, "lfo1.rate": 5.5,
        **mod(1, "LFO", "Pitch", 0.12), **mod(2, "Pressure", "Cutoff", 0.35), **mod(3, "Pressure", "LFO Amount", 1.0),
    },
    "Gritty Saw Bass": {
        # The mixer pushed: the ladder saturates.
        "voice.mode": "Mono", "unison.mode": "Single", "osc1.shape": 0.25, "osc1.octave": -1, "osc1.level": 1.0,
        "osc2.wave": "Saturated", "osc2.octave": -2, "osc2.detune": 0, "osc2.level": 0.9, "f.cutoff": 600,
        "f.resonance": 0.35, "filter.env": 0.4, "env2.decay": 0.25, "env2.sustain": 0.2, "amp.release": 0.1,
        "drift": 0.2,
    },
    "Glassy Keys": {
        "osc1.wave": "Triangle", "osc1.shape": 0.7, "osc2.wave": "Sine", "osc2.octave": 2, "osc2.level": 0.2,
        "osc2.detune": 0, "f.type": "Low-pass I 12 dB", "f.cutoff": 4_500, "f.resonance": 0.2, "filter.env": 0.15,
        "amp.attack": 0.002, "amp.decay": 1.4, "amp.sustain": 0.25, "amp.release": 0.9,
        **mod(2, "Velocity", "Cutoff", 0.25), "drift": 0.3, "unison.thickness": 0.5,
    },
}
