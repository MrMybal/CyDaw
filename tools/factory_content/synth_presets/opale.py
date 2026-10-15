"""Opale's factory sounds: the everyday polysynth (two deep oscillators, a
creative mixer, one filter of seven kinds). Each a few settings over the
defaults, choices by their English names; "Init" first.

Shapes (osc*.shape): 0 sine, 0.25 triangle, 0.5 saw, 0.75 square, then a
pulse narrowing to 1. Octaves by their feet ("16'"). lfo1.pitch is a depth
that grows as its square (0.15 is about a quarter tone).
"""

PRESETS = {
    "Init": {},
    "Warm Keys": {
        "osc1.shape": 0.4, "osc2.shape": 0.25, "osc2.fine": 5, "f.type": "Low-pass", "f.cutoff": 1_400,
        "f.resonance": 0.1, "filter.env": 0.3, "filter.velocity": 0.5, "filter.key": 0.6, "fenv.decay": 1.2,
        "fenv.sustain": 0.2, "amp.attack": 0.003, "amp.decay": 1.8, "amp.sustain": 0.45, "amp.release": 0.45,
        "amp.velocity": 0.6, "mod.vibrato": 0.3, "drift": 0.25,
    },
    "Electric Piano": {
        "osc1.shape": 0.0, "osc2.shape": 0.0, "osc2.octave": "2'", "osc2.fine": 0, "mix.mode": "AM",
        "mix.balance": 0.2, "f.type": "Low-pass", "f.cutoff": 900, "f.resonance": 0.05, "filter.env": 0.45,
        "filter.velocity": 0.7, "filter.key": 0.8, "fenv.attack": 0.001, "fenv.decay": 0.5, "fenv.sustain": 0.0,
        "fenv.release": 0.3, "amp.attack": 0.002, "amp.decay": 2.5, "amp.sustain": 0.0, "amp.release": 0.5,
        "amp.velocity": 0.7, "lfo1.shape": "Sine", "lfo1.rate": 4.0, "lfo1.amp": 0.2, "drift": 0.1,
    },
    "Bright Poly": {
        "osc1.unison": 3, "osc1.detune": 14, "osc1.spread": 0.8, "osc2.unison": 3, "osc2.detune": 18,
        "osc2.spread": 0.8, "osc2.fine": 5, "f.cutoff": 4_200, "f.resonance": 0.15, "filter.env": 0.35,
        "fenv.decay": 0.45, "fenv.sustain": 0.5, "amp.decay": 0.8, "amp.sustain": 0.85, "amp.release": 0.3,
    },
    "Sync Lead": {
        "voice.mode": "Legato", "voice.glide": 0.05, "osc1.hardsync": 10, "osc2.octave": "16'", "osc2.shape": 0.75,
        "osc2.fine": 0, "mix.balance": 0.35, "pitchenv.target": "Sync", "pitchenv.amount": 14, "f.type": "Low-pass",
        "f.cutoff": 5_500, "f.resonance": 0.15, "filter.env": 0.15, "fenv.attack": 0.001, "fenv.decay": 0.7,
        "fenv.sustain": 0.25, "amp.sustain": 0.9, "amp.release": 0.18, "lfo1.shape": "Sine", "lfo1.rate": 5.5,
        "lfo1.delay": 0.35, "lfo1.pitch": 0.15,
    },
    "Unison Saw Lead": {
        "voice.mode": "Legato", "voice.glide": 0.04, "osc1.unison": 5, "osc1.detune": 16, "osc1.spread": 0.8,
        "osc2.unison": 5, "osc2.detune": 28, "osc2.spread": 1.0, "osc2.fine": -4, "f.cutoff": 3_200,
        "f.resonance": 0.25, "filter.env": 0.3, "filter.drive": 0.25, "fenv.decay": 0.5, "fenv.sustain": 0.4,
        "amp.sustain": 0.9, "amp.release": 0.22, "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.delay": 0.4,
        "lfo1.pitch": 0.13,
    },
    "Hollow Pad": {
        "mix.mode": "Negative", "osc2.fine": 3, "osc1.unison": 2, "osc1.detune": 10, "osc2.unison": 2,
        "osc2.detune": 12, "f.type": "Low-pass", "f.cutoff": 1_800, "f.resonance": 0.2, "filter.env": 0.15,
        "fenv.attack": 1.0, "fenv.decay": 2.5, "fenv.sustain": 0.5, "fenv.release": 1.5, "amp.attack": 0.8,
        "amp.decay": 2.0, "amp.sustain": 0.8, "amp.release": 1.8, "lfo1.shape": "Triangle", "lfo1.rate": 0.3,
        "lfo1.morph": 0.15,
    },
    "Detuned Strings": {
        "osc1.unison": 4, "osc1.detune": 22, "osc1.spread": 0.9, "osc2.unison": 3, "osc2.detune": 16,
        "osc2.spread": 0.9, "osc2.octave": "4'", "osc2.fine": 3, "mix.balance": 0.35, "f.type": "Low-pass",
        "f.cutoff": 3_200, "f.resonance": 0.05, "filter.env": 0.1, "filter.key": 0.6, "fenv.attack": 0.4,
        "fenv.decay": 1.5, "fenv.sustain": 0.6, "amp.attack": 0.45, "amp.decay": 1.5, "amp.sustain": 0.9,
        "amp.release": 1.2, "lfo1.shape": "Sine", "lfo1.rate": 5.2, "lfo1.delay": 0.6, "lfo1.pitch": 0.08,
    },
    "Sub Bass": {
        "voice.mode": "Mono", "osc1.shape": 0.1, "osc1.sub": 0.6, "osc1.retrigger": 1, "mix.balance": 0.0,
        "mix.drive": 0.2, "f.cutoff": 600, "f.resonance": 0.05, "filter.env": 0.1, "filter.key": 0.3,
        "amp.attack": 0.003, "amp.decay": 0.6, "amp.sustain": 1.0, "amp.release": 0.12, "drift": 0.05,
    },
    "Rubber Bass": {
        "voice.mode": "Mono", "voice.glide": 0.03, "osc1.octave": "16'", "osc1.retrigger": 1, "osc2.octave": "16'",
        "osc2.shape": 0.75, "osc2.fine": -6, "osc2.retrigger": 1, "mix.balance": 0.4, "pitchenv.amount": 2,
        "f.cutoff": 280, "f.resonance": 0.55, "filter.env": 0.55, "filter.key": 0.4, "fenv.decay": 0.25,
        "fenv.sustain": 0.1, "fenv.release": 0.1, "amp.decay": 0.5, "amp.sustain": 0.7, "amp.release": 0.08,
    },
    "AM Bell": {
        "osc1.shape": 0.0, "osc2.shape": 0.0, "osc2.octave": "4'", "osc2.pitch": 5.6, "osc2.fine": 0,
        "mix.mode": "AM", "mix.balance": 0.4, "f.type": "Low-pass", "f.cutoff": 6_000, "f.resonance": 0.0,
        "filter.env": 0.2, "filter.key": 0.3, "fenv.decay": 1.5, "fenv.sustain": 0.0, "amp.attack": 0.001,
        "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 2.0, "amp.velocity": 0.6, "drift": 0.0,
    },
    "Wiping Pad": {
        "mix.mode": "Wipe", "osc1.unison": 3, "osc1.detune": 12, "osc2.shape": 0.85, "osc2.octave": "4'",
        "osc2.unison": 2, "osc2.detune": 10, "osc2.fine": 0, "lfo1.shape": "Triangle", "lfo1.rate": 0.25,
        "lfo1.balance": 0.6, "f.type": "Low-pass", "f.cutoff": 2_500, "f.resonance": 0.15, "filter.env": 0.1,
        "fenv.attack": 0.5, "fenv.decay": 2.0, "fenv.sustain": 0.6, "amp.attack": 0.6, "amp.decay": 2.0,
        "amp.sustain": 0.85, "amp.release": 1.5,
    },
    "Chord Stab": {
        "osc1.unison": 3, "osc1.detune": 14, "osc2.shape": 0.75, "osc2.fine": 6, "mix.drive": 0.25,
        "f.type": "Ladder", "f.cutoff": 900, "f.resonance": 0.3, "filter.env": 0.6, "fenv.attack": 0.001,
        "fenv.decay": 0.22, "fenv.sustain": 0.0, "amp.attack": 0.001, "amp.decay": 0.35, "amp.sustain": 0.0,
        "amp.release": 0.2,
    },
    "Pluck": {
        "osc1.shape": 0.55, "osc1.retrigger": 1, "osc2.shape": 0.25, "osc2.octave": "4'", "osc2.fine": 0,
        "osc2.retrigger": 1, "mix.balance": 0.4, "f.cutoff": 600, "f.resonance": 0.35, "filter.env": 0.65,
        "filter.key": 0.7, "filter.velocity": 0.6, "fenv.decay": 0.18, "fenv.sustain": 0.0, "amp.attack": 0.001,
        "amp.decay": 0.5, "amp.sustain": 0.0, "amp.release": 0.35,
    },
    "Brass": {
        "osc1.unison": 2, "osc1.detune": 8, "osc2.unison": 2, "osc2.detune": 10, "osc2.fine": 8, "mix.drive": 0.15,
        "f.cutoff": 900, "f.resonance": 0.1, "filter.env": 0.5, "filter.key": 0.6, "fenv.attack": 0.06,
        "fenv.decay": 0.5, "fenv.sustain": 0.55, "fenv.release": 0.3, "amp.attack": 0.03, "amp.decay": 0.4,
        "amp.sustain": 0.85, "amp.release": 0.25, "lfo1.shape": "Sine", "lfo1.rate": 5.0, "lfo1.delay": 0.5,
        "lfo1.pitch": 0.1,
    },
    "Noise Sweep FX": {
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.level": 1.0, "noise.colour": 0.2, "f.type": "Band-pass",
        "f.cutoff": 400, "f.resonance": 0.7, "filter.env": 0.5, "filter.key": 0.0, "fenv.attack": 2.0,
        "fenv.decay": 3.0, "fenv.sustain": 0.2, "fenv.release": 2.0, "lfo1.shape": "Sine", "lfo1.rate": 0.15,
        "lfo1.cutoff": 0.6, "amp.attack": 0.5, "amp.sustain": 1.0, "amp.release": 2.5,
    },
    "Rave Swoop Lead": {
        "voice.mode": "Legato", "voice.glide": 0.12, "osc1.shape": 0.6, "osc1.unison": 7, "osc1.detune": 60,
        "osc1.spread": 1.0, "osc2.shape": 0.6, "osc2.octave": "16'", "osc2.unison": 5, "osc2.detune": 45,
        "osc2.spread": 1.0, "osc2.fine": 0, "mix.balance": 0.45, "mix.drive": 0.3, "pitchenv.amount": -5,
        "fenv.attack": 0.001, "fenv.decay": 0.35, "fenv.sustain": 0.0, "filter.env": 0.1, "f.type": "Low-pass",
        "f.cutoff": 3_500, "f.resonance": 0.1, "lfo1.shape": "Triangle", "lfo1.rate": 4.5, "lfo1.morph": 0.3,
        "amp.attack": 0.005, "amp.sustain": 1.0, "amp.release": 0.4,
    },
    "Glass Keys": {
        "osc1.shape": 0.25, "osc1.octave": "4'", "osc2.shape": 0.0, "osc2.hardsync": 19, "osc2.fine": 0,
        "osc2.unison": 2, "osc2.detune": 6, "f.type": "Low-pass", "f.cutoff": 5_000, "f.resonance": 0.1,
        "filter.env": 0.2, "fenv.decay": 0.8, "fenv.sustain": 0.2, "amp.attack": 0.002, "amp.decay": 1.8,
        "amp.sustain": 0.2, "amp.release": 1.0, "amp.velocity": 0.6,
    },
    "Soft Sine Pad": {
        "osc1.shape": 0.0, "osc1.unison": 3, "osc1.detune": 10, "osc1.spread": 0.9, "osc2.shape": 0.2,
        "osc2.fine": 6, "osc2.unison": 3, "osc2.detune": 12, "osc2.spread": 0.9, "f.type": "Low-pass",
        "f.cutoff": 1_600, "f.resonance": 0.05, "filter.env": 0.1, "amp.attack": 1.2, "amp.decay": 2.0,
        "amp.sustain": 0.9, "amp.release": 2.5, "lfo1.shape": "Sine", "lfo1.rate": 4.8, "lfo1.delay": 1.0,
        "lfo1.pitch": 0.07,
    },
    "Square Lead": {
        "voice.mode": "Legato", "voice.glide": 0.03, "osc1.shape": 0.75, "osc2.shape": 0.75, "osc2.fine": -5,
        "mix.balance": 0.4, "f.cutoff": 2_800, "f.resonance": 0.2, "filter.env": 0.25, "fenv.decay": 0.4,
        "fenv.sustain": 0.5, "amp.sustain": 0.9, "amp.release": 0.15, "lfo1.shape": "Sine", "lfo1.rate": 5.6,
        "lfo1.delay": 0.3, "lfo1.pitch": 0.14,
    },
    "Sine Organ": {
        "osc1.shape": 0.0, "osc1.sub": 0.6, "osc2.shape": 0.05, "osc2.octave": "4'", "osc2.fine": 0,
        "mix.balance": 0.4, "f.type": "Low-pass", "f.cutoff": 8_000, "f.resonance": 0.0, "filter.env": 0.0,
        "filter.key": 0.0, "filter.velocity": 0.0, "amp.attack": 0.003, "amp.decay": 0.1, "amp.sustain": 1.0,
        "amp.release": 0.08, "amp.velocity": 0.0, "lfo1.shape": "Sine", "lfo1.rate": 6.2, "lfo1.amp": 0.12,
        "drift": 0.05,
    },
    "Feedback Growl Bass": {
        "voice.mode": "Mono", "osc1.octave": "16'", "osc2.octave": "16'", "osc2.shape": 0.75, "osc2.fine": -4,
        "mix.balance": 0.4, "mix.feedback": 0.6, "mix.drive": 0.4, "f.type": "Ladder", "f.cutoff": 500,
        "f.resonance": 0.4, "filter.env": 0.4, "filter.key": 0.3, "filter.drive": 0.3, "fenv.decay": 0.3,
        "fenv.sustain": 0.2, "amp.sustain": 0.9, "amp.release": 0.1,
    },
    "Digital Sign Lead": {
        "voice.mode": "Legato", "voice.glide": 0.04, "mix.mode": "Sign", "osc2.shape": 0.75, "osc2.octave": "4'",
        "osc2.pitch": 7, "osc2.fine": 0, "f.type": "Low-pass", "f.cutoff": 4_000, "f.resonance": 0.2,
        "filter.env": 0.2, "fenv.decay": 0.5, "fenv.sustain": 0.5, "lfo1.shape": "Triangle", "lfo1.rate": 3.0,
        "lfo1.balance": 0.25, "amp.sustain": 0.9, "amp.release": 0.2,
    },
    "Max Pluck": {
        "mix.mode": "Max", "osc1.retrigger": 1, "osc2.octave": "4'", "osc2.fine": 3, "osc2.retrigger": 1,
        "f.type": "Ladder", "f.cutoff": 1_200, "f.resonance": 0.45, "filter.env": 0.5, "fenv.decay": 0.15,
        "fenv.sustain": 0.0, "amp.attack": 0.001, "amp.decay": 0.3, "amp.sustain": 0.0, "amp.release": 0.2,
    },
    "Falling Ring FX": {
        "mix.mode": "AM", "mix.balance": 0.55, "osc1.shape": 0.25, "osc2.shape": 0.0, "osc2.octave": "2'",
        "osc2.fine": 0, "pitchenv.target": "Osc 2", "pitchenv.amount": 24, "fenv.attack": 0.001, "fenv.decay": 1.5,
        "fenv.sustain": 0.0, "fenv.release": 1.0, "filter.env": 0.0, "f.type": "Comb", "f.cutoff": 300,
        "f.resonance": 0.5, "filter.key": 1.0, "amp.attack": 0.002, "amp.decay": 2.5, "amp.sustain": 0.0,
        "amp.release": 1.5,
    },
    "PWM Pad": {
        "osc1.shape": 0.85, "osc1.unison": 2, "osc1.detune": 8, "osc2.shape": 0.85, "osc2.fine": 5,
        "osc2.unison": 2, "osc2.detune": 10, "lfo1.shape": "Triangle", "lfo1.rate": 0.6, "lfo1.morph": 0.2,
        "f.type": "Low-pass", "f.cutoff": 2_400, "f.resonance": 0.1, "filter.env": 0.15, "fenv.attack": 0.6,
        "fenv.decay": 2.0, "fenv.sustain": 0.5, "amp.attack": 0.5, "amp.decay": 2.0, "amp.sustain": 0.85,
        "amp.release": 1.4,
    },
}
