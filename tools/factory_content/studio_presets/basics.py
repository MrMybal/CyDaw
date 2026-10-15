"""The basic devices: the factory modules made of graphs (Poly Keys, Stereo
Finisher, Warm Drive…) and the plain effect, filter, instrument and MIDI
cards (Chorus, Delay, Compressor…).

Values by binding suffix; what is not given keeps its default. A module's
settings live on its inner cards, whose ids are fixed by the factory module
("node.16002.eq.tone"); a plain card's are its own ("chorus.rate").
"""
from __future__ import annotations

# Waveforms of the polyphonic oscillator, shapes of the LFOs.
SINE, TRIANGLE, SAW, SQUARE = range(4)
# A voice allocator's playing.
POLY, MONO, LEGATO = range(3)
# Note values of the sync settings (0 is free).
FREE, SYNC_16, SYNC_8, SYNC_4, SYNC_2, SYNC_BAR = 0, 3, 6, 9, 11, 12
SYNC_16_TRIPLET, SYNC_8_TRIPLET, SYNC_8_DOTTED, SYNC_4_DOTTED = 2, 4, 8, 10
# The multimode filter's modes.
LOW_PASS, HIGH_PASS, BAND_PASS, NOTCH = range(4)


def card(node: int, **settings: float) -> dict[str, float]:
    """Settings of one inner card of a module: `a__b` stands for `a.b`."""
    return {f"node.{node}.{name.replace('__', '.')}": float(value) for name, value in settings.items()}


def plain(**settings: float) -> dict[str, float]:
    """Settings of a plain card: `a__b` stands for `a.b`."""
    return {name.replace("__", "."): float(value) for name, value in settings.items()}


def merge(*parts: dict[str, float]) -> dict[str, float]:
    preset: dict[str, float] = {}
    for part in parts:
        preset.update(part)
    return preset


# ---- Instrument modules ------------------------------------------------------------------
# The inner cards of each: voice allocator, oscillator, envelope, gain,
# voice filter, and what each adds after them.
INSTRUMENTS = {
    "poly-sine": {"voice": 10002, "osc": 10003, "env": 10004, "gain": 10007, "filter": 10005},
    "poly-pad": {"voice": 18002, "osc": 18003, "env": 18004, "gain": 18005, "filter": 18006, "width": 18007, "reverb": 18008},
    "poly-bass": {"voice": 19002, "osc": 19003, "env": 19004, "gain": 19005, "filter": 19006, "drive": 19007, "comp": 19008},
    "poly-pluck": {"voice": 35002, "osc": 35003, "env": 35004, "gain": 35005, "filter": 35006, "delay": 35007},
    "poly-lead": {"voice": 36002, "osc": 36003, "env": 36004, "gain": 36005, "filter": 36006, "drive": 36007, "delay": 36008},
    "poly-keys": {"voice": 37002, "osc": 37003, "env": 37004, "gain": 37005, "filter": 37006, "comp": 37007, "width": 37008},
    "ambient-glass": {"voice": 38002, "osc": 38003, "env": 38004, "gain": 38005, "filter": 38006, "delay": 38007, "reverb": 38008,
                      "width": 38009},
    "electric-organ": {"voice": 39002, "osc": 39003, "env": 39004, "gain": 39005, "filter": 39006, "drive": 39007, "comp": 39008,
                       "width": 39009},
    "percussive-bell": {"voice": 40002, "osc": 40003, "env": 40004, "gain": 40005, "filter": 40006, "delay": 40007, "reverb": 40008},
}


def voice(module: str, mode=None, glide=None, wave=None, level=None, attack=None, decay=None, sustain=None, release=None,
          gain=None, cutoff=None, q=None, width=None, room=None, reverb=None, time=None, feedback=None, echo=None, drive=None,
          threshold=None, ratio=None) -> dict[str, float]:
    """A setting of an instrument module, by what it does; those its module
    lacks (a reverb on Poly Bass) are left out."""
    ids = INSTRUMENTS[module]
    wanted = {
        ("voice", "voice.mode"): mode, ("voice", "voice.glide"): glide,
        ("osc", "polyOscillator.waveform"): wave, ("osc", "polyOscillator.level"): level,
        ("env", "envelope.attack"): attack, ("env", "envelope.decay"): decay,
        ("env", "envelope.sustain"): sustain, ("env", "envelope.release"): release,
        ("gain", "gain.db"): gain,
        ("filter", "voiceFilter.cutoff"): cutoff, ("filter", "voiceFilter.resonance"): q,
        ("width", "stereo.width"): width,
        ("reverb", "reverb.roomSize"): room, ("reverb", "reverb.mix"): reverb,
        ("delay", "delay.time"): time, ("delay", "delay.feedback"): feedback, ("delay", "delay.mix"): echo,
        ("drive", "drive.db"): drive,
        ("comp", "compressor.threshold"): threshold, ("comp", "compressor.ratio"): ratio,
    }
    preset: dict[str, float] = {}
    for (part, name), value in wanted.items():
        if value is not None and part in ids:
            preset[f"node.{ids[part]}.{name}"] = float(value)
    return preset


def sounds(module: str, table: dict[str, dict]) -> dict[str, dict[str, float]]:
    return {name: voice(module, **settings) for name, settings in table.items()}


PRESETS: dict[str, dict[str, dict[str, float]]] = {}

PRESETS["poly-sine"] = sounds("poly-sine", {
    "Pure Sine Keys": dict(wave=SINE, level=0.16, attack=4, decay=300, sustain=0.6, release=260, cutoff=12000, q=0.7),
    "Soft Flute Tone": dict(wave=SINE, level=0.15, attack=60, decay=400, sustain=0.85, release=320, cutoff=6500, q=0.8),
    "Sub Bass": dict(mode=MONO, glide=40, wave=SINE, level=0.22, attack=3, decay=200, sustain=0.9, release=120, cutoff=1200, q=0.7),
    "Glass Bell": dict(wave=TRIANGLE, level=0.14, attack=1, decay=900, sustain=0.0, release=1200, cutoff=9000, q=2.5),
    "Warm Triangle Pad": dict(wave=TRIANGLE, level=0.13, attack=700, decay=1500, sustain=0.7, release=2600, cutoff=4200, q=0.9),
    "Square Chip Lead": dict(mode=MONO, glide=25, wave=SQUARE, level=0.09, attack=2, decay=120, sustain=0.75, release=90,
                             cutoff=7000, q=0.8),
    "Saw Strings": dict(wave=SAW, level=0.1, attack=380, decay=900, sustain=0.8, release=900, cutoff=3800, q=0.75),
    "Short Blip": dict(wave=SINE, level=0.18, attack=1, decay=90, sustain=0.0, release=60, cutoff=10000, q=1.2),
})

PRESETS["poly-pad"] = sounds("poly-pad", {
    "Warm Saw Pad": dict(wave=SAW, level=0.12, attack=650, decay=1800, sustain=0.7, release=4000, cutoff=3200, q=0.85,
                         width=1.35, room=0.82, reverb=0.32),
    "Dark Evolving Pad": dict(wave=SAW, level=0.12, attack=1500, decay=3000, sustain=0.65, release=6000, cutoff=1400, q=2.2,
                              width=1.5, room=0.9, reverb=0.4),
    "Airy Choir": dict(wave=TRIANGLE, level=0.14, attack=900, decay=2000, sustain=0.8, release=4500, cutoff=5200, q=1.1,
                       width=1.6, room=0.88, reverb=0.45),
    "Square Organ Pad": dict(wave=SQUARE, level=0.08, attack=300, decay=1200, sustain=0.85, release=2200, cutoff=2600, q=0.8,
                             width=1.2, room=0.7, reverb=0.25),
    "Sine Bed": dict(wave=SINE, level=0.16, attack=1200, decay=2500, sustain=0.9, release=5000, cutoff=8000, q=0.7,
                     width=1.4, room=0.85, reverb=0.35),
    "Wide String Section": dict(wave=SAW, level=0.11, attack=450, decay=1000, sustain=0.85, release=1800, cutoff=4800, q=0.8,
                                width=1.8, room=0.78, reverb=0.3),
    "Lo-Fi Haze": dict(wave=TRIANGLE, level=0.13, attack=1100, decay=2400, sustain=0.6, release=3800, cutoff=1800, q=0.9,
                       width=0.9, room=0.92, reverb=0.55),
    "Bright Sweep Pad": dict(wave=SAW, level=0.11, attack=2000, decay=3000, sustain=0.75, release=4000, cutoff=7600, q=3.0,
                             width=1.45, room=0.8, reverb=0.3),
})

