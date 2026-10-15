"""Alliage's factory sounds (made by build_synth_presets.py): each a few settings
over the defaults, "Init" first; keys as in Alliage's table, a choice by its
English name. The oscillator's kinds: Swarm, Wavetable, Pulse, Sine FM,
Supersquare, Sync, Additive, Noise Band; osc.timbre and osc.shape read as the
kind reads them (detune and mix, position and warp, width and PWM, index and
feedback, detune and width, sync pitch and window, tilt and register,
bandwidth and overtones). Its volume is set by the builder.
"""

PRESETS = {
    "Init": {},
    "Swarm Lead": {
        "voice.mode": "Legato", "voice.glide": 0.05, "osc.type": "Swarm", "osc.timbre": 0.32, "osc.shape": 0.6,
        "osc.spread": 0.5, "sub.level": 0.2, "f.type": "Ladder", "f.cutoff": 2_800, "f.resonance": 0.25,
        "f.drive": 0.35, "f.env": 0.3, "fenv.decay": 0.6, "fenv.sustain": 0.4, "amp.sustain": 0.9,
        "amp.release": 0.2, "lfo1.rate": 5.5, "lfo1.delay": 0.4, "lfo1.pitch": 0.12,
    },
    "Swarm Pad": {
        "osc.type": "Swarm", "osc.timbre": 0.5, "osc.shape": 0.75, "osc.spread": 1.0, "noise.level": 0.04,
        "noise.colour": 0.7, "f.type": "Low-pass", "f.cutoff": 1_600, "f.resonance": 0.15, "f.env": 0.2,
        "f.key": 0.3, "fenv.attack": 0.9, "fenv.decay": 2.0, "fenv.sustain": 0.6, "fenv.release": 1.8,
        "amp.attack": 0.8, "amp.decay": 1.5, "amp.sustain": 0.9, "amp.release": 2.0, "lfo1.shape": "Triangle",
        "lfo1.rate": 0.2, "lfo1.cutoff": 0.15,
    },
    "Wavetable Keys": {
        "osc.type": "Wavetable", "osc.table": "Harmonic", "osc.mode": "Plain", "osc.timbre": 0.35, "osc.shape": 0.0,
        "osc.env": 0.3, "osc.spread": 0.3, "f.type": "Low-pass", "f.cutoff": 3_500, "f.resonance": 0.1,
        "f.env": 0.2, "f.velocity": 0.5, "fenv.decay": 0.8, "fenv.sustain": 0.15, "amp.attack": 0.003,
        "amp.decay": 1.6, "amp.sustain": 0.35, "amp.release": 0.5, "amp.velocity": 0.6,
    },
    "PWM Strings": {
        "osc.type": "Pulse", "osc.timbre": 0.15, "osc.shape": 0.7, "osc.spread": 0.6, "f.type": "Low-pass",
        "f.cutoff": 2_600, "f.resonance": 0.1, "f.env": 0.15, "fenv.attack": 0.4, "fenv.sustain": 0.6,
        "amp.attack": 0.45, "amp.decay": 1.0, "amp.sustain": 0.9, "amp.release": 1.2, "lfo1.rate": 5.0,
        "lfo1.delay": 0.8, "lfo1.pitch": 0.06,
    },
    "FM Bell": {
        "osc.type": "Sine FM", "osc.ratio": "3.5", "osc.timbre": 0.35, "osc.shape": 0.0, "osc.env": 0.35,
        "osc.spread": 0.2, "f.type": "Low-pass", "f.cutoff": 12_000, "f.resonance": 0.0, "f.drive": 0.0,
        "f.env": 0.0, "f.key": 0.0, "fenv.attack": 0.001, "fenv.decay": 1.8, "fenv.sustain": 0.0,
        "fenv.release": 1.5, "amp.attack": 0.001, "amp.decay": 3.0, "amp.sustain": 0.0, "amp.release": 2.0,
    },
    "FM Bass": {
        "voice.mode": "Mono", "osc.type": "Sine FM", "osc.ratio": "1", "osc.octave": -1, "osc.timbre": 0.25,
        "osc.shape": 0.35, "osc.env": 0.35, "osc.spread": 0.0, "sub.level": 0.3, "f.type": "Ladder",
        "f.cutoff": 1_800, "f.resonance": 0.1, "f.drive": 0.3, "f.env": 0.15, "fenv.decay": 0.25,
        "fenv.sustain": 0.1, "amp.attack": 0.002, "amp.decay": 0.5, "amp.sustain": 0.8, "amp.release": 0.1,
    },
    "Sync Lead": {
        "voice.mode": "Legato", "voice.glide": 0.04, "osc.type": "Sync", "osc.timbre": 0.3, "osc.shape": 0.0,
        "osc.env": 0.35, "osc.spread": 0.25, "f.type": "Low-pass", "f.cutoff": 5_000, "f.resonance": 0.15,
        "f.env": 0.1, "fenv.decay": 0.7, "fenv.sustain": 0.25, "amp.sustain": 0.9, "amp.release": 0.2,
        "lfo1.rate": 5.5, "lfo1.delay": 0.4, "lfo1.pitch": 0.12,
    },
    "Organ Additive": {
        "osc.type": "Additive", "osc.timbre": 0.75, "osc.shape": 1.0, "osc.spread": 0.1, "sub.level": 0.35,
        "f.type": "Low-pass", "f.cutoff": 9_000, "f.resonance": 0.0, "f.drive": 0.25, "f.env": 0.0, "f.key": 0.0,
        "amp.attack": 0.005, "amp.decay": 0.1, "amp.sustain": 1.0, "amp.release": 0.08, "amp.velocity": 0.0,
        "lfo1.rate": 6.5, "lfo1.pitch": 0.06,
    },
    "Noise Wind": {
        "osc.type": "Noise Band", "osc.octave": 1, "osc.timbre": 0.45, "osc.shape": 0.2, "osc.spread": 1.0,
        "noise.level": 0.15, "noise.colour": 0.3, "f.type": "Low-pass", "f.cutoff": 3_000, "f.resonance": 0.2,
        "f.env": 0.0, "f.key": 0.3, "amp.attack": 1.0, "amp.decay": 2.0, "amp.sustain": 0.9, "amp.release": 2.5,
        "lfo1.shape": "Triangle", "lfo1.rate": 0.15, "lfo1.timbre": 0.5, "lfo1.cutoff": 0.4,
    },
    "Comb Pluck": {
        "osc.type": "Noise Band", "osc.timbre": 1.0, "osc.shape": 0.0, "osc.spread": 0.3, "osc.level": 1.0, "f.type": "Comb",
        "f.cutoff": 261.6, "f.key": 1.0, "f.resonance": 0.85, "f.drive": 0.0, "f.env": 0.0, "f.velocity": 0.0,
        "amp.attack": 0.001, "amp.decay": 0.9, "amp.sustain": 0.0, "amp.release": 0.5,
    },
    "Vowel Pad": {
        "osc.type": "Swarm", "osc.timbre": 0.3, "osc.shape": 0.5, "osc.spread": 0.8, "f.type": "Vowel",
        "f.cutoff": 400, "f.resonance": 0.6, "f.drive": 0.2, "f.env": 0.0, "f.key": 0.0, "amp.attack": 0.6,
        "amp.decay": 1.5, "amp.sustain": 0.9, "amp.release": 1.8, "lfo1.shape": "Triangle", "lfo1.rate": 0.12,
        "lfo1.cutoff": 0.35,
    },
    "Ladder Bass": {
        "voice.mode": "Mono", "osc.type": "Swarm", "osc.octave": -1, "osc.timbre": 0.08, "osc.shape": 0.25,
        "osc.spread": 0.0, "sub.level": 0.45, "f.type": "Ladder", "f.cutoff": 300, "f.resonance": 0.35,
        "f.drive": 0.45, "f.env": 0.5, "f.key": 0.3, "fenv.decay": 0.3, "fenv.sustain": 0.12, "amp.attack": 0.002,
        "amp.decay": 0.6, "amp.sustain": 0.85, "amp.release": 0.1,
    },
    "Square Chords": {
        "osc.type": "Supersquare", "osc.timbre": 0.25, "osc.shape": 0.1, "osc.spread": 0.7, "f.type": "Low-pass",
        "f.cutoff": 2_400, "f.resonance": 0.15, "f.env": 0.3, "fenv.decay": 0.5, "fenv.sustain": 0.3,
        "amp.attack": 0.005, "amp.decay": 1.0, "amp.sustain": 0.6, "amp.release": 0.4,
    },
    "Acid Line": {
        "voice.mode": "Legato", "voice.glide": 0.07, "osc.type": "Sync", "osc.octave": -1, "osc.timbre": 0.0,
        "osc.shape": 0.0, "osc.spread": 0.0, "f.type": "Ladder", "f.cutoff": 320, "f.resonance": 0.8,
        "f.drive": 0.55, "f.env": 0.65, "f.key": 0.3, "f.velocity": 0.6, "fenv.attack": 0.001, "fenv.decay": 0.18,
        "fenv.sustain": 0.0, "fenv.release": 0.1, "amp.attack": 0.001, "amp.decay": 0.4, "amp.sustain": 0.85,
        "amp.release": 0.06,
    },
    "Metallic Hit": {
        "osc.type": "Sine FM", "osc.ratio": "1.41", "osc.timbre": 0.55, "osc.shape": 0.4, "osc.env": 0.3,
        "osc.spread": 0.0, "f.type": "High-pass", "f.cutoff": 250, "f.resonance": 0.2, "f.env": 0.0, "f.key": 0.0,
        "fenv.decay": 0.25, "fenv.sustain": 0.0, "pitch.env": 12, "pitch.decay": 0.05, "amp.attack": 0.001,
        "amp.decay": 0.6, "amp.sustain": 0.0, "amp.release": 0.5,
    },
    "Soft Brass": {
        "osc.type": "Swarm", "osc.timbre": 0.2, "osc.shape": 0.4, "osc.spread": 0.4, "f.type": "Ladder",
        "f.cutoff": 650, "f.resonance": 0.1, "f.env": 0.5, "f.velocity": 0.6, "fenv.attack": 0.08,
        "fenv.decay": 0.6, "fenv.sustain": 0.4, "fenv.release": 0.3, "amp.attack": 0.04, "amp.decay": 0.6,
        "amp.sustain": 0.85, "amp.release": 0.3, "lfo1.rate": 5.0, "lfo1.delay": 0.5, "lfo1.pitch": 0.1,
    },
    "Dark Drone": {
        "osc.type": "Wavetable", "osc.table": "Metal", "osc.mode": "Mirror", "osc.octave": -1, "osc.timbre": 0.3,
        "osc.shape": 0.3, "osc.spread": 0.8, "sub.level": 0.4, "noise.level": 0.08, "noise.colour": 0.2,
        "f.type": "Low-pass", "f.cutoff": 700, "f.resonance": 0.3, "f.env": 0.0, "f.key": 0.2,
        "amp.attack": 1.5, "amp.decay": 2.0, "amp.sustain": 1.0, "amp.release": 3.0, "lfo1.shape": "Triangle",
        "lfo1.rate": 0.08, "lfo1.timbre": 0.6, "lfo1.cutoff": 0.2,
    },
    "Bright Arp": {
        "osc.type": "Pulse", "osc.timbre": 0.3, "osc.shape": 0.0, "osc.spread": 0.35, "f.type": "Low-pass",
        "f.cutoff": 1_800, "f.resonance": 0.4, "f.env": 0.5, "fenv.decay": 0.18, "fenv.sustain": 0.0,
        "amp.attack": 0.001, "amp.decay": 0.35, "amp.sustain": 0.0, "amp.release": 0.25,
    },
    "Hollow Reed": {
        "voice.mode": "Legato", "voice.glide": 0.03, "osc.type": "Additive", "osc.timbre": 0.55, "osc.shape": 0.5,
        "osc.spread": 0.0, "noise.level": 0.06, "noise.colour": 0.6, "f.type": "Low-pass", "f.cutoff": 3_000,
        "f.resonance": 0.1, "f.env": 0.15, "fenv.attack": 0.05, "fenv.decay": 0.5, "fenv.sustain": 0.5,
        "amp.attack": 0.06, "amp.decay": 0.4, "amp.sustain": 0.9, "amp.release": 0.2, "lfo1.rate": 5.0,
        "lfo1.delay": 0.5, "lfo1.pitch": 0.08,
    },
    "Deep Sine Bass": {
        "voice.mode": "Mono", "osc.type": "Sine FM", "osc.octave": -1, "osc.timbre": 0.0, "osc.shape": 0.0,
        "osc.spread": 0.0, "f.type": "Low-pass", "f.cutoff": 2_000, "f.resonance": 0.0, "f.drive": 0.1,
        "f.env": 0.0, "amp.attack": 0.004, "amp.sustain": 1.0, "amp.release": 0.1,
    },
    "Digital Lead": {
        "voice.mode": "Legato", "voice.glide": 0.05, "osc.type": "Wavetable", "osc.table": "Digital",
        "osc.mode": "Bend", "osc.timbre": 0.4, "osc.shape": 0.25, "osc.env": 0.3, "osc.spread": 0.4,
        "f.type": "Low-pass", "f.cutoff": 4_000, "f.resonance": 0.2, "f.env": 0.15, "fenv.decay": 0.5,
        "fenv.sustain": 0.3, "amp.sustain": 0.9, "amp.release": 0.25, "lfo1.rate": 5.5, "lfo1.delay": 0.3,
        "lfo1.pitch": 0.12,
    },
    "Choir Pad": {
        "osc.type": "Wavetable", "osc.table": "Voices", "osc.mode": "Plain", "osc.timbre": 0.4, "osc.shape": 0.0,
        "osc.spread": 0.7, "noise.level": 0.05, "noise.colour": 0.65, "f.type": "Low-pass", "f.cutoff": 3_500,
        "f.resonance": 0.1, "f.env": 0.0, "amp.attack": 0.7, "amp.decay": 1.5, "amp.sustain": 0.9,
        "amp.release": 2.0, "lfo1.shape": "Triangle", "lfo1.rate": 0.15, "lfo1.timbre": 0.4,
    },
    "Sync Pluck": {
        "osc.type": "Sync", "osc.timbre": 0.15, "osc.shape": 0.2, "osc.env": 0.6, "osc.spread": 0.3,
        "f.type": "Low-pass", "f.cutoff": 3_000, "f.resonance": 0.2, "f.env": 0.3, "fenv.decay": 0.2,
        "fenv.sustain": 0.0, "amp.attack": 0.001, "amp.decay": 0.45, "amp.sustain": 0.0, "amp.release": 0.3,
    },
    "Screaming Lead": {
        "voice.mode": "Legato", "voice.glide": 0.06, "osc.type": "Swarm", "osc.timbre": 0.25, "osc.shape": 0.5,
        "osc.spread": 0.3, "f.type": "Scream", "f.cutoff": 1_400, "f.resonance": 0.55, "f.drive": 0.6,
        "f.env": 0.25, "fenv.decay": 0.6, "fenv.sustain": 0.3, "amp.sustain": 0.9, "amp.release": 0.2,
        "lfo1.rate": 5.5, "lfo1.delay": 0.4, "lfo1.pitch": 0.12, "lfo1.cutoff": 0.1,
    },
    "Laser Zap": {
        "osc.type": "Sine FM", "osc.ratio": "2", "osc.timbre": 0.4, "osc.shape": 0.5, "osc.env": 0.3,
        "osc.spread": 0.0, "pitch.env": 24, "pitch.decay": 0.18, "f.type": "Low-pass", "f.cutoff": 8_000,
        "f.env": 0.0, "fenv.decay": 0.2, "fenv.sustain": 0.0, "amp.attack": 0.001, "amp.decay": 0.3,
        "amp.sustain": 0.0, "amp.release": 0.2,
    },
}
