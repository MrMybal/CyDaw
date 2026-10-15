"""Argile's factory sounds: a classic virtual-analog synth with two of everything (two oscillators with their subs,
a noise, two filters side by side or in series, two amplifiers each with its envelope and pan, two LFOs).

Each sound is a few settings over the defaults ("Init" first): keys as in Argile's table ("osc1.wave",
"filter1.cutoff", "amp2.pan"); a choice by its English name ("Saw", "Rectangle", "LP24", "Formant", "Legato");
a toggle as 1 or 0. tools/factory_content/build_synth_presets.py measures each one, brings its volume to the others'
and saves it as a device preset.
"""

PRESETS = {
    "Init": {},
    # Basses.
    "Fat Saw Bass": {
        "voice.mode": "Mono", "osc1.octave": -1, "osc1.sub": 0.45, "osc2.octave": -1, "osc2.fine": -8, "osc2.level": 0.7,
        "unison.voices": 2, "unison.detune": 8, "unison.spread": 0.25,
        "filter1.cutoff": 280, "filter1.resonance": 0.3, "filter1.drive": 0.45, "filter1.env": 0.55, "filter1.key": 0.3,
        "fenv1.attack": 0.001, "fenv1.decay": 0.35, "fenv1.sustain": 0.15, "fenv1.release": 0.15,
        "amp1.attack": 0.002, "amp1.decay": 0.5, "amp1.sustain": 0.9, "amp1.release": 0.12, "drift": 0.15,
    },
    "Rectangle Bass": {
        "voice.mode": "Mono", "osc1.wave": "Rectangle", "osc1.octave": -1, "osc1.pw": 0.35,
        "osc2.wave": "Rectangle", "osc2.octave": -1, "osc2.pw": 0.6, "osc2.fine": -5, "osc2.level": 0.6,
        "filter1.cutoff": 420, "filter1.resonance": 0.35, "filter1.env": 0.45, "filter1.key": 0.3,
        "fenv1.attack": 0.001, "fenv1.decay": 0.25, "fenv1.sustain": 0.2,
        "amp1.attack": 0.002, "amp1.sustain": 0.9, "amp1.release": 0.1,
        "lfo2.pw": 0.3, "lfo2.rate": 0.7, "drift": 0.15,
    },
    "Sub Sine Bass": {
        "voice.mode": "Mono", "osc1.wave": "Sine", "osc1.octave": -1, "osc1.level": 0.9, "osc1.sub": 0.2, "osc2.level": 0.0,
        "filter1.type": "LP12", "filter1.cutoff": 700, "filter1.resonance": 0.0, "filter1.env": 0.1, "filter1.drive": 0.35,
        "filter1.key": 0.4, "amp1.attack": 0.003, "amp1.sustain": 1.0, "amp1.release": 0.1, "drift": 0.05,
    },
    "Dark Bass": {
        "voice.mode": "Mono", "osc1.octave": -1, "osc2.wave": "Rectangle", "osc2.octave": -2, "osc2.fine": 0, "osc2.level": 0.6,
        "filter1.cutoff": 160, "filter1.resonance": 0.15, "filter1.drive": 0.6, "filter1.env": 0.25, "filter1.key": 0.3,
        "fenv1.decay": 0.5, "fenv1.sustain": 0.3,
        "amp1.attack": 0.003, "amp1.sustain": 0.9, "amp1.release": 0.15, "drift": 0.2,
    },
    "Talking Bass": {
        # The first oscillator speaks through the formant filter, the second holds the low end through filter 2.
        "voice.mode": "Mono", "osc1.octave": -1, "osc2.octave": -1, "osc2.fine": -6, "osc2.level": 0.7, "osc2.route": 1.0,
        "filter1.type": "Formant", "filter1.cutoff": 900, "filter1.resonance": 0.45, "filter1.vowel": 0.0, "filter1.env": 0.5,
        "filter1.drive": 0.3, "filter1.key": 0.2, "fenv1.attack": 0.005, "fenv1.decay": 0.35, "fenv1.sustain": 0.1,
        "filter2.type": "LP24", "filter2.cutoff": 300, "filter2.resonance": 0.2, "filter2.env": 0.2, "filter2.key": 0.3,
        "amp1.attack": 0.003, "amp1.sustain": 0.9, "amp1.release": 0.12,
        "amp2.attack": 0.003, "amp2.sustain": 0.9, "amp2.release": 0.12, "amp2.level": 0.7,
    },
    # Leads.
    "Classic Lead": {
        "voice.mode": "Legato", "voice.glide": 0.06, "osc2.fine": 9, "osc2.level": 0.7,
        "filter1.cutoff": 2600, "filter1.resonance": 0.3, "filter1.drive": 0.4, "filter1.env": 0.35,
        "fenv1.decay": 0.5, "fenv1.sustain": 0.4, "amp1.sustain": 0.9, "amp1.release": 0.2,
        "lfo1.pitch": 0.1, "lfo1.rate": 5.5, "lfo1.delay": 0.4, "mod.wheel": 0.6,
    },
    "Wide Unison Lead": {
        "voice.mode": "Legato", "voice.glide": 0.05, "unison.voices": 4, "unison.detune": 22, "unison.spread": 0.9,
        "osc2.octave": 1, "osc2.fine": 0, "osc2.level": 0.35,
        "filter1.cutoff": 3200, "filter1.resonance": 0.2, "filter1.drive": 0.3, "filter1.env": 0.25,
        "fenv1.decay": 0.7, "fenv1.sustain": 0.5, "amp1.sustain": 0.9, "amp1.release": 0.3,
        "lfo1.pitch": 0.08, "lfo1.delay": 0.5, "mod.wheel": 0.7,
        "amp1.level": 1.0,
    },
    "Sync Sweep": {
        # Filter envelope 1 sweeps the synced second oscillator.
        "voice.mode": "Legato", "voice.glide": 0.03, "osc1.level": 0.25, "osc2.sync": 1, "osc2.fine": 0, "osc2.level": 0.9,
        "osc2.env": 0.55, "filter1.cutoff": 5000, "filter1.resonance": 0.15, "filter1.env": 0.1, "filter1.drive": 0.3,
        "fenv1.attack": 0.005, "fenv1.decay": 1.2, "fenv1.sustain": 0.2, "fenv1.release": 0.5,
        "amp1.sustain": 0.9, "amp1.release": 0.25, "lfo1.pitch": 0.08, "lfo1.delay": 0.4,
    },
    "Serial Filter Lead": {
        # A resonant high-pass into a driven low-pass: only amplifier 2 is heard.
        "voice.mode": "Legato", "voice.glide": 0.04, "osc2.wave": "Rectangle", "osc2.pw": 0.35, "osc2.fine": 8, "osc2.level": 0.6,
        "filter1.type": "HP12", "filter1.cutoff": 400, "filter1.resonance": 0.3, "filter1.env": 0.0, "filter1.key": 0.6,
        "filter.serial": 1.0, "filter2.type": "LP24", "filter2.cutoff": 2200, "filter2.resonance": 0.45, "filter2.drive": 0.35,
        "filter2.env": 0.4, "fenv2.decay": 0.45, "fenv2.sustain": 0.35,
        "amp2.attack": 0.005, "amp2.sustain": 0.9, "amp2.release": 0.2,
        "lfo1.pitch": 0.1, "lfo1.delay": 0.35, "mod.wheel": 0.6,
    },
    # Pads and ensembles.
    "Dual Filter Pad": {
        "unison.voices": 2, "unison.detune": 10, "unison.spread": 0.6,
        "osc2.fine": -10, "osc2.level": 0.8, "osc2.route": 1.0,
        "filter1.cutoff": 900, "filter1.resonance": 0.25, "filter1.env": 0.2,
        "filter2.type": "BP12", "filter2.cutoff": 1400, "filter2.resonance": 0.45, "filter2.env": 0.15,
        "fenv1.attack": 1.0, "fenv1.decay": 2.0, "fenv1.sustain": 0.5, "fenv1.release": 1.5,
        "fenv2.attack": 1.4, "fenv2.decay": 2.5, "fenv2.sustain": 0.5, "fenv2.release": 1.5,
        "amp1.attack": 0.8, "amp1.decay": 2.0, "amp1.sustain": 0.8, "amp1.release": 1.8, "amp1.pan": -0.45,
        "amp2.attack": 1.0, "amp2.decay": 2.0, "amp2.sustain": 0.8, "amp2.release": 1.8, "amp2.pan": 0.45,
        "lfo2.rate": 0.2, "lfo2.cutoff1": 0.25, "lfo2.cutoff2": -0.3, "drift": 0.35,
        "amp1.level": 1.0, "amp2.level": 1.0,
    },
    "Stereo Pad": {
        "osc2.wave": "Rectangle", "osc2.pw": 0.4, "osc2.fine": 6, "osc2.level": 0.7, "osc2.route": 1.0,
        "filter1.cutoff": 1100, "filter1.env": 0.25, "filter2.type": "LP24", "filter2.cutoff": 1300, "filter2.env": 0.25,
        "fenv1.attack": 0.9, "fenv1.decay": 2.0, "fenv1.sustain": 0.6, "fenv1.release": 2.0,
        "fenv2.attack": 1.1, "fenv2.decay": 2.0, "fenv2.sustain": 0.6, "fenv2.release": 2.0,
        "amp1.attack": 0.7, "amp1.decay": 2.0, "amp1.sustain": 0.85, "amp1.release": 2.2, "amp1.pan": -0.8,
        "amp2.attack": 0.7, "amp2.decay": 2.0, "amp2.sustain": 0.85, "amp2.release": 2.2, "amp2.pan": 0.8,
        "lfo1.pw": 0.25, "lfo1.rate": 0.4, "lfo1.delay": 0.0,
        "lfo2.rate": 0.15, "lfo2.cutoff1": 0.2, "lfo2.cutoff2": -0.2, "drift": 0.35,
    },
    "Strings Ensemble": {
        "unison.voices": 3, "unison.detune": 18, "unison.spread": 0.85, "drift": 0.4,
        "osc2.fine": 12, "osc2.level": 0.8,
        "filter1.type": "LP12", "filter1.cutoff": 3200, "filter1.resonance": 0.05, "filter1.env": 0.15, "filter1.key": 0.6,
        "fenv1.attack": 0.3, "fenv1.decay": 1.5, "fenv1.sustain": 0.6, "fenv1.release": 1.0,
        "amp1.attack": 0.35, "amp1.decay": 1.0, "amp1.sustain": 0.9, "amp1.release": 1.1, "amp1.velocity": 0.3,
        "lfo1.pitch": 0.06, "lfo1.rate": 5.2, "lfo1.delay": 0.6,
    },
    "Brass Section": {
        "unison.voices": 2, "unison.detune": 9, "unison.spread": 0.5, "osc2.fine": -6, "osc2.level": 0.8,
        "filter1.cutoff": 700, "filter1.resonance": 0.1, "filter1.drive": 0.3, "filter1.env": 0.55, "filter.velocity": 0.5,
        "fenv1.attack": 0.08, "fenv1.decay": 0.5, "fenv1.sustain": 0.55, "fenv1.release": 0.3,
        "amp1.attack": 0.04, "amp1.decay": 0.6, "amp1.sustain": 0.85, "amp1.release": 0.25,
        "lfo1.pitch": 0.06, "lfo1.rate": 5.0, "lfo1.delay": 0.5,
        "amp1.level": 0.9,
    },
    "Formant Voice": {
        "osc2.wave": "Rectangle", "osc2.pw": 0.3, "osc2.fine": 5, "osc2.level": 0.4, "noise.level": 0.05,
        "filter1.type": "Formant", "filter1.cutoff": 1000, "filter1.resonance": 0.5, "filter1.vowel": 0.1, "filter1.env": 0.3,
        "filter1.drive": 0.2, "filter1.key": 0.3,
        "fenv1.attack": 0.25, "fenv1.decay": 0.8, "fenv1.sustain": 0.4, "fenv1.release": 0.5,
        "amp1.attack": 0.08, "amp1.sustain": 0.9, "amp1.release": 0.5,
        "lfo1.pitch": 0.08, "lfo1.rate": 5.2, "lfo1.delay": 0.35,
        "lfo2.shape": "Sine", "lfo2.rate": 0.35, "lfo2.cutoff1": 0.25,
    },
    # Keys.
    "Bright Poly": {
        "unison.voices": 2, "unison.detune": 8, "unison.spread": 0.6,
        "osc2.wave": "Rectangle", "osc2.pw": 0.45, "osc2.fine": 6, "osc2.level": 0.55,
        "filter1.type": "LP12", "filter1.cutoff": 4200, "filter1.resonance": 0.15, "filter1.env": 0.3, "filter1.key": 0.6,
        "fenv1.decay": 0.9, "fenv1.sustain": 0.45,
        "amp1.attack": 0.006, "amp1.decay": 1.0, "amp1.sustain": 0.75, "amp1.release": 0.45,
        "lfo2.pw": 0.2, "lfo2.rate": 0.6,
    },
    "Organ Square": {
        "osc1.wave": "Rectangle", "osc1.level": 0.7, "osc1.sub": 0.5,
        "osc2.wave": "Rectangle", "osc2.octave": 1, "osc2.fine": 0, "osc2.level": 0.45,
        "filter1.type": "LP12", "filter1.cutoff": 4500, "filter1.resonance": 0.0, "filter1.env": 0.0, "filter1.key": 0.3,
        "amp1.attack": 0.005, "amp1.decay": 0.1, "amp1.sustain": 1.0, "amp1.release": 0.08,
        "lfo1.pitch": 0.05, "lfo1.rate": 6.5, "lfo1.delay": 0.0, "drift": 0.1,
    },
    "Notch Phaser Keys": {
        # Two moving notches, one on each side.
        "osc1.wave": "Rectangle", "osc2.fine": 5, "osc2.level": 0.6, "osc2.route": 1.0,
        "filter1.type": "Notch", "filter1.cutoff": 1200, "filter1.resonance": 0.4, "filter1.env": 0.35,
        "filter2.type": "Notch", "filter2.cutoff": 2400, "filter2.resonance": 0.4, "filter2.env": 0.35,
        "fenv1.decay": 0.8, "fenv1.sustain": 0.3, "fenv2.decay": 0.8, "fenv2.sustain": 0.3,
        "amp1.decay": 0.8, "amp1.sustain": 0.55, "amp1.release": 0.4, "amp1.pan": -0.35,
        "amp2.decay": 0.8, "amp2.sustain": 0.55, "amp2.release": 0.4, "amp2.pan": 0.35,
        "lfo2.rate": 0.3, "lfo2.cutoff1": 0.25, "lfo2.cutoff2": -0.25,
    },
    "Tremolo Pan Keys": {
        "osc1.wave": "Sine", "osc2.wave": "Rectangle", "osc2.octave": 1, "osc2.fine": 0, "osc2.pw": 0.3, "osc2.level": 0.3,
        "filter1.type": "LP12", "filter1.cutoff": 2200, "filter1.env": 0.3,
        "amp1.decay": 1.2, "amp1.sustain": 0.3, "amp1.release": 0.6, "amp1.velocity": 0.6,
        "lfo2.shape": "Sine", "lfo2.rate": 2.5, "lfo2.pan": 0.8, "lfo2.amp": 0.2,
        "amp1.level": 1.0,
    },
    # Plucks and bells.
    "Pluck Arp": {
        "osc2.wave": "Rectangle", "osc2.pw": 0.3, "osc2.octave": 1, "osc2.fine": 0, "osc2.level": 0.5,
        "filter1.cutoff": 400, "filter1.resonance": 0.35, "filter1.env": 0.6, "filter1.drive": 0.2, "filter.velocity": 0.5,
        "fenv1.attack": 0.001, "fenv1.decay": 0.22, "fenv1.sustain": 0.0, "fenv1.release": 0.2,
        "amp1.attack": 0.001, "amp1.decay": 0.45, "amp1.sustain": 0.0, "amp1.release": 0.3, "amp1.velocity": 0.6,
        "amp1.level": 1.0,
    },
    "Glass Pluck": {
        "osc1.wave": "Rectangle", "osc1.pw": 0.25, "osc2.wave": "Sine", "osc2.octave": 2, "osc2.fine": 0, "osc2.level": 0.3,
        "filter1.type": "BP6", "filter1.cutoff": 1800, "filter1.resonance": 0.5, "filter1.env": 0.4, "filter1.key": 0.8,
        "fenv1.attack": 0.001, "fenv1.decay": 0.3, "fenv1.sustain": 0.0, "fenv1.release": 0.3,
        "amp1.attack": 0.001, "amp1.decay": 0.6, "amp1.sustain": 0.0, "amp1.release": 0.5,
        "amp1.level": 1.0,
    },
    "Soft Bells": {
        # The high partial through its own amplifier, dying sooner, as a bell's do.
        "osc1.wave": "Sine", "osc1.level": 0.8, "osc2.wave": "Sine", "osc2.octave": 2, "osc2.semi": 4, "osc2.fine": 0,
        "osc2.level": 0.35, "osc2.route": 1.0,
        "filter1.type": "LP12", "filter1.cutoff": 6000, "filter1.resonance": 0.0, "filter1.env": 0.0,
        "filter2.type": "LP12", "filter2.cutoff": 9000, "filter2.resonance": 0.0, "filter2.env": 0.0,
        "amp1.attack": 0.002, "amp1.decay": 2.5, "amp1.sustain": 0.0, "amp1.release": 2.0, "amp1.velocity": 0.6, "amp1.pan": -0.15,
        "amp2.attack": 0.001, "amp2.decay": 0.6, "amp2.sustain": 0.0, "amp2.release": 0.6, "amp2.velocity": 0.8, "amp2.pan": 0.15,
        "drift": 0.1,
    },
    # Percussion and effects.
    "Noise Percussion": {
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.level": 1.0, "noise.colour": 0.65,
        "filter1.type": "BP12", "filter1.cutoff": 2200, "filter1.resonance": 0.35, "filter1.env": 0.4, "filter1.key": 0.8,
        "fenv1.attack": 0.001, "fenv1.decay": 0.12, "fenv1.sustain": 0.0, "fenv1.release": 0.1,
        "amp1.attack": 0.001, "amp1.decay": 0.18, "amp1.sustain": 0.0, "amp1.release": 0.15, "amp1.velocity": 0.8, "drift": 0.0,
        "amp1.level": 1.0,
    },
    "Resonant Sweep": {
        "osc2.fine": -7, "osc2.level": 0.7,
        "filter1.cutoff": 300, "filter1.resonance": 0.75, "filter1.env": 0.25, "filter1.drive": 0.3,
        "fenv1.attack": 0.5, "fenv1.decay": 2.0, "fenv1.sustain": 0.4, "fenv1.release": 1.0,
        "amp1.attack": 0.02, "amp1.sustain": 0.9, "amp1.release": 0.8,
        "lfo2.shape": "Triangle", "lfo2.rate": 0.12, "lfo2.cutoff1": 0.55,
    },
    "Wind FX": {
        "osc1.level": 0.0, "osc2.level": 0.0, "noise.level": 0.9, "noise.colour": 0.4, "noise.route": 0.5,
        "filter1.type": "BP12", "filter1.cutoff": 900, "filter1.resonance": 0.6, "filter1.env": 0.0, "filter1.key": 0.2,
        "filter2.type": "BP12", "filter2.cutoff": 1500, "filter2.resonance": 0.6, "filter2.env": 0.0, "filter2.key": 0.2,
        "amp1.attack": 1.2, "amp1.decay": 2.0, "amp1.sustain": 0.9, "amp1.release": 2.5, "amp1.pan": -0.6,
        "amp2.attack": 1.5, "amp2.decay": 2.0, "amp2.sustain": 0.9, "amp2.release": 2.5, "amp2.pan": 0.6,
        "lfo1.shape": "Random", "lfo1.rate": 2.0, "lfo1.delay": 0.0, "lfo1.cutoff1": 0.12, "lfo1.cutoff2": -0.12,
        "lfo2.shape": "Sine", "lfo2.rate": 0.15, "lfo2.cutoff1": 0.45, "lfo2.cutoff2": -0.4, "drift": 0.0,
        "amp1.level": 1.0, "amp2.level": 1.0,
    },
}