PRESETS["poly-bass"] = sounds("poly-bass", {
    "Round Analog Bass": dict(mode=MONO, glide=0, wave=TRIANGLE, level=0.2, attack=3, decay=250, sustain=0.6, release=110,
                              cutoff=900, q=1.0, drive=3, threshold=-14, ratio=3),
    "Acid Squelch": dict(mode=MONO, glide=60, wave=SAW, level=0.16, attack=2, decay=180, sustain=0.2, release=90, cutoff=520,
                         q=9.0, drive=10, threshold=-16, ratio=4),
    "Deep Sub": dict(mode=MONO, glide=30, wave=SINE, level=0.25, attack=4, decay=300, sustain=0.95, release=140, cutoff=400,
                     q=0.7, drive=1.5, threshold=-12, ratio=2.5),
    "Pluck Bass": dict(mode=MONO, wave=SAW, level=0.17, attack=1, decay=160, sustain=0.1, release=80, cutoff=1300, q=2.0,
                       drive=4, threshold=-18, ratio=4),
    "Growl Bass": dict(mode=MONO, glide=20, wave=SAW, level=0.15, attack=4, decay=300, sustain=0.7, release=120, cutoff=1800,
                       q=4.5, drive=18, threshold=-20, ratio=6),
    "Square Funk Bass": dict(mode=MONO, wave=SQUARE, level=0.12, attack=2, decay=140, sustain=0.35, release=70, cutoff=1500,
                             q=1.6, drive=5, threshold=-16, ratio=4),
    "Legato Slide Bass": dict(mode=LEGATO, glide=140, wave=SAW, level=0.15, attack=5, decay=400, sustain=0.75, release=150,
                              cutoff=1100, q=2.4, drive=6, threshold=-15, ratio=3.5),
    "Soft Upright": dict(mode=MONO, wave=TRIANGLE, level=0.2, attack=6, decay=600, sustain=0.0, release=220, cutoff=700, q=0.8,
                         drive=0, threshold=-12, ratio=2),
})

PRESETS["poly-pluck"] = sounds("poly-pluck", {
    "Bright Pluck": dict(wave=SAW, level=0.15, attack=1, decay=240, sustain=0.05, release=170, cutoff=5200, q=1.4,
                         time=285, feedback=0.28, echo=0.2),
    "Muted Pluck": dict(wave=SAW, level=0.16, attack=1, decay=150, sustain=0.0, release=110, cutoff=1600, q=1.0,
                        time=250, feedback=0.2, echo=0.12),
    "Kalimba": dict(wave=SINE, level=0.2, attack=1, decay=520, sustain=0.0, release=480, cutoff=7000, q=2.2,
                    time=330, feedback=0.22, echo=0.15),
    "Harp Pluck": dict(wave=TRIANGLE, level=0.17, attack=2, decay=900, sustain=0.0, release=900, cutoff=6500, q=1.2,
                       time=400, feedback=0.25, echo=0.18),
    "Dotted Echo Pluck": dict(wave=SAW, level=0.14, attack=1, decay=200, sustain=0.05, release=150, cutoff=4200, q=1.8,
                              time=375, feedback=0.48, echo=0.35),
    "Square Marimba": dict(wave=SQUARE, level=0.1, attack=1, decay=300, sustain=0.0, release=200, cutoff=3000, q=1.5,
                           time=240, feedback=0.15, echo=0.1),
    "Resonant Zap": dict(wave=SAW, level=0.13, attack=1, decay=120, sustain=0.0, release=90, cutoff=2400, q=12.0,
                         time=180, feedback=0.3, echo=0.2),
    "Dry Staccato": dict(wave=TRIANGLE, level=0.17, attack=1, decay=110, sustain=0.0, release=60, cutoff=5000, q=1.0,
                         time=200, feedback=0.0, echo=0.0),
})

PRESETS["poly-lead"] = sounds("poly-lead", {
    "Classic Saw Lead": dict(mode=MONO, glide=40, wave=SAW, level=0.13, attack=6, decay=320, sustain=0.7, release=380,
                             cutoff=3400, q=1.6, drive=5, time=340, feedback=0.26, echo=0.18),
    "Square Hero Lead": dict(mode=MONO, glide=60, wave=SQUARE, level=0.1, attack=4, decay=280, sustain=0.75, release=300,
                             cutoff=4200, q=1.2, drive=7, time=375, feedback=0.3, echo=0.2),
    "Screaming Lead": dict(mode=LEGATO, glide=90, wave=SAW, level=0.12, attack=3, decay=500, sustain=0.85, release=400,
                           cutoff=5600, q=4.0, drive=16, time=300, feedback=0.32, echo=0.22),
    "Soft Sine Lead": dict(mode=MONO, glide=80, wave=SINE, level=0.18, attack=30, decay=400, sustain=0.8, release=500,
                           cutoff=6000, q=0.8, drive=2, time=420, feedback=0.3, echo=0.25),
    "Portamento Synth Lead": dict(mode=LEGATO, glide=220, wave=SAW, level=0.12, attack=10, decay=600, sustain=0.75,
                                  release=450, cutoff=2600, q=2.5, drive=8, time=460, feedback=0.35, echo=0.24),
    "Chiptune Lead": dict(mode=MONO, glide=0, wave=SQUARE, level=0.08, attack=1, decay=150, sustain=0.85, release=80,
                          cutoff=9000, q=0.7, drive=0, time=150, feedback=0.2, echo=0.12),
    "Dark Filtered Lead": dict(mode=MONO, glide=50, wave=SAW, level=0.14, attack=12, decay=380, sustain=0.65, release=420,
                               cutoff=1300, q=3.2, drive=9, time=500, feedback=0.4, echo=0.2),
    "Poly Brass Stab": dict(mode=POLY, glide=0, wave=SAW, level=0.11, attack=25, decay=260, sustain=0.55, release=200,
                            cutoff=3000, q=1.4, drive=4, time=300, feedback=0.1, echo=0.08),
})

PRESETS["poly-keys"] = sounds("poly-keys", {
    "Electric Piano": dict(wave=TRIANGLE, level=0.13, attack=4, decay=900, sustain=0.35, release=500, cutoff=4800, q=1.0,
                           threshold=-16, ratio=2.5, width=1.25),
    "Soft Keys": dict(wave=SINE, level=0.16, attack=10, decay=1200, sustain=0.45, release=700, cutoff=3800, q=0.8,
                      threshold=-14, ratio=2.0, width=1.15),
    "Bright Clav": dict(wave=SQUARE, level=0.09, attack=1, decay=380, sustain=0.2, release=120, cutoff=6800, q=2.0,
                        threshold=-18, ratio=3.5, width=1.05),
    "Synth Piano": dict(wave=SAW, level=0.11, attack=2, decay=700, sustain=0.25, release=420, cutoff=3600, q=1.2,
                        threshold=-16, ratio=3.0, width=1.3),
    "Mellow Stage Keys": dict(wave=TRIANGLE, level=0.14, attack=8, decay=1500, sustain=0.5, release=900, cutoff=2600, q=0.9,
                              threshold=-12, ratio=2.0, width=1.35),
    "Toy Piano": dict(wave=SQUARE, level=0.08, attack=1, decay=260, sustain=0.0, release=180, cutoff=9500, q=2.8,
                      threshold=-18, ratio=3.0, width=1.0),
    "Wide Chord Keys": dict(wave=SAW, level=0.1, attack=12, decay=1100, sustain=0.55, release=800, cutoff=4200, q=1.0,
                            threshold=-15, ratio=2.4, width=1.7),
    "Dull Felt Keys": dict(wave=SINE, level=0.17, attack=6, decay=800, sustain=0.3, release=450, cutoff=1600, q=0.7,
                           threshold=-12, ratio=1.8, width=1.1),
})

