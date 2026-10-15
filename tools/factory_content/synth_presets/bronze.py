"""Bronze's factory sounds: struck, bowed and blown objects (see build_synth_presets.py).

A sound is a few settings over the defaults ("Init" first): keys as in Bronze's table ("r1.type",
"mallet.stiffness"); a choice by its English name ("Marimba", "Band-pass", "1 > 2"). The defaults are a
marimba bar ringing into a tube (structure "1 > 2"), struck by a medium mallet.
"""

PRESETS = {
    "Init": {},
    # ---- Bars and keys --------------------------------------------------------------------------
    "Marimba": {
        # A wooden bar tuned 1:4:10, its tube below it tuned to the note; a rubber mallet that hardens with the velocity.
        "mallet.stiffness": 0.35, "mallet.noise": 0.08, "mallet.colour": 0.3, "velocity.stiffness": 0.7,
        "r1.type": "Marimba", "r1.quality": 20, "r1.decay": 1.4, "r1.material": 0.55, "r1.bright": 0.5, "r1.hit": 0.15,
        "r2.type": "Tube", "r2.decay": 0.9, "r2.material": 0.4, "r2.bright": 0.35,
        "structure": "1 > 2", "balance": 0.4, "release": 0.8,
    },
    "Vibes": {
        # Metal bars tuned 1:4:10 over their tubes, a long ring, the tremolo of turning discs.
        "mallet.stiffness": 0.4, "mallet.noise": 0.05, "r1.type": "Marimba", "r1.decay": 4.5, "r1.material": 0.0,
        "r1.bright": 0.6, "r1.hit": 0.1, "r2.type": "Tube", "r2.decay": 3.0, "r2.material": 0.1, "r2.bright": 0.4,
        "structure": "1 > 2", "balance": 0.4, "release": 2.0, "lfo1.shape": "Sine", "lfo1.rate": 5.5, "lfo1.volume": 0.45,
    },
    "Glockenspiel": {
        # Small steel bars two octaves up, a hard mallet, a bright click.
        "mallet.stiffness": 0.85, "mallet.noise": 0.1, "mallet.colour": 0.8, "r1.type": "Beam", "r1.quality": 12,
        "r1.tune": 24, "r1.decay": 3.0, "r1.material": 0.1, "r1.bright": 0.75, "r1.hit": 0.3,
        "structure": "1 + 2", "balance": 0.0, "release": 1.5,
    },
    "Kalimba": {
        # A plucked metal tine (its partials stretched far apart) on a small wooden box.
        "mallet.stiffness": 0.6, "mallet.noise": 0.3, "mallet.colour": 0.6, "r1.type": "Marimba", "r1.decay": 1.6,
        "r1.material": 0.25, "r1.bright": 0.5, "r1.inharm": 0.6, "r1.hit": 0.85, "r2.type": "Tube", "r2.decay": 0.25,
        "r2.material": 0.5, "r2.bright": 0.3, "structure": "1 > 2", "balance": 0.3, "release": 1.2,
    },
    "Steel Drum": {
        # A harmonic note area that rings bright, a plate an octave up for the metal; a rubber-tipped stick.
        "mallet.stiffness": 0.55, "mallet.noise": 0.05, "r1.type": "String", "r1.decay": 1.0, "r1.material": -0.2,
        "r1.bright": 0.6, "r1.inharm": 0.15, "r1.hit": 0.35, "r2.type": "Plate", "r2.tune": 12, "r2.decay": 0.5,
        "r2.material": 0.2, "r2.bright": 0.6, "structure": "1 + 2", "balance": 0.3, "release": 0.8,
    },
    "Tubular Bell": {
        # A long free tube struck at its top: its pitch heard about two octaves above its lowest mode.
        "mallet.stiffness": 0.7, "mallet.noise": 0.1, "r1.type": "Beam", "r1.quality": 16, "r1.tune": -26,
        "r1.decay": 6.0, "r1.material": 0.1, "r1.bright": 0.85, "r1.hit": 0.98, "structure": "1 + 2", "balance": 0.0,
        "release": 2.5, "voice.count": 8,
    },
    "Metal Rod": {
        # A steel rod struck hard at one end, listened to near the ends: long and glassy.
        "mallet.stiffness": 0.9, "mallet.noise": 0.15, "mallet.colour": 0.85, "r1.type": "Beam", "r1.decay": 6.0,
        "r1.material": 0.0, "r1.bright": 0.85, "r1.hit": 0.95, "r1.left": 0.9, "r1.right": 0.7,
        "structure": "1 + 2", "balance": 0.0, "release": 2.5, "voice.count": 8,
    },
    "Gamelan": {
        # Two bronze keys tuned a few cents apart: the slow beating between them.
        "mallet.stiffness": 0.55, "mallet.noise": 0.1, "mallet.colour": 0.4, "r1.type": "Beam", "r1.decay": 3.5,
        "r1.material": 0.15, "r1.bright": 0.5, "r1.inharm": 0.15, "r1.hit": 0.1, "r2.type": "Beam", "r2.fine": 14,
        "r2.decay": 3.5, "r2.material": 0.15, "r2.bright": 0.5, "r2.inharm": 0.15, "r2.hit": 0.1,
        "structure": "1 + 2", "balance": 0.5, "release": 2.0,
    },
    "Wood Block": {
        # A hollow block of hard wood: a short knock, a little cavity behind it.
        "mallet.stiffness": 0.75, "mallet.noise": 0.2, "mallet.colour": 0.55, "r1.type": "Marimba", "r1.quality": 8,
        "r1.tune": 12, "r1.decay": 0.3, "r1.material": 0.9, "r1.bright": 0.35, "r1.hit": 0.5, "r2.type": "Tube",
        "r2.tune": 12, "r2.decay": 0.12, "r2.material": 0.5, "structure": "1 + 2", "balance": 0.35, "release": 0.2,
        "voice.count": 8,
    },
    # ---- Bells, plates, bowls -------------------------------------------------------------------
    "Plate Bell": {
        # A ringing plate and a high bar over it.
        "mallet.stiffness": 0.7, "mallet.noise": 0.05, "r1.type": "Plate", "r1.quality": 28, "r1.decay": 4.0,
        "r1.material": 0.1, "r1.bright": 0.7, "r1.hit": 0.3, "r2.type": "Beam", "r2.tune": 24, "r2.decay": 2.5,
        "r2.material": 0.2, "r2.bright": 0.6, "structure": "1 + 2", "balance": 0.2, "release": 2.0,
    },
    "Dull Bell": {
        # A thick plate damped by its own weight, a skin under it.
        "mallet.stiffness": 0.3, "mallet.noise": 0.05, "r1.type": "Plate", "r1.decay": 1.8, "r1.material": 0.75,
        "r1.bright": 0.3, "r1.hit": 0.5, "r2.type": "Membrane", "r2.tune": -12, "r2.decay": 1.2, "r2.material": 0.6,
        "r2.bright": 0.3, "structure": "1 + 2", "balance": 0.3, "release": 1.2,
    },
    "Bowl": {
        # A metal bowl and its twin a few cents apart, struck softly: a long beating ring.
        "mallet.stiffness": 0.45, "mallet.noise": 0.02, "r1.type": "Beam", "r1.quality": 12, "r1.decay": 9.0,
        "r1.material": -0.1, "r1.bright": 0.4, "r1.hit": 0.35, "r2.type": "Beam", "r2.quality": 12, "r2.fine": 4,
        "r2.decay": 9.0, "r2.material": -0.1, "r2.bright": 0.4, "r2.hit": 0.35, "structure": "1 + 2", "balance": 0.5,
        "release": 3.0, "voice.count": 6,
    },
    "Gong": {
        # A big plate struck with a soft beater, its highs ringing longer than its lows, a skin beneath.
        "mallet.stiffness": 0.2, "mallet.noise": 0.15, "mallet.colour": 0.3, "r1.type": "Plate", "r1.quality": 32,
        "r1.tune": -12, "r1.decay": 7.0, "r1.material": -0.3, "r1.bright": 0.85, "r1.inharm": 0.2, "r1.hit": 0.4,
        "r2.type": "Membrane", "r2.quality": 24, "r2.tune": -5, "r2.decay": 5.0, "r2.material": -0.2, "r2.bright": 0.7,
        "structure": "1 + 2", "balance": 0.4, "release": 2.5, "voice.count": 6,
    },
    "Bowed Glass": {
        # A glass rubbed by a narrow band of noise that follows the keys; the rubbing place drifts slowly.
        "mallet.volume": 0.0, "noise.volume": 0.6, "noise.filter": "Band-pass", "noise.cutoff": 2_000,
        "noise.resonance": 0.5, "noise.key": 1.0, "noise.attack": 0.4, "noise.decay": 0.5, "noise.sustain": 0.8,
        "noise.release": 0.6, "r1.type": "Beam", "r1.quality": 12, "r1.decay": 6.0, "r1.material": -0.2,
        "r1.bright": 0.5, "r1.hit": 0.7, "r2.type": "Beam", "r2.quality": 12, "r2.fine": 5, "r2.decay": 6.0,
        "r2.material": -0.2, "r2.bright": 0.5, "r2.hit": 0.7, "structure": "1 + 2", "balance": 0.5,
        "lfo1.shape": "Triangle", "lfo1.rate": 0.3, "lfo1.hit": 0.3, "release": 1.5,
    },
    # ---- Drums ----------------------------------------------------------------------------------
    "Membrane Kick": {
        # A skin struck in its middle (its round modes only), its pitch falling, a beater's click, a short shell.
        "mallet.stiffness": 0.55, "mallet.noise": 0.3, "mallet.colour": 0.75, "velocity.stiffness": 0.6,
        "r1.type": "Membrane", "r1.quality": 12, "r1.tune": -24, "r1.decay": 0.4, "r1.material": 0.75,
        "r1.bright": 0.3, "r1.hit": 0.0, "r1.left": 0.0, "r1.right": 0.1, "r2.type": "Tube", "r2.tune": -24,
        "r2.decay": 0.15, "r2.material": 0.6, "r2.bright": 0.3, "structure": "1 + 2", "balance": 0.2,
        "lfo1.shape": "Saw", "lfo1.rate": 3.0, "lfo1.tune": 4.0, "release": 0.3,
    },
    "Tom": {
        # A tom's skin, its pitch dropping a little as it rings, its shell behind.
        "mallet.stiffness": 0.45, "mallet.noise": 0.25, "mallet.colour": 0.35, "r1.type": "Membrane", "r1.quality": 20,
        "r1.tune": -12, "r1.decay": 0.6, "r1.material": 0.55, "r1.bright": 0.45, "r1.hit": 0.35, "r1.left": 0.3,
        "r1.right": 0.55, "r2.type": "Tube", "r2.tune": -12, "r2.decay": 0.3, "r2.material": 0.6, "r2.bright": 0.3,
        "structure": "1 + 2", "balance": 0.25, "lfo1.shape": "Saw", "lfo1.rate": 2.0, "lfo1.tune": 1.0, "release": 0.6,
    },
    "Timpani-like": {
        # A kettle drum's skin struck near its rim with a felt mallet, its modes drawn towards 1:1.5:2.
        "mallet.stiffness": 0.3, "mallet.noise": 0.05, "mallet.colour": 0.2, "velocity.stiffness": 0.7,
        "r1.type": "Membrane", "r1.quality": 24, "r1.tune": -12, "r1.decay": 3.0, "r1.material": 0.35,
        "r1.bright": 0.4, "r1.inharm": -0.4, "r1.hit": 0.75, "r2.type": "Tube", "r2.tune": -12, "r2.decay": 0.8,
        "r2.material": 0.5, "r2.bright": 0.3, "structure": "1 > 2", "balance": 0.25, "release": 1.5,
    },
    "Snare-like Drum": {
        # A tight skin, a second one under it, and a burst of high noise for the wires.
        "mallet.stiffness": 0.75, "mallet.noise": 0.35, "mallet.colour": 0.7, "noise.volume": 0.55,
        "noise.filter": "High-pass", "noise.cutoff": 2_500, "noise.resonance": 0.1, "noise.attack": 0.001,
        "noise.decay": 0.18, "noise.sustain": 0.0, "noise.release": 0.15, "r1.type": "Membrane", "r1.quality": 24,
        "r1.decay": 0.35, "r1.material": 0.5, "r1.bright": 0.6, "r1.hit": 0.4, "r2.type": "Membrane", "r2.tune": 7,
        "r2.decay": 0.25, "r2.material": 0.55, "r2.bright": 0.6, "r2.hit": 0.6, "structure": "1 + 2",
        "balance": 0.3, "release": 0.3,
    },
    "Water Drop": {
        # A small bar whose pitch leaps up as it dies away.
        "mallet.stiffness": 0.5, "mallet.noise": 0.0, "r1.type": "Beam", "r1.quality": 4, "r1.tune": 12,
        "r1.decay": 0.15, "r1.material": 0.6, "r1.bright": 0.2, "r1.hit": 0.0, "structure": "1 + 2", "balance": 0.0,
        "lfo1.shape": "Saw", "lfo1.rate": 7.0, "lfo1.tune": -7.0, "release": 0.15,
    },
    # ---- Strings ----------------------------------------------------------------------------------
    "Plucked String": {
        # A string plucked near its bridge, a light wooden body behind it.
        "mallet.stiffness": 0.8, "mallet.noise": 0.15, "mallet.colour": 0.7, "r1.type": "String", "r1.decay": 2.5,
        "r1.material": 0.45, "r1.bright": 0.7, "r1.inharm": 0.05, "r1.hit": 0.85, "r1.left": 0.6, "r1.right": 0.8,
        "r2.type": "Plate", "r2.decay": 0.25, "r2.material": 0.6, "r2.bright": 0.5, "structure": "1 > 2",
        "balance": 0.3, "release": 0.4,
    },
    "Struck Bass": {
        # A heavy string struck by a soft hammer, an octave down, a dark body.
        "mallet.stiffness": 0.5, "mallet.noise": 0.08, "r1.type": "String", "r1.tune": -12, "r1.decay": 2.5,
        "r1.material": 0.5, "r1.bright": 0.5, "r1.inharm": 0.1, "r1.hit": 0.7, "r2.type": "Plate", "r2.tune": -12,
        "r2.decay": 0.3, "r2.material": 0.6, "r2.bright": 0.4, "structure": "1 > 2", "balance": 0.25, "release": 0.25,
    },
    "Bowed-like String": {
        # A string rubbed by noise (a bow near the bridge), its body after it, a late vibrato.
        "mallet.volume": 0.0, "noise.volume": 0.8, "noise.filter": "Low-pass", "noise.cutoff": 1_800,
        "noise.resonance": 0.1, "noise.key": 0.5, "noise.attack": 0.12, "noise.decay": 0.4, "noise.sustain": 0.75,
        "noise.release": 0.35, "r1.type": "String", "r1.decay": 4.0, "r1.material": 0.35, "r1.bright": 0.6,
        "r1.hit": 0.85, "r1.left": 0.5, "r1.right": 0.7, "r2.type": "Plate", "r2.decay": 0.3, "r2.material": 0.5,
        "r2.bright": 0.45, "structure": "1 > 2", "balance": 0.35, "lfo1.shape": "Sine", "lfo1.rate": 5.2,
        "lfo1.tune": 0.12, "release": 0.4,
    },
    # ---- Pipes and breath -------------------------------------------------------------------------
    "Flute-like Pipe": {
        # A pipe closed at its bottom, blown by a band of breath that follows the keys; a light vibrato.
        "mallet.volume": 0.0, "noise.volume": 0.7, "noise.filter": "Band-pass", "noise.cutoff": 1_500,
        "noise.resonance": 0.25, "noise.key": 0.8, "noise.attack": 0.06, "noise.decay": 0.2, "noise.sustain": 0.85,
        "noise.release": 0.15, "r1.type": "Pipe", "r1.decay": 2.5, "r1.material": 0.6, "r1.bright": 0.4,
        "r1.hit": 0.95, "r1.left": 0.5, "r1.right": 0.3, "structure": "1 + 2", "balance": 0.0,
        "lfo1.shape": "Sine", "lfo1.rate": 5.0, "lfo1.tune": 0.08, "release": 0.2,
    },
    "Breath Tube": {
        # Breath through an open tube, a closed pipe an octave under it: more air than tone.
        "mallet.volume": 0.0, "noise.volume": 0.9, "noise.filter": "Low-pass", "noise.cutoff": 2_500,
        "noise.resonance": 0.0, "noise.key": 0.6, "noise.attack": 0.15, "noise.decay": 0.3, "noise.sustain": 0.9,
        "noise.release": 0.3, "r1.type": "Tube", "r1.decay": 0.6, "r1.material": 0.6, "r1.bright": 0.35,
        "r1.hit": 0.9, "r2.type": "Pipe", "r2.tune": -12, "r2.decay": 0.5, "r2.material": 0.6,
        "structure": "1 + 2", "balance": 0.35, "release": 0.3,
    },
}
