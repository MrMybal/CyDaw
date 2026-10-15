"""Jade's factory sounds: a modelled electric piano (see build_synth_presets.py).

A sound is a few settings over the defaults ("Init" first): keys as in Jade's table ("model", "hammer.hardness",
"pickup.proximity"); a choice by its English name ("Tines", "Reeds", "Struck strings", "Sine", "Square");
"tremolo.sync" as a note division's number (0 free, 6 = 1/8, 9 = 1/4). The defaults are a clean tine piano:
a medium hammer, the tone bar half-way, the pickup at a moderate distance, no tremolo, no chorus.
"""

PRESETS = {
    "Init": {},
    # ---- Tines ----------------------------------------------------------------------------------
    "Classic Tine Piano": {
        # The everyday tine piano: warm sustain, a little bell, a gentle stereo tremolo.
        "hammer.hardness": 0.45, "tine.bar": 0.6, "pickup.proximity": 0.38, "drive.amount": 0.06, "tone.treble": 1.0,
        "tremolo.depth": 0.25, "tremolo.stereo": 1.0, "tremolo.rate": 3.5,
    },
    "Bright Ballad": {
        # Harder hammers and the pickup at the tine's tip: a clear bell over a clean, long tone, a light chorus.
        "hammer.hardness": 0.6, "tine.harmonics": 0.65, "pickup.position": 0.7, "pickup.proximity": 0.25,
        "tine.decay": 8.0, "tine.damper": 0.15, "tine.bar": 0.6, "tone.treble": 4.0, "tone.body": -2.0,
        "chorus.mix": 0.35, "chorus.depth": 0.4, "chorus.rate": 0.5,
    },
    "Barking Rock": {
        # The pickup close to the tines and off their axis: played hard, the notes bark into a hot preamp.
        "pickup.proximity": 0.85, "pickup.symmetry": 0.3, "hammer.hardness": 0.6, "hammer.velocity": 0.8,
        "velocity.curve": 0.3, "tine.bar": 0.45, "drive.amount": 0.45, "drive.output": -3.0, "tone.body": 3.0,
        "tone.bass": 2.0, "tone.treble": 1.0,
    },
    "Soft Lo-Fi Keys": {
        # Soft hammers, a dull pickup, keys a little out of tune, a slow wide chorus and plenty of key noise.
        "hammer.hardness": 0.2, "tine.harmonics": 0.2, "pickup.tone": 3_000, "pickup.proximity": 0.25,
        "tone.treble": -6.0, "tone.bass": 2.0, "tune.detune": 6.0, "tune.stretch": 0.0, "tine.decay": 4.0,
        "drive.amount": 0.15, "noise.thump": 0.6, "noise.release": 0.5, "chorus.mix": 0.5, "chorus.depth": 0.7,
        "chorus.rate": 0.35,
    },
    "Suitcase Auto-Pan": {
        # The amplifier's tremolo panning the keys from side to side, a slightly pushed preamp.
        "pickup.proximity": 0.5, "tine.bar": 0.6, "drive.amount": 0.2, "tone.bass": 2.0, "tone.body": 1.0,
        "tremolo.shape": "Sine", "tremolo.depth": 0.7, "tremolo.stereo": 1.0, "tremolo.rate": 4.2, "stereo.spread": 0.1,
    },
    "Mellow Jazz Tines": {
        # Soft hammers, the pickup where the bell is silent, a round low end and a faint tremolo.
        "hammer.hardness": 0.3, "tine.harmonics": 0.3, "pickup.position": 0.27, "pickup.proximity": 0.3,
        "pickup.symmetry": 0.55, "tine.decay": 7.0, "tine.bar": 0.75, "tone.bass": 3.0, "tone.treble": -3.0,
        "drive.amount": 0.08, "tremolo.depth": 0.15, "tremolo.rate": 3.0,
    },
    "Dub Tremolo": {
        # A deep square tremolo on the quarter note, the lows up, the highs down, short notes.
        "tremolo.shape": "Square", "tremolo.sync": 9, "tremolo.depth": 0.9, "tremolo.stereo": 0.2,
        "drive.amount": 0.25, "tone.bass": 4.0, "tone.treble": -4.0, "tine.decay": 5.0, "tine.damper": 0.08,
        "pickup.proximity": 0.5,
    },
    "Overdriven Stage Keys": {
        # A close pickup into a preamp turned up: thick, compressed and growling.
        "pickup.proximity": 0.75, "pickup.symmetry": 0.4, "hammer.velocity": 0.7, "drive.amount": 0.7,
        "drive.output": -5.0, "tone.body": 2.0, "tone.treble": 2.0, "tine.bar": 0.5,
    },
    "Glass Bell Keys": {
        # All bell: hard hammers, the pickup at the tip, a shorter tone bar, a stretched tuning and a chorus.
        "hammer.hardness": 0.7, "tine.harmonics": 0.7, "pickup.position": 0.8, "pickup.proximity": 0.2,
        "pickup.symmetry": 0.4, "tine.bar": 0.35, "tine.decay": 9.0, "tune.stretch": 6.0, "tone.treble": 2.0,
        "chorus.mix": 0.4, "chorus.depth": 0.5,
    },
    "Hard Hammer Pop": {
        # Bright and punchy for pop chords: hard hammers that answer the lightest touch.
        "hammer.hardness": 0.65, "hammer.velocity": 0.4, "velocity.curve": 0.4, "tine.harmonics": 0.55,
        "pickup.position": 0.65, "pickup.proximity": 0.45, "tone.treble": 3.0, "tone.body": -2.0, "chorus.mix": 0.25,
    },
    "Neo Soul Keys": {
        # Warm and slightly barking, a lazy stereo tremolo and a touch of chorus.
        "pickup.proximity": 0.45, "pickup.symmetry": 0.45, "hammer.hardness": 0.4, "tine.bar": 0.65,
        "drive.amount": 0.12, "tone.bass": 2.0, "tone.treble": -1.0, "tremolo.depth": 0.35, "tremolo.stereo": 1.0,
        "tremolo.rate": 3.2, "chorus.mix": 0.25, "chorus.depth": 0.4, "chorus.rate": 0.4,
    },
    "Chorus Dream": {
        # A long, soft tine sound in a deep chorus, the tremolo slowly panning, the keys spread wide.
        "pickup.proximity": 0.3, "tine.harmonics": 0.55, "tine.decay": 9.0, "tine.damper": 0.3,
        "chorus.mix": 0.8, "chorus.depth": 0.8, "chorus.rate": 0.45, "tremolo.depth": 0.3, "tremolo.stereo": 1.0,
        "tremolo.rate": 2.0, "stereo.spread": 0.6,
    },
    "Vintage Worn Keys": {
        # An old instrument: keys out of tune, a dull pickup, noisy hammers and dampers, a warm preamp.
        "tune.detune": 8.0, "tune.stretch": 10.0, "pickup.tone": 4_500, "pickup.symmetry": 0.35, "drive.amount": 0.3,
        "hammer.hardness": 0.4, "noise.thump": 0.6, "noise.release": 0.6, "tine.decay": 5.0, "tone.treble": -2.0,
    },
    "Ambient Tines": {
        # A pad of tines: soft hammers, a long ring and a slow damper, chorus and a slow pan.
        "hammer.hardness": 0.25, "tine.harmonics": 0.35, "tine.decay": 25.0, "tine.damper": 1.5, "tine.bar": 0.8,
        "noise.thump": 0.1, "noise.release": 0.1, "chorus.mix": 0.6, "chorus.depth": 0.6, "chorus.rate": 0.3,
        "tremolo.depth": 0.35, "tremolo.rate": 1.2, "tremolo.stereo": 1.0, "stereo.spread": 0.8,
    },
    "Tine Bass": {
        # The bass end of a tine piano on its own: one voice, a long tone bar, a close pickup, the highs down.
        "voice.mode": "Mono", "tine.bar": 0.8, "tine.decay": 5.0, "tine.harmonics": 0.3, "pickup.proximity": 0.6,
        "pickup.symmetry": 0.4, "drive.amount": 0.2, "tone.bass": 4.0, "tone.treble": -4.0, "stereo.spread": 0.0,
    },
    # ---- Reeds ----------------------------------------------------------------------------------
    "Funky Reed": {
        # A steel reed piano with a bite: nasal, a little pushed, short notes.
        "model": "Reeds", "hammer.hardness": 0.55, "pickup.proximity": 0.6, "drive.amount": 0.25, "tone.body": 2.0,
        "tone.treble": 2.0, "tine.decay": 3.0, "tine.damper": 0.06,
    },
    "Growling Reed": {
        # The reeds close to the plates: struck hard, they growl.
        "model": "Reeds", "pickup.proximity": 0.9, "pickup.symmetry": 0.4, "hammer.velocity": 0.85, "drive.amount": 0.5,
        "drive.output": -3.0, "tone.body": 3.0, "tone.bass": 1.0,
    },
    "Reed Tremolo": {
        # The reed piano's own amplitude tremolo, quick and even.
        "model": "Reeds", "pickup.proximity": 0.5, "drive.amount": 0.15, "tremolo.shape": "Sine", "tremolo.depth": 0.55,
        "tremolo.stereo": 0.0, "tremolo.rate": 5.5,
    },
    "Soft Reed Ballad": {
        # Gentle hammers on the reeds, a rounder top and a light chorus.
        "model": "Reeds", "hammer.hardness": 0.3, "pickup.proximity": 0.3, "tine.harmonics": 0.35, "tine.decay": 4.0,
        "tine.bar": 0.7, "tone.treble": -2.0, "chorus.mix": 0.3,
    },
    "Percussive Reed Stabs": {
        # Short, hard and driven: chords that stop as soon as the keys come up.
        "model": "Reeds", "tine.decay": 1.2, "tine.damper": 0.03, "hammer.hardness": 0.7, "pickup.proximity": 0.7,
        "drive.amount": 0.3, "tone.body": 2.0, "noise.release": 0.5,
    },
    # ---- Struck strings -------------------------------------------------------------------------
    "Struck-String Funk": {
        # Short strings struck near their end and read near it: thin, bright, a snap as the pad lands.
        "model": "Struck strings", "hammer.hardness": 0.65, "hammer.position": 0.2, "pickup.proximity": 0.5,
        "pickup.position": 0.35, "tine.harmonics": 0.6, "tine.decay": 4.0, "tine.damper": 0.04, "noise.release": 0.6,
        "drive.amount": 0.15, "tone.body": 2.0, "tone.treble": 2.0, "amp.velocity": 0.8,
    },
    "Muted String Keys": {
        # The strings damped as they ring: short, plucky notes.
        "model": "Struck strings", "tine.decay": 0.8, "tine.bar": 0.2, "tine.damper": 0.03, "tine.harmonics": 0.4,
        "pickup.position": 0.6, "tone.treble": 1.0, "noise.release": 0.4,
    },
    "Bright String Comp": {
        # Read right at the end of the strings, hard hammers: cutting rhythm chords.
        "model": "Struck strings", "pickup.position": 0.1, "hammer.hardness": 0.75, "tine.harmonics": 0.7,
        "tone.treble": 4.0, "tone.bass": -3.0, "tine.damper": 0.05,
    },
    "Warm String Keys": {
        # Read near the middle of the strings: hollow and round, soft hammers and a chorus.
        "model": "Struck strings", "pickup.position": 0.9, "hammer.hardness": 0.35, "tine.harmonics": 0.3,
        "tine.bar": 0.7, "tone.bass": 3.0, "tone.treble": -3.0, "chorus.mix": 0.3,
    },
}