PRESETS["ambient-glass"] = sounds("ambient-glass", {
    "Glass Cathedral": dict(wave=SINE, level=0.1, attack=360, decay=1200, sustain=0.55, release=2800, cutoff=7200, q=1.65,
                            time=460, feedback=0.48, echo=0.31, room=0.9, reverb=0.45, width=1.6),
    "Frozen Shimmer": dict(wave=TRIANGLE, level=0.1, attack=800, decay=2500, sustain=0.7, release=6000, cutoff=9500, q=2.4,
                           time=620, feedback=0.6, echo=0.35, room=0.95, reverb=0.55, width=1.8),
    "Distant Bells": dict(wave=SINE, level=0.12, attack=2, decay=1600, sustain=0.0, release=3000, cutoff=8000, q=2.0,
                          time=500, feedback=0.55, echo=0.38, room=0.92, reverb=0.5, width=1.5),
    "Underwater Glass": dict(wave=TRIANGLE, level=0.11, attack=500, decay=1800, sustain=0.6, release=3500, cutoff=1600, q=3.5,
                             time=700, feedback=0.5, echo=0.3, room=0.88, reverb=0.48, width=1.4),
    "Crystal Rain": dict(wave=SAW, level=0.08, attack=1, decay=400, sustain=0.0, release=1500, cutoff=6400, q=4.0,
                         time=230, feedback=0.62, echo=0.4, room=0.85, reverb=0.4, width=1.9),
    "Slow Dawn": dict(wave=SINE, level=0.12, attack=2500, decay=3000, sustain=0.8, release=7000, cutoff=5000, q=1.2,
                      time=900, feedback=0.45, echo=0.28, room=0.94, reverb=0.52, width=1.65),
})

PRESETS["electric-organ"] = sounds("electric-organ", {
    "Drawbar Organ": dict(wave=SQUARE, level=0.1, attack=10, decay=80, sustain=0.9, release=200, cutoff=6400, q=0.8,
                          drive=6, threshold=-18, ratio=3, width=1.2),
    "Gospel Organ": dict(wave=SQUARE, level=0.1, attack=8, decay=80, sustain=0.95, release=260, cutoff=7600, q=0.9,
                         drive=10, threshold=-20, ratio=3.5, width=1.3),
    "Soft Church Organ": dict(wave=SINE, level=0.14, attack=60, decay=200, sustain=0.95, release=600, cutoff=5200, q=0.7,
                              drive=2, threshold=-16, ratio=2, width=1.4),
    "Rock Organ": dict(wave=SAW, level=0.09, attack=6, decay=100, sustain=0.9, release=180, cutoff=5600, q=1.1,
                       drive=18, threshold=-22, ratio=4, width=1.15),
    "Combo Organ": dict(wave=TRIANGLE, level=0.12, attack=4, decay=60, sustain=0.9, release=140, cutoff=8200, q=1.4,
                        drive=8, threshold=-18, ratio=3, width=1.0),
    "Muffled Lounge Organ": dict(wave=SQUARE, level=0.1, attack=20, decay=120, sustain=0.85, release=300, cutoff=2400, q=0.8,
                                 drive=4, threshold=-14, ratio=2.5, width=1.25),
})

PRESETS["percussive-bell"] = sounds("percussive-bell", {
    "Tubular Bell": dict(wave=TRIANGLE, level=0.13, attack=1, decay=1400, sustain=0.0, release=2200, cutoff=8000, q=2.4,
                         time=300, feedback=0.25, echo=0.15, room=0.8, reverb=0.32),
    "Music Box": dict(wave=SINE, level=0.16, attack=1, decay=700, sustain=0.0, release=900, cutoff=10000, q=1.6,
                      time=250, feedback=0.3, echo=0.18, room=0.7, reverb=0.25),
    "Vibraphone": dict(wave=SINE, level=0.15, attack=2, decay=1800, sustain=0.1, release=1600, cutoff=6000, q=1.2,
                       time=400, feedback=0.2, echo=0.12, room=0.75, reverb=0.28),
    "Bright Glock": dict(wave=SAW, level=0.09, attack=1, decay=500, sustain=0.0, release=800, cutoff=12000, q=3.0,
                         time=225, feedback=0.34, echo=0.2, room=0.76, reverb=0.29),
    "Gamelan Metal": dict(wave=SQUARE, level=0.08, attack=1, decay=900, sustain=0.0, release=1500, cutoff=5400, q=6.0,
                          time=330, feedback=0.4, echo=0.22, room=0.82, reverb=0.35),
    "Short Cowbell": dict(wave=SQUARE, level=0.09, attack=1, decay=160, sustain=0.0, release=120, cutoff=4200, q=4.5,
                          time=200, feedback=0.1, echo=0.05, room=0.5, reverb=0.12),
    "Ice Chime": dict(wave=TRIANGLE, level=0.12, attack=1, decay=600, sustain=0.0, release=2500, cutoff=14000, q=5.0,
                      time=450, feedback=0.55, echo=0.32, room=0.9, reverb=0.45),
})

# ---- Effect modules -------------------------------------------------------------------

PRESETS["reverb-rack"] = {
    "Small Room": card(11002, reverb__roomSize=0.35, reverb__mix=0.2),
    "Vocal Plate": card(11002, reverb__roomSize=0.6, reverb__mix=0.25),
    "Medium Hall": card(11002, reverb__roomSize=0.78, reverb__mix=0.32),
    "Large Hall": card(11002, reverb__roomSize=0.88, reverb__mix=0.38),
    "Huge Cathedral": card(11002, reverb__roomSize=0.95, reverb__mix=0.5),
    "Ambient Wash": card(11002, reverb__roomSize=0.93, reverb__mix=0.7),
    "Subtle Glue": card(11002, reverb__roomSize=0.5, reverb__mix=0.1),
}

PRESETS["vocal-strip"] = {
    "Gentle Vocal": card(12002, compressor__threshold=-16, compressor__ratio=2.5) | card(12003, eq__tone=0.1),
    "Upfront Vocal": card(12002, compressor__threshold=-22, compressor__ratio=4) | card(12003, eq__tone=0.25),
    "Rap Vocal": card(12002, compressor__threshold=-26, compressor__ratio=6) | card(12003, eq__tone=0.3),
    "Warm Ballad": card(12002, compressor__threshold=-18, compressor__ratio=3) | card(12003, eq__tone=-0.12),
    "Spoken Word": card(12002, compressor__threshold=-20, compressor__ratio=3.5) | card(12003, eq__tone=0.05),
    "Backing Vocals": card(12002, compressor__threshold=-24, compressor__ratio=5) | card(12003, eq__tone=-0.05),
    "Airy Pop Vocal": card(12002, compressor__threshold=-20, compressor__ratio=3) | card(12003, eq__tone=0.4),
}

PRESETS["warm-drive"] = {
    "Tape Warmth": card(14002, drive__db=4) | card(14003, lowpass__cutoff=12000, lowpass__resonance=0.6)
    | card(14004, clip__threshold=0.95) | card(14005, gain__db=-1),
    "Crunchy Edge": card(14002, drive__db=12) | card(14003, lowpass__cutoff=9000, lowpass__resonance=0.7)
    | card(14004, clip__threshold=0.85) | card(14005, gain__db=-4),
    "Fuzz": card(14002, drive__db=28) | card(14003, lowpass__cutoff=6000, lowpass__resonance=0.9)
    | card(14004, clip__threshold=0.6) | card(14005, gain__db=-9),
    "Dark Saturation": card(14002, drive__db=10) | card(14003, lowpass__cutoff=3500, lowpass__resonance=0.8)
    | card(14004, clip__threshold=0.9) | card(14005, gain__db=-3),
    "Bass Grit": card(14002, drive__db=14) | card(14003, lowpass__cutoff=5000, lowpass__resonance=1.1)
    | card(14004, clip__threshold=0.8) | card(14005, gain__db=-5),
    "Radio Speaker": card(14002, drive__db=18) | card(14003, lowpass__cutoff=2600, lowpass__resonance=2.5)
    | card(14004, clip__threshold=0.7) | card(14005, gain__db=-6),
    "Subtle Glow": card(14002, drive__db=2.5) | card(14003, lowpass__cutoff=16000, lowpass__resonance=0.6)
    | card(14004, clip__threshold=0.98) | card(14005, gain__db=-0.5),
}

PRESETS["echo-space"] = {
    "Slapback": card(15002, delay__time=110, delay__feedback=0.12, delay__mix=0.25) | card(15003, reverb__roomSize=0.4, reverb__mix=0.1),
    "Quarter Echo": card(15002, delay__time=500, delay__feedback=0.35, delay__mix=0.28) | card(15003, reverb__roomSize=0.7, reverb__mix=0.2),
    "Dotted Eighth": card(15002, delay__time=375, delay__feedback=0.42, delay__mix=0.3) | card(15003, reverb__roomSize=0.72, reverb__mix=0.22),
    "Dub Space": card(15002, delay__time=750, delay__feedback=0.7, delay__mix=0.38) | card(15003, reverb__roomSize=0.85, reverb__mix=0.3),
    "Ambient Trails": card(15002, delay__time=900, delay__feedback=0.6, delay__mix=0.35) | card(15003, reverb__roomSize=0.93, reverb__mix=0.45),
    "Tight Doubler": card(15002, delay__time=28, delay__feedback=0.0, delay__mix=0.35) | card(15003, reverb__roomSize=0.3, reverb__mix=0.08),
    "Vocal Throw": card(15002, delay__time=250, delay__feedback=0.5, delay__mix=0.32) | card(15003, reverb__roomSize=0.8, reverb__mix=0.28),
}

PRESETS["stereo-finisher"] = {
    "Gentle Master": card(16002, eq__tone=0.04) | card(16003, stereo__width=1.1) | card(16004, compressor__threshold=-8, compressor__ratio=1.6)
    | card(16005, clip__threshold=0.98),
    "Loud and Wide": card(16002, eq__tone=0.12) | card(16003, stereo__width=1.35) | card(16004, compressor__threshold=-16, compressor__ratio=3)
    | card(16005, clip__threshold=0.9),
    "Warm Glue": card(16002, eq__tone=-0.08) | card(16003, stereo__width=1.05) | card(16004, compressor__threshold=-12, compressor__ratio=2)
    | card(16005, clip__threshold=0.96),
    "Bright Pop": card(16002, eq__tone=0.25) | card(16003, stereo__width=1.25) | card(16004, compressor__threshold=-14, compressor__ratio=2.5)
    | card(16005, clip__threshold=0.93),
    "Club Punch": card(16002, eq__tone=0.08) | card(16003, stereo__width=1.15) | card(16004, compressor__threshold=-18, compressor__ratio=4)
    | card(16005, clip__threshold=0.88),
    "Narrow and Focused": card(16002, eq__tone=0.0) | card(16003, stereo__width=0.8) | card(16004, compressor__threshold=-10, compressor__ratio=2)
    | card(16005, clip__threshold=0.97),
    "Lo-Fi Master": card(16002, eq__tone=-0.3) | card(16003, stereo__width=0.9) | card(16004, compressor__threshold=-20, compressor__ratio=5)
    | card(16005, clip__threshold=0.85),
    "Transparent": card(16002, eq__tone=0.0) | card(16003, stereo__width=1.0) | card(16004, compressor__threshold=-4, compressor__ratio=1.3)
    | card(16005, clip__threshold=1.0),
}

PRESETS["parallel-drive"] = {
    "Subtle Parallel": card(17002, drive__db=8) | card(17003, gain__db=-14),
    "Drum Smash": card(17002, drive__db=24) | card(17003, gain__db=-10),
    "Vocal Grit": card(17002, drive__db=14) | card(17003, gain__db=-12),
    "Bass Thickener": card(17002, drive__db=18) | card(17003, gain__db=-8),
    "Heavy Blend": card(17002, drive__db=32) | card(17003, gain__db=-6),
    "Gentle Harmonics": card(17002, drive__db=6) | card(17003, gain__db=-18),
}

PRESETS["drum-punch"] = {
    "Kick Punch": card(25002, highpass__cutoff=30) | card(25003, compressor__threshold=-18, compressor__ratio=4)
    | card(25004, drive__db=3) | card(25005, clip__threshold=0.95) | card(25006, gain__db=-1),
    "Snare Crack": card(25002, highpass__cutoff=90) | card(25003, compressor__threshold=-22, compressor__ratio=6)
    | card(25004, drive__db=5) | card(25005, clip__threshold=0.9) | card(25006, gain__db=-2),
    "Drum Bus Glue": card(25002, highpass__cutoff=35) | card(25003, compressor__threshold=-14, compressor__ratio=2.5)
    | card(25004, drive__db=2) | card(25005, clip__threshold=0.97) | card(25006, gain__db=0),
    "Crushed Room": card(25002, highpass__cutoff=60) | card(25003, compressor__threshold=-34, compressor__ratio=12)
    | card(25004, drive__db=9) | card(25005, clip__threshold=0.8) | card(25006, gain__db=-6),
    "Tight Electronic": card(25002, highpass__cutoff=45) | card(25003, compressor__threshold=-20, compressor__ratio=5)
    | card(25004, drive__db=4) | card(25005, clip__threshold=0.88) | card(25006, gain__db=-2),
    "Loud Breakbeat": card(25002, highpass__cutoff=50) | card(25003, compressor__threshold=-26, compressor__ratio=8)
    | card(25004, drive__db=7) | card(25005, clip__threshold=0.85) | card(25006, gain__db=-4),
}

PRESETS["wide-echo"] = {
    "Wide Quarter": card(26002, delay__time=500, delay__feedback=0.4, delay__mix=0.3) | card(26003, stereo__width=1.5)
    | card(26004, lowpass__cutoff=8000, lowpass__resonance=0.7),
    "Dark Wide Dub": card(26002, delay__time=750, delay__feedback=0.68, delay__mix=0.35) | card(26003, stereo__width=1.7)
    | card(26004, lowpass__cutoff=2500, lowpass__resonance=1.0),
    "Bright Ping": card(26002, delay__time=250, delay__feedback=0.35, delay__mix=0.28) | card(26003, stereo__width=1.6)
    | card(26004, lowpass__cutoff=14000, lowpass__resonance=0.7),
    "Telephone Echo": card(26002, delay__time=330, delay__feedback=0.5, delay__mix=0.3) | card(26003, stereo__width=1.2)
    | card(26004, lowpass__cutoff=1800, lowpass__resonance=3.0),
    "Huge Stereo Space": card(26002, delay__time=900, delay__feedback=0.6, delay__mix=0.38) | card(26003, stereo__width=2.0)
    | card(26004, lowpass__cutoff=6000, lowpass__resonance=0.8),
    "Short Widener": card(26002, delay__time=18, delay__feedback=0.0, delay__mix=0.25) | card(26003, stereo__width=1.8)
    | card(26004, lowpass__cutoff=12000, lowpass__resonance=0.7),
}

PRESETS["dark-room"] = {
    "Dark Chamber": card(27002, lowpass__cutoff=3200, lowpass__resonance=0.9) | card(27003, reverb__roomSize=0.88, reverb__mix=0.45)
    | card(27004, eq__tone=-0.16),
    "Muffled Club": card(27002, lowpass__cutoff=900, lowpass__resonance=1.4) | card(27003, reverb__roomSize=0.6, reverb__mix=0.3)
    | card(27004, eq__tone=-0.3),
    "Cave": card(27002, lowpass__cutoff=1800, lowpass__resonance=1.0) | card(27003, reverb__roomSize=0.95, reverb__mix=0.6)
    | card(27004, eq__tone=-0.25),
    "Next Room": card(27002, lowpass__cutoff=1200, lowpass__resonance=0.8) | card(27003, reverb__roomSize=0.5, reverb__mix=0.35)
    | card(27004, eq__tone=-0.4),
    "Warm Hall": card(27002, lowpass__cutoff=5500, lowpass__resonance=0.7) | card(27003, reverb__roomSize=0.85, reverb__mix=0.35)
    | card(27004, eq__tone=-0.05),
    "Underwater": card(27002, lowpass__cutoff=500, lowpass__resonance=3.0) | card(27003, reverb__roomSize=0.9, reverb__mix=0.5)
    | card(27004, eq__tone=-0.5),
}

PRESETS["mono-focus"] = {
    "Full Mono": card(28002, stereo__width=0.0),
    "Narrow": card(28002, stereo__width=0.4),
    "Half Width": card(28002, stereo__width=0.6),
    "Gentle Focus": card(28002, stereo__width=0.8),
}

PRESETS["master-guard"] = {
    "Safe Ceiling": card(29003, highpass__cutoff=25) | card(29004, compressor__threshold=-6, compressor__ratio=2) | card(29005, clip__threshold=0.98),
    "Rumble Guard": card(29003, highpass__cutoff=40) | card(29004, compressor__threshold=-4, compressor__ratio=1.5) | card(29005, clip__threshold=0.99),
    "Loud Guard": card(29003, highpass__cutoff=30) | card(29004, compressor__threshold=-12, compressor__ratio=3) | card(29005, clip__threshold=0.92),
    "Live Stream Safety": card(29003, highpass__cutoff=60) | card(29004, compressor__threshold=-16, compressor__ratio=4) | card(29005, clip__threshold=0.9),
    "Transparent Guard": card(29003, highpass__cutoff=18) | card(29004, compressor__threshold=-3, compressor__ratio=1.3) | card(29005, clip__threshold=1.0),
}

PRESETS["tremolo"] = {
    "Classic Tremolo": card(30002, lfo__rate=4, lfo__depth=1, lfo__shape=SINE) | card(30003, map__outputMinimum=-12, map__outputMaximum=0),
    "Fast Shimmer": card(30002, lfo__rate=9, lfo__depth=1, lfo__shape=SINE) | card(30003, map__outputMinimum=-9, map__outputMaximum=0),
    "Slow Pulse": card(30002, lfo__rate=1.2, lfo__depth=1, lfo__shape=TRIANGLE) | card(30003, map__outputMinimum=-18, map__outputMaximum=0),
    "Hard Chop": card(30002, lfo__rate=6, lfo__depth=1, lfo__shape=SQUARE) | card(30003, map__outputMinimum=-40, map__outputMaximum=0),
    "Helicopter": card(30002, lfo__rate=14, lfo__depth=1, lfo__shape=SQUARE) | card(30003, map__outputMinimum=-30, map__outputMaximum=0),
    "Gentle Wobble": card(30002, lfo__rate=3, lfo__depth=0.6, lfo__shape=SINE) | card(30003, map__outputMinimum=-6, map__outputMaximum=0),
    "Saw Ramp Gate": card(30002, lfo__rate=2, lfo__depth=1, lfo__shape=SAW) | card(30003, map__outputMinimum=-30, map__outputMaximum=0),
}

PRESETS["auto-pan"] = {
    "Slow Drift": card(31002, lfo__rate=0.15, lfo__depth=0.7, lfo__shape=SINE),
    "Classic Auto-Pan": card(31002, lfo__rate=0.5, lfo__depth=1, lfo__shape=SINE),
    "Fast Swirl": card(31002, lfo__rate=4, lfo__depth=0.8, lfo__shape=SINE),
    "Hard Left-Right": card(31002, lfo__rate=2, lfo__depth=1, lfo__shape=SQUARE),
    "Triangle Sweep": card(31002, lfo__rate=1, lfo__depth=0.9, lfo__shape=TRIANGLE),
    "Subtle Movement": card(31002, lfo__rate=0.3, lfo__depth=0.3, lfo__shape=SINE),
}

PRESETS["dynamic-filter"] = {
    "Auto-Wah": card(32002, follower__attack=0.004, follower__release=0.18) | card(32003, map__outputMinimum=250, map__outputMaximum=4000)
    | card(32004, lowpass__resonance=4.0),
    "Funky Envelope": card(32002, follower__attack=0.002, follower__release=0.12) | card(32003, map__outputMinimum=300, map__outputMaximum=6000)
    | card(32004, lowpass__resonance=6.0),
    "Slow Bloom": card(32002, follower__attack=0.08, follower__release=0.8) | card(32003, map__outputMinimum=200, map__outputMaximum=9000)
    | card(32004, lowpass__resonance=1.5),
    "Bass Quack": card(32002, follower__attack=0.003, follower__release=0.1) | card(32003, map__outputMinimum=120, map__outputMaximum=2200)
    | card(32004, lowpass__resonance=8.0),
    "Soft Touch": card(32002, follower__attack=0.01, follower__release=0.3) | card(32003, map__outputMinimum=800, map__outputMaximum=14000)
    | card(32004, lowpass__resonance=0.9),
    "Drum Brightener": card(32002, follower__attack=0.001, follower__release=0.06) | card(32003, map__outputMinimum=2000, map__outputMaximum=16000)
    | card(32004, lowpass__resonance=1.2),
}

# ---- MIDI modules -------------------------------------------------------------------------

PRESETS["arp-motion"] = {
    "Octave Up": card(20003, midi__transpose=12) | card(20004, midi__velocity=0.86),
    "Same Octave": card(20003, midi__transpose=0) | card(20004, midi__velocity=0.9),
    "Two Octaves Up": card(20003, midi__transpose=24) | card(20004, midi__velocity=0.8),
    "Octave Down": card(20003, midi__transpose=-12) | card(20004, midi__velocity=0.95),
    "Fifth Up": card(20003, midi__transpose=7) | card(20004, midi__velocity=0.85),
    "Soft Accent": card(20003, midi__transpose=12) | card(20004, midi__velocity=0.6),
}

PRESETS["note-shaper"] = {
    "Full Range": card(21002, midi__note__minimum=0, midi__note__maximum=127) | card(21003, midi__velocity=1.0),
    "Piano Range": card(21002, midi__note__minimum=21, midi__note__maximum=108) | card(21003, midi__velocity=1.0),
    "Lead Register": card(21002, midi__note__minimum=60, midi__note__maximum=96) | card(21003, midi__velocity=0.95),
    "Bass Register": card(21002, midi__note__minimum=24, midi__note__maximum=55) | card(21003, midi__velocity=1.0),
    "Softer Playing": card(21002, midi__note__minimum=0, midi__note__maximum=127) | card(21003, midi__velocity=0.7),
    "Harder Playing": card(21002, midi__note__minimum=0, midi__note__maximum=127) | card(21003, midi__velocity=1.3),
}

PRESETS["octave-up"] = {
    "Octave Up": card(33002, midi__transpose=12),
    "Two Octaves Up": card(33002, midi__transpose=24),
    "Octave Down": card(33002, midi__transpose=-12),
    "Fifth Up": card(33002, midi__transpose=7),
    "Fourth Down": card(33002, midi__transpose=-5),
}

PRESETS["bass-zone"] = {
    "Below Middle C": card(34002, midi__note__minimum=0, midi__note__maximum=59) | card(34003, midi__velocity=0.95),
    "Low Octave Only": card(34002, midi__note__minimum=24, midi__note__maximum=47) | card(34003, midi__velocity=1.0),
    "Above Middle C": card(34002, midi__note__minimum=60, midi__note__maximum=127) | card(34003, midi__velocity=0.95),
    "Left Hand Split": card(34002, midi__note__minimum=0, midi__note__maximum=53) | card(34003, midi__velocity=1.0),
    "Right Hand Split": card(34002, midi__note__minimum=54, midi__note__maximum=127) | card(34003, midi__velocity=1.0),
}

# ---- Modulation modules --------------------------------------------------------------------

PRESETS["lfo"] = {
    "Slow Sine": card(9001, lfo__rate=0.2, lfo__depth=1, lfo__shape=SINE),
    "Gentle Wobble": card(9001, lfo__rate=1.5, lfo__depth=0.5, lfo__shape=SINE),
    "Fast Vibrato": card(9001, lfo__rate=6, lfo__depth=0.2, lfo__shape=SINE),
    "Triangle Sweep": card(9001, lfo__rate=0.5, lfo__depth=1, lfo__shape=TRIANGLE),
    "Saw Ramp": card(9001, lfo__rate=1, lfo__depth=1, lfo__shape=SAW),
    "Square Steps": card(9001, lfo__rate=2, lfo__depth=1, lfo__shape=SQUARE),
}

PRESETS["unipolar-lfo"] = {
    "Slow Rise": card(22001, lfo__rate=0.2, lfo__depth=1, lfo__shape=SINE),
    "Pulse": card(22001, lfo__rate=2, lfo__depth=1, lfo__shape=SQUARE),
    "Breathing": card(22001, lfo__rate=0.4, lfo__depth=1, lfo__shape=TRIANGLE),
    "Ramp Up": card(22001, lfo__rate=0.5, lfo__depth=1, lfo__shape=SAW),
    "Half Depth": card(22001, lfo__rate=1, lfo__depth=1, lfo__shape=SINE) | card(22002, map__outputMinimum=0.25, map__outputMaximum=0.75),
}

# ---- Analysers ---------------------------------------------------------------------------

PRESETS["spectrometer-analyzer"] = {
    "Mixing (3 dB slope)": card(48002, spectrometer__floor=-90, spectrometer__tilt=3, spectrometer__smoothing=0.5,
                                spectrometer__hold=2, spectrometer__history=8),
    "Flat Measurement": card(48002, spectrometer__floor=-110, spectrometer__tilt=0, spectrometer__smoothing=0.2,
                             spectrometer__hold=0, spectrometer__history=4),
    "Mastering (4.5 dB)": card(48002, spectrometer__floor=-80, spectrometer__tilt=4.5, spectrometer__smoothing=0.7,
                               spectrometer__hold=4, spectrometer__history=16),
    "Fast Detail": card(48002, spectrometer__floor=-100, spectrometer__tilt=3, spectrometer__smoothing=0.05,
                        spectrometer__hold=1, spectrometer__history=3),
    "Long History": card(48002, spectrometer__floor=-90, spectrometer__tilt=3, spectrometer__smoothing=0.6,
                         spectrometer__hold=6, spectrometer__history=30),
}

# ---- Plain cards: dynamics -----------------------------------------------------------------

PRESETS["compressor"] = {
    "Gentle Glue": plain(compressor__threshold=-12, compressor__ratio=2),
    "Vocal Leveler": plain(compressor__threshold=-20, compressor__ratio=4),
    "Drum Smash": plain(compressor__threshold=-32, compressor__ratio=10),
    "Bass Control": plain(compressor__threshold=-18, compressor__ratio=5),
    "Bus Compression": plain(compressor__threshold=-10, compressor__ratio=1.8),
    "Heavy Squash": plain(compressor__threshold=-40, compressor__ratio=20),
    "Acoustic Guitar": plain(compressor__threshold=-16, compressor__ratio=3),
}

PRESETS["limiter"] = {
    "Safe Peak": plain(limiter__gain=0, limiter__ceiling=-1, limiter__release=100),
    "Loud Master": plain(limiter__gain=6, limiter__ceiling=-0.3, limiter__release=60),
    "Very Loud": plain(limiter__gain=10, limiter__ceiling=-0.3, limiter__release=40),
    "Streaming (-1 dB)": plain(limiter__gain=3, limiter__ceiling=-1, limiter__release=80),
    "Drum Bus Catch": plain(limiter__gain=2, limiter__ceiling=-2, limiter__release=20),
    "Slow and Smooth": plain(limiter__gain=4, limiter__ceiling=-0.5, limiter__release=300),
}

PRESETS["gate"] = {
    "Noise Gate": plain(gate__threshold=-50, gate__attack=1, gate__release=150, gate__range=-80),
    "Tight Drums": plain(gate__threshold=-30, gate__attack=0.3, gate__release=60, gate__range=-80),
    "Vocal Breaths": plain(gate__threshold=-45, gate__attack=2, gate__release=200, gate__range=-12),
    "Guitar Amp Hiss": plain(gate__threshold=-55, gate__attack=1, gate__release=120, gate__range=-30),
    "Gated Reverb": plain(gate__threshold=-24, gate__attack=0.5, gate__release=40, gate__range=-80),
    "Soft Reduction": plain(gate__threshold=-40, gate__attack=5, gate__release=400, gate__range=-8),
}

PRESETS["transient-shaper"] = {
    "More Attack": plain(transient__attack=0.6, transient__sustain=0.0, transient__output=-1),
    "Punchy Drums": plain(transient__attack=0.8, transient__sustain=-0.3, transient__output=-1.5),
    "Softer Hits": plain(transient__attack=-0.6, transient__sustain=0.0, transient__output=1),
    "More Room": plain(transient__attack=0.0, transient__sustain=0.7, transient__output=-2),
    "Dry and Tight": plain(transient__attack=0.3, transient__sustain=-0.8, transient__output=1),
    "Pluck Snap": plain(transient__attack=1.0, transient__sustain=-0.2, transient__output=-2),
}

# ---- Plain cards: colour -------------------------------------------------------------------

PRESETS["drive"] = {
    "Subtle Warmth": plain(drive__db=4),
    "Crunch": plain(drive__db=12),
    "Heavy Drive": plain(drive__db=22),
    "Fuzz Wall": plain(drive__db=34),
}

PRESETS["hard-clip"] = {
    "Soft Ceiling": plain(clip__threshold=0.95),
    "Clipped Drums": plain(clip__threshold=0.75),
    "Aggressive": plain(clip__threshold=0.5),
    "Destroyed": plain(clip__threshold=0.2),
}

PRESETS["bitcrusher"] = {
    "Retro 12-bit": plain(crusher__bits=12, crusher__downsample=2, crusher__drive=2, crusher__mix=1),
    "8-bit Console": plain(crusher__bits=8, crusher__downsample=4, crusher__drive=0, crusher__mix=1),
    "Lo-Fi Sampler": plain(crusher__bits=10, crusher__downsample=3, crusher__drive=4, crusher__mix=0.6),
    "Telephone Grit": plain(crusher__bits=6, crusher__downsample=8, crusher__drive=6, crusher__mix=0.8),
    "Total Destruction": plain(crusher__bits=3, crusher__downsample=16, crusher__drive=12, crusher__mix=1),
    "Parallel Crush": plain(crusher__bits=5, crusher__downsample=6, crusher__drive=8, crusher__mix=0.3),
}

# ---- Plain cards: time and space ------------------------------------------------------------

PRESETS["delay"] = {
    "Slapback": plain(delay__time=110, delay__feedback=0.1, delay__mix=0.25),
    "Quarter Note (120)": plain(delay__time=500, delay__feedback=0.35, delay__mix=0.25),
    "Dotted Eighth (120)": plain(delay__time=375, delay__feedback=0.4, delay__mix=0.28),
    "Long Feedback": plain(delay__time=750, delay__feedback=0.75, delay__mix=0.3),
    "Doubler": plain(delay__time=25, delay__feedback=0.0, delay__mix=0.4),
    "Subtle Echo": plain(delay__time=300, delay__feedback=0.2, delay__mix=0.12),
}

PRESETS["reverb"] = {
    "Small Room": plain(reverb__roomSize=0.35, reverb__mix=0.18),
    "Plate": plain(reverb__roomSize=0.6, reverb__mix=0.25),
    "Hall": plain(reverb__roomSize=0.8, reverb__mix=0.3),
    "Cathedral": plain(reverb__roomSize=0.95, reverb__mix=0.45),
    "Ambient Wash": plain(reverb__roomSize=0.92, reverb__mix=0.7),
    "Touch of Space": plain(reverb__roomSize=0.5, reverb__mix=0.08),
}

PRESETS["ping-pong"] = {
    "Eighth Ping-Pong": plain(pingpong__time=250, pingpong__feedback=0.45, pingpong__mix=0.3, pingpong__width=1, pingpong__sync=SYNC_8),
    "Quarter Bounce": plain(pingpong__time=500, pingpong__feedback=0.4, pingpong__mix=0.28, pingpong__width=1, pingpong__sync=SYNC_4),
    "Dotted Eighth": plain(pingpong__time=375, pingpong__feedback=0.5, pingpong__mix=0.3, pingpong__width=1,
                           pingpong__sync=SYNC_8_DOTTED),
    "Sixteenth Ripple": plain(pingpong__time=125, pingpong__feedback=0.55, pingpong__mix=0.25, pingpong__width=0.8,
                              pingpong__sync=SYNC_16),
    "Free Wide Echo": plain(pingpong__time=420, pingpong__feedback=0.5, pingpong__mix=0.32, pingpong__width=1, pingpong__sync=FREE),
    "Dub Throw": plain(pingpong__time=750, pingpong__feedback=0.8, pingpong__mix=0.35, pingpong__width=1, pingpong__sync=SYNC_4_DOTTED),
}

# ---- Plain cards: modulation ----------------------------------------------------------------

PRESETS["chorus"] = {
    "Classic Chorus": plain(chorus__rate=0.6, chorus__depth=0.5, chorus__delay=14, chorus__width=0.8, chorus__mix=0.5),
    "Subtle Thickener": plain(chorus__rate=0.3, chorus__depth=0.25, chorus__delay=10, chorus__width=0.6, chorus__mix=0.3),
    "Lush Strings": plain(chorus__rate=0.4, chorus__depth=0.7, chorus__delay=22, chorus__width=1.0, chorus__mix=0.55),
    "Fast Vibrato": plain(chorus__rate=5.5, chorus__depth=0.4, chorus__delay=6, chorus__width=0.5, chorus__mix=1.0),
    "Wide Ensemble": plain(chorus__rate=0.9, chorus__depth=0.6, chorus__delay=18, chorus__width=1.0, chorus__mix=0.6),
    "Seasick": plain(chorus__rate=2.5, chorus__depth=1.0, chorus__delay=30, chorus__width=0.9, chorus__mix=0.7),
}

PRESETS["flanger"] = {
    "Jet Sweep": plain(flanger__rate=0.15, flanger__depth=0.9, flanger__delay=2.0, flanger__feedback=0.7, flanger__mix=0.5),
    "Subtle Flange": plain(flanger__rate=0.25, flanger__depth=0.4, flanger__delay=1.2, flanger__feedback=0.3, flanger__mix=0.35),
    "Metallic Resonance": plain(flanger__rate=0.4, flanger__depth=0.6, flanger__delay=0.8, flanger__feedback=0.9, flanger__mix=0.5),
    "Negative Hollow": plain(flanger__rate=0.3, flanger__depth=0.7, flanger__delay=1.5, flanger__feedback=-0.75, flanger__mix=0.5),
    "Fast Warble": plain(flanger__rate=3.0, flanger__depth=0.5, flanger__delay=1.0, flanger__feedback=0.4, flanger__mix=0.45),
    "Through-Zero Style": plain(flanger__rate=0.1, flanger__depth=1.0, flanger__delay=0.3, flanger__feedback=0.5, flanger__mix=0.5),
}

PRESETS["phaser"] = {
    "Classic Phaser": plain(phaser__rate=0.35, phaser__depth=0.8, phaser__frequency=700, phaser__feedback=0.5, phaser__mix=0.5),
    "Slow Swirl": plain(phaser__rate=0.1, phaser__depth=1.0, phaser__frequency=500, phaser__feedback=0.6, phaser__mix=0.5),
    "Funky Phase": plain(phaser__rate=1.5, phaser__depth=0.7, phaser__frequency=1200, phaser__feedback=0.4, phaser__mix=0.5),
    "Resonant Sweep": plain(phaser__rate=0.25, phaser__depth=0.9, phaser__frequency=900, phaser__feedback=0.85, phaser__mix=0.55),
    "Subtle Motion": plain(phaser__rate=0.2, phaser__depth=0.4, phaser__frequency=600, phaser__feedback=0.2, phaser__mix=0.3),
    "Fast Bubbles": plain(phaser__rate=5.0, phaser__depth=0.6, phaser__frequency=1800, phaser__feedback=0.5, phaser__mix=0.45),
}

PRESETS["tremolo-pan"] = {
    "Classic Tremolo": plain(tremolo__rate=4, tremolo__depth=0.6, tremolo__shape=SINE, tremolo__spread=0, tremolo__sync=FREE),
    "Eighth Chop": plain(tremolo__rate=4, tremolo__depth=1.0, tremolo__shape=SQUARE, tremolo__spread=0, tremolo__sync=SYNC_8),
    "Auto-Pan Quarter": plain(tremolo__rate=2, tremolo__depth=0.8, tremolo__shape=SINE, tremolo__spread=1, tremolo__sync=SYNC_4),
    "Slow Stereo Drift": plain(tremolo__rate=0.3, tremolo__depth=0.6, tremolo__shape=TRIANGLE, tremolo__spread=1, tremolo__sync=FREE),
    "Sixteenth Gate": plain(tremolo__rate=8, tremolo__depth=1.0, tremolo__shape=SQUARE, tremolo__spread=0, tremolo__sync=SYNC_16),
    "Vintage Amp Tremolo": plain(tremolo__rate=5.5, tremolo__depth=0.5, tremolo__shape=TRIANGLE, tremolo__spread=0, tremolo__sync=FREE),
}

# ---- Plain cards: filters and EQ -----------------------------------------------------------

PRESETS["lowpass"] = {
    "Gentle Top Cut": plain(lowpass__cutoff=12000, lowpass__resonance=0.7),
    "Warm Roll-Off": plain(lowpass__cutoff=6000, lowpass__resonance=0.7),
    "Muffled": plain(lowpass__cutoff=1200, lowpass__resonance=0.8),
    "Resonant Peak": plain(lowpass__cutoff=2000, lowpass__resonance=6.0),
    "Telephone Top": plain(lowpass__cutoff=3400, lowpass__resonance=1.2),
    "Bass Only": plain(lowpass__cutoff=250, lowpass__resonance=0.7),
}

PRESETS["highpass"] = {
    "Rumble Cut": plain(highpass__cutoff=30),
    "Vocal Clean": plain(highpass__cutoff=100),
    "Thin Out": plain(highpass__cutoff=300),
    "Telephone Bottom": plain(highpass__cutoff=500),
    "Hats Only": plain(highpass__cutoff=4000),
}

PRESETS["eq"] = {
    "Brighter": plain(eq__tone=0.4),
    "A Bit Brighter": plain(eq__tone=0.15),
    "Neutral": plain(eq__tone=0.0),
    "A Bit Warmer": plain(eq__tone=-0.15),
    "Darker": plain(eq__tone=-0.4),
}

PRESETS["eq-band"] = {
    "Low Boost": plain(eqBand__frequency=100, eqBand__gain=4, eqBand__q=0.8),
    "Mud Cut": plain(eqBand__frequency=300, eqBand__gain=-4, eqBand__q=1.2),
    "Boxiness Cut": plain(eqBand__frequency=600, eqBand__gain=-3, eqBand__q=1.5),
    "Presence Lift": plain(eqBand__frequency=3000, eqBand__gain=3, eqBand__q=1.0),
    "Harsh Notch": plain(eqBand__frequency=3500, eqBand__gain=-6, eqBand__q=6.0),
    "Air Boost": plain(eqBand__frequency=12000, eqBand__gain=4, eqBand__q=0.7),
}

PRESETS["multimode-filter"] = {
    "Warm Low-Pass": plain(filter__mode=LOW_PASS, filter__cutoff=3000, filter__resonance=0.9, filter__drive=2),
    "Squelchy Low-Pass": plain(filter__mode=LOW_PASS, filter__cutoff=800, filter__resonance=8.0, filter__drive=6),
    "Clean High-Pass": plain(filter__mode=HIGH_PASS, filter__cutoff=200, filter__resonance=0.7, filter__drive=0),
    "Telephone Band": plain(filter__mode=BAND_PASS, filter__cutoff=1500, filter__resonance=1.5, filter__drive=4),
    "Narrow Notch": plain(filter__mode=NOTCH, filter__cutoff=1000, filter__resonance=4.0, filter__drive=0),
    "Driven Band": plain(filter__mode=BAND_PASS, filter__cutoff=700, filter__resonance=5.0, filter__drive=12),
}

PRESETS["auto-filter"] = {
    "Slow Sweep": plain(autofilter__cutoff=600, autofilter__resonance=2.5, autofilter__rate=0.2, autofilter__depth=0.7,
                        autofilter__sync=FREE),
    "Quarter Wobble": plain(autofilter__cutoff=700, autofilter__resonance=4.0, autofilter__rate=2, autofilter__depth=0.8,
                            autofilter__sync=SYNC_4),
    "Eighth Wobble": plain(autofilter__cutoff=500, autofilter__resonance=5.0, autofilter__rate=4, autofilter__depth=0.85,
                           autofilter__sync=SYNC_8),
    "Gentle Breathing": plain(autofilter__cutoff=2000, autofilter__resonance=1.2, autofilter__rate=0.3, autofilter__depth=0.4,
                              autofilter__sync=FREE),
    "Sixteenth Gate Filter": plain(autofilter__cutoff=1200, autofilter__resonance=3.0, autofilter__rate=8, autofilter__depth=0.9,
                                   autofilter__sync=SYNC_16),
    "Bar-Long Rise": plain(autofilter__cutoff=400, autofilter__resonance=3.5, autofilter__rate=0.5, autofilter__depth=1.0,
                           autofilter__sync=SYNC_BAR),
}

PRESETS["eq-three"] = {
    "Smile Curve": plain(eq3__low=4, eq3__mid=-3, eq3__high=4, eq3__lowFrequency=200, eq3__highFrequency=4000),
    "Mid Focus": plain(eq3__low=-3, eq3__mid=3, eq3__high=-2, eq3__lowFrequency=250, eq3__highFrequency=3500),
    "Kill Lows (DJ)": plain(eq3__low=-24, eq3__mid=0, eq3__high=0, eq3__lowFrequency=200, eq3__highFrequency=3500),
    "Kill Highs (DJ)": plain(eq3__low=0, eq3__mid=0, eq3__high=-24, eq3__lowFrequency=250, eq3__highFrequency=3000),
    "Warm Mix": plain(eq3__low=2, eq3__mid=0, eq3__high=-2, eq3__lowFrequency=150, eq3__highFrequency=6000),
    "Bright Mix": plain(eq3__low=-1, eq3__mid=0, eq3__high=3, eq3__lowFrequency=250, eq3__highFrequency=5000),
    "Radio Voice": plain(eq3__low=-12, eq3__mid=4, eq3__high=-8, eq3__lowFrequency=400, eq3__highFrequency=2500),
}

PRESETS["stereo-width"] = {
    "Mono": plain(stereo__width=0.0),
    "Narrow": plain(stereo__width=0.5),
    "Wider": plain(stereo__width=1.3),
    "Very Wide": plain(stereo__width=1.7),
    "Extreme": plain(stereo__width=2.0),
}

# ---- Plain cards: sources -------------------------------------------------------------------

PRESETS["wavetable"] = {
    "Soft Start": plain(wavetable__position=0.1, wavetable__level=0.14, wavetable__detune=4),
    "Middle Table": plain(wavetable__position=0.5, wavetable__level=0.13, wavetable__detune=8),
    "Bright End": plain(wavetable__position=0.9, wavetable__level=0.12, wavetable__detune=10),
    "Detuned Wide": plain(wavetable__position=0.4, wavetable__level=0.12, wavetable__detune=30),
    "Clean Unison": plain(wavetable__position=0.35, wavetable__level=0.14, wavetable__detune=0),
}

PRESETS["string"] = {
    "Nylon Pluck": plain(string__damping=0.45, string__decay=3),
    "Steel String": plain(string__damping=0.2, string__decay=6),
    "Muted String": plain(string__damping=0.8, string__decay=0.6),
    "Long Harp": plain(string__damping=0.3, string__decay=12),
    "Bright Koto": plain(string__damping=0.1, string__decay=2),
}

PRESETS["noise"] = {
    "Whisper": plain(noise__level=0.02),
    "Soft Hiss": plain(noise__level=0.06),
    "Loud Noise": plain(noise__level=0.2),
}

# ---- Plain cards: MIDI ----------------------------------------------------------------------

PRESETS["velocity-map"] = {
    "Softer": plain(midi__velocity=0.7),
    "Much Softer": plain(midi__velocity=0.45),
    "Harder": plain(midi__velocity=1.3),
    "Much Harder": plain(midi__velocity=1.7),
}

PRESETS["midi-transpose"] = {
    "Octave Up": plain(midi__transpose=12),
    "Octave Down": plain(midi__transpose=-12),
    "Fifth Up": plain(midi__transpose=7),
    "Fourth Up": plain(midi__transpose=5),
    "Whole Tone Down": plain(midi__transpose=-2),
}

PRESETS["lfo-core"] = {
    "Slow Sine": plain(lfo__rate=0.2, lfo__depth=1, lfo__shape=SINE),
    "Vibrato": plain(lfo__rate=6, lfo__depth=0.2, lfo__shape=SINE),
    "Triangle Sweep": plain(lfo__rate=0.5, lfo__depth=1, lfo__shape=TRIANGLE),
    "Saw Ramp": plain(lfo__rate=1, lfo__depth=1, lfo__shape=SAW),
    "Square Steps": plain(lfo__rate=2, lfo__depth=1, lfo__shape=SQUARE),
}
