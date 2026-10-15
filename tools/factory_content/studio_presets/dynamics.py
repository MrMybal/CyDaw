"""The dynamics, the loudness meter and the Égaliseur Pro.

Values by binding suffix; what is not given keeps its default. The helpers
below write a module's settings whole (every meaningful value of it), so a
preset says what it does rather than leaning on the defaults.
"""
from __future__ import annotations

# Compresseur Pro styles (ProEffectsDesign.h).
CLEAN, CLASSIC, OPTO, VOCAL, MASTERING, BUS, PUNCH, PUMPING = range(8)
# Compresseur Pro channels: both, the middle only, the sides only.
STEREO, MID, SIDE = range(3)
# Limiteur Pro styles.
TRANSPARENT, PUNCHY, DYNAMIC, ALLROUND, AGGRESSIVE, MODERN, SAFE, BUS_LIMIT = range(8)
# Limiteur Pro dither (off, 24, 20, 18, 16 bits) and its noise shaping.
NO_DITHER, DITHER_24, DITHER_20, DITHER_18, DITHER_16 = range(5)
SHAPING_BASIC, SHAPING_OPTIMISED, SHAPING_WEIGHTED = range(3)
# Multibande Pro band modes.
COMPRESS, EXPAND, RAISE, BURST = range(4)


def comp(style: int, threshold: float, ratio: float, knee: float, attack: float, release: float, makeup: float = 0.0, *,
         mix: float = 1.0, detector_high_pass: float = 20.0, detector_low_pass: float = 20_000.0, reach: float = 60.0,
         hold: float = 0.0, lookahead: float = 0.0, auto_release: bool = False, auto_gain: bool = False, link: float = 1.0,
         channels: int = STEREO) -> dict:
    """A Compresseur Pro, whole: `reach` is its range (the deepest it goes,
    dB); the detector hears only what lies between its two filters."""
    return {"comppro.style": float(style), "comppro.threshold": float(threshold), "comppro.ratio": float(ratio),
            "comppro.knee": float(knee), "comppro.attack": float(attack), "comppro.release": float(release),
            "comppro.makeup": float(makeup), "comppro.mix": float(mix), "comppro.sidechain": float(detector_high_pass),
            "comppro.sidechainLow": float(detector_low_pass), "comppro.range": float(reach), "comppro.hold": float(hold),
            "comppro.lookahead": float(lookahead), "comppro.autoRelease": 1.0 if auto_release else 0.0,
            "comppro.autoGain": 1.0 if auto_gain else 0.0, "comppro.link": float(link), "comppro.channels": float(channels)}


def limit(style: int, gain: float, ceiling: float, lookahead: float, attack: float, release: float, *,
          link_transients: float = 0.75, link_release: float = 1.0, true_peak: bool = True, oversampling: int = 1,
          dither: int = NO_DITHER, shaping: int = SHAPING_OPTIMISED, dc_filter: bool = False, target: float = -14.0) -> dict:
    """A Limiteur Pro, whole: oversampling 0..3 is 1x..8x; the target is the
    meters' mark (LUFS)."""
    return {"limpro.style": float(style), "limpro.gain": float(gain), "limpro.output": float(ceiling),
            "limpro.lookahead": float(lookahead), "limpro.attack": float(attack), "limpro.release": float(release),
            "limpro.linkTransients": float(link_transients), "limpro.linkRelease": float(link_release),
            "limpro.truePeak": 1.0 if true_peak else 0.0, "limpro.oversampling": float(oversampling),
            "limpro.dither": float(dither), "limpro.shaping": float(shaping), "limpro.dcFilter": 1.0 if dc_filter else 0.0,
            "limpro.unity": 0.0, "limpro.target": float(target)}


def mb_band(number: int, mode: int, threshold: float, ratio: float, reach: float, attack: float, release: float,
            knee: float = 6.0, makeup: float = 0.0) -> dict:
    """A Multibande Pro band, on: its mode, threshold, ratio, range (the
    most it changes, dB), times, knee and gain."""
    band = "multiband.band%d" % number
    return {"multiband.threshold%d" % number: float(threshold), band + "On": 1.0, band + "Mode": float(mode),
            band + "Ratio": float(ratio), band + "Range": float(reach), band + "Attack": float(attack),
            band + "Release": float(release), band + "Knee": float(knee), band + "Makeup": float(makeup)}


def multiband(crossovers: tuple, *bands: dict, input_gain: float = 0.0, output: float = 0.0, amount: float = 1.0,
              link: float = 1.0) -> dict:
    values = {"multiband.cross1": float(crossovers[0]), "multiband.cross2": float(crossovers[1]),
              "multiband.cross3": float(crossovers[2]), "multiband.input": float(input_gain), "multiband.output": float(output),
              "multiband.amount": float(amount), "multiband.link": float(link)}
    for band in bands:
        values.update(band)
    return values


def pression(depth: float, time: float, upward: float, downward: float, gains: tuple, crossovers: tuple, low: tuple,
             mid: tuple, high: tuple, input_gain: float = 0.0, output: float = 0.0) -> dict:
    """A Pression, whole. Each band: (down threshold, down ratio, up
    threshold, up ratio, attack, release); `gains`: low, mid, high (dB)."""
    values = {"pression.depth": float(depth), "pression.time": float(time), "pression.upward": float(upward),
              "pression.downward": float(downward), "pression.lowGain": float(gains[0]), "pression.midGain": float(gains[1]),
              "pression.highGain": float(gains[2]), "pression.crossLow": float(crossovers[0]),
              "pression.crossHigh": float(crossovers[1]), "pression.input": float(input_gain), "pression.output": float(output)}
    for name, band in (("low", low), ("mid", mid), ("high", high)):
        for field, value in zip(("DownThreshold", "DownRatio", "UpThreshold", "UpRatio", "Attack", "Release"), band):
            values["pression." + name + field] = float(value)
    return values


def velours(depth: float, sharpness: float, selectivity: float, attack: float, release: float, focus_low: float,
            focus_high: float, *, mix: float = 1.0, hard: bool = False, output: float = 0.0, link: float = 1.0) -> dict:
    """A Velours, whole (the delta, a listening aid, stays off)."""
    return {"velours.depth": float(depth), "velours.sharpness": float(sharpness), "velours.selectivity": float(selectivity),
            "velours.attack": float(attack), "velours.release": float(release), "velours.focusLow": float(focus_low),
            "velours.focusHigh": float(focus_high), "velours.mix": float(mix), "velours.mode": 1.0 if hard else 0.0,
            "velours.delta": 0.0, "velours.output": float(output), "velours.link": float(link)}


def meter(target: int, history: float, true_peak: float) -> dict:
    """A Sonomètre: its target (0 streaming -14, 1 streaming -16, 2
    streaming -15, 3 podcast -16, 4 EBU R128 -23, 5 US TV -24, 6 CD and
    club -9 LUFS), its history (s) and the true peak it allows (dB)."""
    return {"sonometre.target": float(target), "sonometre.history": float(history), "sonometre.truePeak": float(true_peak)}


PRESETS = {
    "comp-pro": {
        "Gentle Vocal": {"comppro.style": 3.0, "comppro.threshold": -22.0, "comppro.ratio": 3.0, "comppro.knee": 8.0,
                         "comppro.attack": 8.0, "comppro.release": 140.0, "comppro.autoGain": 1.0, "comppro.sidechain": 80.0},
        "Punchy Drums": {"comppro.style": 6.0, "comppro.threshold": -20.0, "comppro.ratio": 4.0, "comppro.attack": 25.0,
                         "comppro.release": 90.0, "comppro.makeup": 3.0, "comppro.hold": 10.0},
        "Bus Glue": {"comppro.style": 5.0, "comppro.threshold": -16.0, "comppro.ratio": 2.0, "comppro.knee": 6.0,
                     "comppro.attack": 30.0, "comppro.release": 300.0, "comppro.autoRelease": 1.0, "comppro.autoGain": 1.0},
        "Parallel Compression": {"comppro.style": 1.0, "comppro.threshold": -35.0, "comppro.ratio": 8.0, "comppro.attack": 3.0,
                                 "comppro.release": 120.0, "comppro.makeup": 10.0, "comppro.mix": 0.4},
        "Warm Opto": {"comppro.style": 2.0, "comppro.threshold": -20.0, "comppro.ratio": 3.0, "comppro.attack": 15.0,
                      "comppro.release": 400.0, "comppro.autoGain": 1.0},
        "Light Mastering": {"comppro.style": 4.0, "comppro.threshold": -12.0, "comppro.ratio": 1.5, "comppro.knee": 12.0,
                            "comppro.attack": 40.0, "comppro.release": 500.0, "comppro.autoRelease": 1.0, "comppro.range": 4.0,
                            "comppro.lookahead": 2.0},
        "Pumping": {"comppro.style": 7.0, "comppro.threshold": -30.0, "comppro.ratio": 10.0, "comppro.attack": 1.0,
                    "comppro.release": 220.0, "comppro.hold": 20.0},
        "Steady Bass": {"comppro.threshold": -20.0, "comppro.ratio": 5.0, "comppro.attack": 5.0, "comppro.release": 180.0,
                        "comppro.sidechainLow": 2_000.0, "comppro.autoGain": 1.0},
        # Vocals.
        "Pop Lead Vocal Tight Control": comp(VOCAL, -24.0, 4.0, 6.0, 3.0, 60.0, 5.0, detector_high_pass=100.0,
                                             detector_low_pass=10_000.0, reach=12.0, lookahead=1.0, auto_release=True),
        "Rap Vocal Upfront (Fast Attack)": comp(CLASSIC, -26.0, 6.0, 3.0, 1.0, 50.0, 7.0, detector_high_pass=120.0,
                                                detector_low_pass=9_000.0, reach=15.0, hold=5.0),
        "Backing Vocals Tucked Back": comp(OPTO, -28.0, 4.0, 10.0, 20.0, 300.0, 4.0, detector_high_pass=150.0,
                                           detector_low_pass=8_000.0, reach=10.0, auto_release=True),
        "Spoken Voice (Podcast and Radio)": comp(VOCAL, -26.0, 3.0, 10.0, 10.0, 180.0, 6.0, detector_high_pass=90.0,
                                                 detector_low_pass=8_000.0, reach=14.0, lookahead=2.0, auto_release=True),
        # Drums.
        "Snappy Kick": comp(PUNCH, -18.0, 4.0, 4.0, 12.0, 70.0, 3.0, reach=10.0, hold=5.0),
        "Thick Snare": comp(CLASSIC, -22.0, 5.0, 4.0, 8.0, 120.0, 4.0, detector_high_pass=100.0, detector_low_pass=12_000.0,
                            reach=12.0, hold=10.0),
        "Round Sustained Toms": comp(PUNCH, -26.0, 5.0, 4.0, 15.0, 200.0, 5.0, detector_high_pass=60.0, reach=12.0, hold=20.0),
        "Tamed Overheads and Cymbals": comp(CLEAN, -24.0, 3.0, 6.0, 2.0, 90.0, 1.5, detector_high_pass=400.0, reach=6.0,
                                            lookahead=1.0),
        "Explosive Parallel Drums": comp(CLASSIC, -40.0, 20.0, 2.0, 0.1, 50.0, 14.0, mix=0.3, detector_high_pass=120.0, reach=30.0),
        "Crushed Drum Room": comp(CLASSIC, -40.0, 20.0, 0.0, 0.2, 60.0, 14.0, mix=0.7, detector_high_pass=80.0, reach=24.0),
        # Bass.
        "Even DI Bass (Opto)": comp(OPTO, -24.0, 4.0, 8.0, 15.0, 250.0, 5.0, detector_low_pass=3_000.0, reach=12.0,
                                    auto_release=True),
        "Even Slap Bass": comp(CLEAN, -26.0, 8.0, 2.0, 0.5, 80.0, 6.0, detector_high_pass=30.0, reach=15.0, lookahead=2.0),
        # The detector hears the kick's low end once a sidechain is chosen.
        "Synth Bass Ducking Under the Kick (Sidechain)": comp(CLEAN, -30.0, 8.0, 0.0, 0.5, 120.0, 0.0,
                                                              detector_low_pass=500.0, reach=9.0, hold=15.0, lookahead=1.0),
        # Instruments.
        "Natural Acoustic Guitar": comp(OPTO, -22.0, 2.5, 12.0, 25.0, 220.0, 3.0, detector_high_pass=100.0,
                                        detector_low_pass=12_000.0, reach=8.0, auto_release=True),
        "Rhythm Electric Guitar": comp(CLASSIC, -18.0, 3.0, 6.0, 15.0, 100.0, 2.0, detector_high_pass=150.0, reach=6.0,
                                       auto_release=True),
        "Controlled Piano Without Squash": comp(CLEAN, -20.0, 2.0, 12.0, 30.0, 300.0, 2.5, detector_high_pass=60.0, reach=6.0,
                                                lookahead=3.0, auto_release=True),
        "Gentle Strings and Pads": comp(OPTO, -26.0, 2.0, 14.0, 60.0, 600.0, 3.0, detector_high_pass=80.0, reach=6.0,
                                        auto_release=True),
        "Brass and Saxophone": comp(CLASSIC, -20.0, 3.5, 6.0, 10.0, 120.0, 3.0, detector_high_pass=120.0,
                                    detector_low_pass=10_000.0, reach=10.0, auto_release=True),
        "Breathing Pad (Kick Sidechain)": comp(PUMPING, -35.0, 20.0, 0.0, 0.3, 180.0, 0.0, detector_low_pass=500.0, reach=18.0,
                                               hold=30.0),
        # Buses and master.
        "Rock Mix Bus (Slow Attack)": comp(BUS, -18.0, 4.0, 4.0, 30.0, 100.0, 2.0, mix=0.85, detector_high_pass=120.0, reach=5.0,
                                           auto_release=True),
        "Mid-Side Master (Mid Only)": comp(MASTERING, -16.0, 2.0, 10.0, 30.0, 250.0, 1.0, detector_high_pass=60.0, reach=3.0,
                                           lookahead=3.0, auto_release=True, channels=MID),
        "Calmer Stereo Sides (Side Only)": comp(CLEAN, -28.0, 3.0, 8.0, 5.0, 150.0, 0.0, detector_high_pass=200.0, reach=6.0,
                                                channels=SIDE),
        # The detector hears the voice's band once a sidechain is chosen.
        "Music Ducking Under the Voice (Sidechain)": comp(CLEAN, -32.0, 6.0, 6.0, 10.0, 400.0, 0.0, detector_high_pass=150.0,
                                                          detector_low_pass=4_000.0, reach=8.0, hold=120.0, lookahead=5.0),
    },
    "limiter-pro": {
        "Transparent Mastering": {"limpro.style": 0.0, "limpro.gain": 4.0, "limpro.output": -1.0, "limpro.lookahead": 1.5,
                                  "limpro.release": 120.0, "limpro.truePeak": 1.0, "limpro.oversampling": 2.0},
        "Loud and Modern": {"limpro.style": 5.0, "limpro.gain": 8.0, "limpro.output": -0.8, "limpro.release": 60.0,
                            "limpro.truePeak": 1.0, "limpro.oversampling": 2.0},
        "Punchy": {"limpro.style": 1.0, "limpro.gain": 6.0, "limpro.output": -1.0, "limpro.release": 40.0},
        "Aggressive": {"limpro.style": 4.0, "limpro.gain": 10.0, "limpro.output": -0.5, "limpro.release": 20.0,
                       "limpro.lookahead": 0.5},
        "Drum Bus": {"limpro.style": 7.0, "limpro.gain": 5.0, "limpro.output": -3.0, "limpro.release": 80.0,
                     "limpro.linkTransients": 0.5},
        "Distortion-Free Safety": {"limpro.style": 6.0, "limpro.output": -1.0, "limpro.lookahead": 5.0,
                                   "limpro.release": 200.0, "limpro.truePeak": 1.0, "limpro.oversampling": 3.0},
        # The target is the meters' mark: the gain brings the integrated
        # loudness (I) to it, by ear and eye, for each piece.
        "Streaming -14 LUFS": {"limpro.style": 2.0, "limpro.gain": 3.0, "limpro.output": -1.0, "limpro.target": -14.0,
                               "limpro.truePeak": 1.0},
        "CD 16-bit": {"limpro.style": 0.0, "limpro.gain": 5.0, "limpro.output": -0.3, "limpro.dither": 4.0,
                      "limpro.shaping": 2.0},
        # Masters by genre and delivery.
        "Pop Streaming Master (-14 LUFS)": limit(MODERN, 6.0, -1.0, 2.0, 8.0, 80.0, link_transients=0.6, link_release=0.9,
                                                 oversampling=2, dither=DITHER_24, dc_filter=True, target=-14.0),
        "Pop Radio Master (-11 LUFS)": limit(ALLROUND, 8.0, -1.0, 2.0, 8.0, 70.0, link_transients=0.6, link_release=0.9,
                                             oversampling=2, dither=DITHER_24, dc_filter=True, target=-11.0),
        "Loud Club Master (-8 LUFS)": limit(AGGRESSIVE, 11.0, -0.3, 1.0, 3.0, 30.0, link_transients=0.4, link_release=0.8,
                                            oversampling=3, dc_filter=True, target=-8.0),
        "Powerful Electronic Master": limit(MODERN, 9.0, -0.8, 1.5, 5.0, 50.0, link_transients=0.5, link_release=0.8,
                                            oversampling=3, dc_filter=True, target=-9.0),
        "Trap Master (Bass and Transients)": limit(PUNCHY, 10.0, -0.5, 0.5, 2.0, 25.0, link_transients=0.5, link_release=0.7,
                                                   oversampling=3, dc_filter=True, target=-8.0),
        "Hip-Hop Master (Solid Low End)": limit(DYNAMIC, 7.0, -1.0, 4.0, 15.0, 150.0, link_transients=0.8, oversampling=2,
                                                dc_filter=True, target=-9.0),
        "Dense Rock Master": limit(ALLROUND, 8.0, -1.0, 1.5, 10.0, 60.0, link_transients=0.7, oversampling=2, dc_filter=True,
                                   target=-10.0),
        "Metal Master (Wall of Sound)": limit(MODERN, 10.0, -0.8, 1.0, 5.0, 40.0, link_transients=0.5, link_release=0.8,
                                              oversampling=3, dc_filter=True, target=-8.0),
        "Acoustic and Jazz Master (Dynamics Kept)": limit(DYNAMIC, 3.0, -1.0, 3.0, 20.0, 250.0, link_transients=0.9,
                                                          oversampling=2, dither=DITHER_24, target=-16.0),
        "Classical and Orchestral Master": limit(TRANSPARENT, 2.0, -1.0, 5.0, 30.0, 400.0, link_transients=1.0, oversampling=3,
                                                 dither=DITHER_24, shaping=SHAPING_WEIGHTED, target=-18.0),
        "Loud 16-bit CD (Pop)": limit(MODERN, 9.0, -0.3, 1.5, 6.0, 60.0, link_transients=0.6, link_release=0.9, oversampling=3,
                                      dither=DITHER_16, shaping=SHAPING_OPTIMISED, dc_filter=True, target=-9.0),
        "24-bit Export with Dither": limit(TRANSPARENT, 4.0, -1.0, 1.5, 5.0, 120.0, oversampling=2, dither=DITHER_24,
                                           shaping=SHAPING_BASIC, target=-14.0),
        "Vinyl Pre-Master": limit(DYNAMIC, 2.0, -1.5, 5.0, 25.0, 200.0, link_transients=1.0, oversampling=2, dc_filter=True,
                                  target=-12.0),
        "Deliberate Lo-Fi Clipping": limit(AGGRESSIVE, 12.0, -1.0, 0.0, 1.0, 10.0, link_transients=0.3, link_release=0.5,
                                           true_peak=False, oversampling=0, target=-9.0),
        # Broadcast and speech.
        "Film and TV EBU R128 (-23 LUFS)": limit(SAFE, 0.0, -1.0, 5.0, 20.0, 300.0, link_transients=1.0, oversampling=3,
                                                 dc_filter=True, target=-23.0),
        "US Television (-24 LUFS)": limit(SAFE, 0.0, -2.0, 5.0, 20.0, 300.0, link_transients=1.0, oversampling=3,
                                          dc_filter=True, target=-24.0),
        "Podcast and Spoken Word (-16 LUFS)": limit(ALLROUND, 6.0, -1.0, 3.0, 10.0, 120.0, link_transients=1.0, oversampling=2,
                                                    dither=DITHER_24, dc_filter=True, target=-16.0),
        # On tracks and buses.
        "Lead Vocal Peak Catcher": limit(PUNCHY, 3.0, -3.0, 1.0, 5.0, 50.0, link_transients=1.0, true_peak=False, oversampling=1),
        "Bass at a Constant Ceiling": limit(BUS_LIMIT, 5.0, -4.0, 3.0, 10.0, 120.0, link_transients=1.0, true_peak=False,
                                            oversampling=1),
        "Kick and Snare Peak Control": limit(PUNCHY, 6.0, -2.0, 0.3, 1.0, 15.0, link_transients=1.0, true_peak=False,
                                             oversampling=2),
        "Synth and Instrument Bus": limit(BUS_LIMIT, 3.0, -2.0, 1.0, 15.0, 150.0, true_peak=False, oversampling=1),
    },
    # Multibande Pro: each band's mode (0 compress, 1 expand, 2 raise, 3 burst),
    # threshold, ratio, range, times, knee; a band off is left as it is.
    "multiband-pro": {
        "Gentle Mastering": {"multiband.threshold1": -18.0, "multiband.band1Ratio": 1.5, "multiband.band1Range": 6.0, "multiband.band1Knee": 12.0, "multiband.band1Attack": 30.0, "multiband.band1Release": 200.0, "multiband.threshold2": -18.0, "multiband.band2Ratio": 1.5, "multiband.band2Range": 6.0, "multiband.band2Knee": 12.0, "multiband.band2Attack": 30.0, "multiband.band2Release": 200.0, "multiband.threshold3": -18.0, "multiband.band3Ratio": 1.5, "multiband.band3Range": 6.0, "multiband.band3Knee": 12.0, "multiband.band3Attack": 20.0, "multiband.band3Release": 150.0, "multiband.threshold4": -18.0, "multiband.band4Ratio": 1.5, "multiband.band4Range": 6.0, "multiband.band4Knee": 12.0, "multiband.band4Attack": 10.0, "multiband.band4Release": 120.0},
        "Steady Bass": {"multiband.threshold1": -24.0, "multiband.band1Ratio": 4.0, "multiband.band1Range": 12.0, "multiband.band1Attack": 20.0, "multiband.band1Release": 150.0, "multiband.band2On": 0.0, "multiband.band3On": 0.0, "multiband.band4On": 0.0},
        "Sibilance": {"multiband.band1On": 0.0, "multiband.band2On": 0.0, "multiband.band3On": 0.0, "multiband.threshold4": -30.0, "multiband.band4Ratio": 6.0, "multiband.band4Range": 12.0, "multiband.band4Attack": 1.0, "multiband.band4Release": 60.0, "multiband.band4Knee": 3.0, "multiband.cross3": 5000.0},
        "Drum Gate": {"multiband.band1Mode": 1.0, "multiband.threshold1": -40.0, "multiband.band1Ratio": 3.0, "multiband.band1Range": 24.0, "multiband.band1Attack": 1.0, "multiband.band1Release": 80.0, "multiband.band2Mode": 1.0, "multiband.threshold2": -40.0, "multiband.band2Ratio": 3.0, "multiband.band2Range": 24.0, "multiband.band2Attack": 1.0, "multiband.band2Release": 80.0, "multiband.band3Mode": 1.0, "multiband.threshold3": -42.0, "multiband.band3Ratio": 3.0, "multiband.band3Range": 24.0, "multiband.band3Attack": 1.0, "multiband.band3Release": 60.0, "multiband.band4Mode": 1.0, "multiband.threshold4": -44.0, "multiband.band4Ratio": 3.0, "multiband.band4Range": 24.0, "multiband.band4Attack": 1.0, "multiband.band4Release": 60.0},
        "Full Vocal": {"multiband.band1On": 0.0, "multiband.band2Mode": 2.0, "multiband.threshold2": -36.0, "multiband.band2Ratio": 2.0, "multiband.band2Range": 6.0, "multiband.threshold3": -24.0, "multiband.band3Ratio": 3.0, "multiband.band3Range": 9.0, "multiband.band4On": 0.0},
        "Forward Attacks": {"multiband.band1On": 0.0, "multiband.band2On": 0.0, "multiband.band3Mode": 3.0, "multiband.threshold3": -24.0, "multiband.band3Ratio": 1.5, "multiband.band3Range": 6.0, "multiband.band3Attack": 5.0, "multiband.band3Release": 80.0, "multiband.band4Mode": 3.0, "multiband.threshold4": -26.0, "multiband.band4Ratio": 1.5, "multiband.band4Range": 6.0, "multiband.band4Attack": 3.0, "multiband.band4Release": 60.0},
        "Drum Bus": {"multiband.threshold1": -20.0, "multiband.band1Ratio": 3.0, "multiband.band1Range": 9.0, "multiband.threshold2": -24.0, "multiband.band2Ratio": 2.0, "multiband.band2Range": 6.0, "multiband.band3Mode": 3.0, "multiband.threshold3": -20.0, "multiband.band3Ratio": 1.3, "multiband.band3Range": 4.0, "multiband.band3Attack": 5.0, "multiband.threshold4": -26.0, "multiband.band4Ratio": 2.0, "multiband.band4Range": 6.0},
        # Masters.
        "Electronic Master (Tight Lows)": multiband(
            (90.0, 400.0, 5_000.0), mb_band(1, COMPRESS, -22.0, 4.0, 6.0, 10.0, 80.0, 6.0, 1.5),
            mb_band(2, COMPRESS, -24.0, 2.0, 3.0, 20.0, 150.0, 8.0, 1.0), mb_band(3, COMPRESS, -26.0, 1.5, 2.0, 15.0, 120.0, 10.0, 0.5),
            mb_band(4, BURST, -30.0, 1.3, 3.0, 3.0, 60.0, 6.0)),
        "Rock Master (Controlled Mids)": multiband(
            (120.0, 800.0, 4_000.0), mb_band(1, COMPRESS, -20.0, 2.5, 4.0, 30.0, 200.0, 8.0, 1.0),
            mb_band(2, COMPRESS, -22.0, 2.0, 3.0, 20.0, 150.0, 8.0, 0.5), mb_band(3, COMPRESS, -24.0, 2.5, 4.0, 10.0, 100.0, 6.0, 0.5),
            mb_band(4, COMPRESS, -26.0, 2.0, 3.0, 5.0, 80.0, 6.0, 1.0)),
        "Airy Acoustic Master": multiband(
            (150.0, 1_200.0, 7_000.0), mb_band(1, COMPRESS, -24.0, 1.6, 3.0, 30.0, 250.0, 12.0),
            mb_band(2, COMPRESS, -24.0, 1.4, 2.0, 25.0, 200.0, 12.0), mb_band(3, COMPRESS, -26.0, 1.6, 2.0, 15.0, 150.0, 12.0),
            mb_band(4, RAISE, -42.0, 1.5, 3.0, 20.0, 200.0, 10.0), amount=0.8),
        "Hip-Hop Master (Controlled Sub)": multiband(
            (60.0, 250.0, 5_000.0), mb_band(1, COMPRESS, -18.0, 3.0, 5.0, 20.0, 150.0, 6.0, 1.0),
            mb_band(2, COMPRESS, -20.0, 2.5, 4.0, 15.0, 120.0, 6.0, 0.5), mb_band(3, COMPRESS, -24.0, 1.5, 2.0, 10.0, 100.0, 10.0),
            mb_band(4, COMPRESS, -28.0, 2.0, 3.0, 3.0, 60.0, 6.0, 1.0)),
        "Club Master (Sub and Attacks)": multiband(
            (60.0, 200.0, 8_000.0), mb_band(1, COMPRESS, -14.0, 5.0, 6.0, 15.0, 100.0, 6.0, 1.0),
            mb_band(2, BURST, -24.0, 1.3, 2.0, 2.0, 50.0, 6.0), mb_band(3, COMPRESS, -22.0, 1.5, 2.0, 15.0, 150.0, 10.0),
            mb_band(4, COMPRESS, -24.0, 2.0, 3.0, 3.0, 60.0, 6.0, 0.5)),
        # Voices.
        "Lead Vocal Without Mud or Sibilance": multiband(
            (250.0, 1_500.0, 5_500.0), mb_band(1, COMPRESS, -30.0, 3.0, 6.0, 10.0, 120.0, 6.0),
            mb_band(2, COMPRESS, -26.0, 2.0, 3.0, 15.0, 150.0, 8.0), mb_band(3, COMPRESS, -28.0, 2.5, 4.0, 5.0, 80.0, 6.0),
            mb_band(4, COMPRESS, -34.0, 5.0, 8.0, 0.5, 50.0, 3.0)),
        "Dense Rap Vocal Without Sibilance": multiband(
            (200.0, 2_000.0, 6_000.0), mb_band(1, COMPRESS, -24.0, 3.0, 5.0, 10.0, 100.0, 6.0),
            mb_band(2, RAISE, -40.0, 2.0, 5.0, 10.0, 150.0, 8.0), mb_band(3, COMPRESS, -26.0, 3.0, 5.0, 3.0, 60.0, 6.0),
            mb_band(4, COMPRESS, -32.0, 6.0, 9.0, 0.3, 40.0, 3.0)),
        "Soft Vocal (Upward Compression)": multiband(
            (200.0, 2_000.0, 8_000.0), mb_band(1, COMPRESS, -30.0, 2.0, 3.0, 20.0, 150.0, 8.0),
            mb_band(2, RAISE, -42.0, 2.0, 6.0, 20.0, 200.0, 8.0), mb_band(3, RAISE, -44.0, 2.0, 6.0, 10.0, 150.0, 8.0),
            mb_band(4, RAISE, -48.0, 1.5, 4.0, 5.0, 100.0, 8.0), amount=0.8),
        "Even Podcast Voice": multiband(
            (150.0, 900.0, 4_500.0), mb_band(1, COMPRESS, -28.0, 3.0, 6.0, 15.0, 200.0, 6.0),
            mb_band(2, RAISE, -45.0, 2.0, 6.0, 20.0, 250.0, 8.0), mb_band(3, COMPRESS, -26.0, 2.0, 4.0, 8.0, 120.0, 6.0),
            mb_band(4, COMPRESS, -34.0, 4.0, 6.0, 1.0, 60.0, 3.0)),
        "Hiss and Background Noise Reduction": multiband(
            (150.0, 1_500.0, 5_000.0), mb_band(1, EXPAND, -55.0, 2.0, 10.0, 2.0, 150.0, 6.0),
            mb_band(2, EXPAND, -55.0, 1.5, 6.0, 2.0, 150.0, 6.0), mb_band(3, EXPAND, -58.0, 2.0, 10.0, 1.0, 120.0, 6.0),
            mb_band(4, EXPAND, -60.0, 3.0, 15.0, 0.5, 100.0, 6.0)),
        # Drums.
        "Kick (Held Sub and Click)": multiband(
            (70.0, 300.0, 3_000.0), mb_band(1, COMPRESS, -18.0, 4.0, 6.0, 20.0, 120.0, 6.0),
            mb_band(2, EXPAND, -40.0, 2.0, 10.0, 1.0, 100.0, 6.0), mb_band(3, BURST, -28.0, 1.5, 4.0, 1.0, 40.0, 4.0),
            mb_band(4, EXPAND, -45.0, 2.0, 12.0, 0.5, 60.0, 6.0)),
        "Snare (Crack Without Spill)": multiband(
            (150.0, 900.0, 5_000.0), mb_band(1, EXPAND, -40.0, 2.0, 12.0, 1.0, 80.0, 6.0),
            mb_band(2, COMPRESS, -22.0, 3.0, 6.0, 10.0, 100.0, 6.0), mb_band(3, BURST, -26.0, 1.5, 4.0, 1.0, 50.0, 4.0),
            mb_band(4, COMPRESS, -30.0, 2.0, 4.0, 2.0, 60.0, 6.0)),
        "Rock Drum Bus (Lively Room)": multiband(
            (100.0, 600.0, 6_000.0), mb_band(1, COMPRESS, -20.0, 3.0, 5.0, 20.0, 100.0, 6.0),
            mb_band(2, RAISE, -38.0, 2.0, 5.0, 10.0, 150.0, 8.0), mb_band(3, BURST, -24.0, 1.4, 4.0, 2.0, 50.0, 4.0),
            mb_band(4, COMPRESS, -28.0, 2.5, 5.0, 1.0, 60.0, 6.0)),
        "Overheads (Tamed Cymbals)": multiband(
            (300.0, 2_000.0, 7_000.0), mb_band(1, EXPAND, -45.0, 2.0, 8.0, 2.0, 120.0, 6.0),
            mb_band(2, COMPRESS, -26.0, 1.5, 2.0, 15.0, 120.0, 8.0), mb_band(3, COMPRESS, -26.0, 3.0, 5.0, 2.0, 80.0, 6.0),
            mb_band(4, COMPRESS, -28.0, 3.0, 6.0, 1.0, 60.0, 6.0)),
        # Bass.
        "DI Bass Even Across the Neck": multiband(
            (80.0, 400.0, 2_000.0), mb_band(1, COMPRESS, -22.0, 4.0, 8.0, 20.0, 150.0, 6.0),
            mb_band(2, COMPRESS, -22.0, 3.0, 6.0, 15.0, 120.0, 6.0), mb_band(3, COMPRESS, -26.0, 2.0, 4.0, 5.0, 80.0, 6.0),
            mb_band(4, EXPAND, -45.0, 2.0, 10.0, 1.0, 60.0, 6.0)),
        "Synth Bass (Constant Sub)": multiband(
            (100.0, 600.0, 3_000.0), mb_band(1, COMPRESS, -16.0, 6.0, 8.0, 5.0, 80.0, 6.0),
            mb_band(2, COMPRESS, -20.0, 3.0, 5.0, 10.0, 100.0, 6.0), mb_band(3, RAISE, -36.0, 2.0, 4.0, 10.0, 120.0, 8.0),
            mb_band(4, COMPRESS, -30.0, 2.0, 4.0, 3.0, 60.0, 6.0)),
        # Instruments.
        "Acoustic Guitar (Body and Attack)": multiband(
            (200.0, 1_000.0, 5_000.0), mb_band(1, COMPRESS, -26.0, 3.0, 6.0, 15.0, 150.0, 6.0),
            mb_band(2, COMPRESS, -24.0, 2.0, 3.0, 20.0, 150.0, 8.0), mb_band(3, BURST, -30.0, 1.3, 3.0, 2.0, 60.0, 4.0),
            mb_band(4, RAISE, -45.0, 1.5, 3.0, 15.0, 150.0, 8.0)),
        "Distorted Guitars (Held Palm Mutes)": multiband(
            (150.0, 700.0, 3_500.0), mb_band(1, COMPRESS, -24.0, 4.0, 8.0, 5.0, 80.0, 6.0),
            mb_band(2, COMPRESS, -24.0, 2.0, 3.0, 15.0, 120.0, 8.0), mb_band(3, COMPRESS, -26.0, 2.0, 4.0, 10.0, 100.0, 6.0),
            mb_band(4, COMPRESS, -30.0, 3.0, 5.0, 2.0, 60.0, 6.0)),
        "Even Piano Without Harshness": multiband(
            (200.0, 1_200.0, 5_000.0), mb_band(1, COMPRESS, -26.0, 2.0, 4.0, 30.0, 250.0, 10.0),
            mb_band(2, COMPRESS, -24.0, 1.6, 3.0, 25.0, 200.0, 10.0), mb_band(3, COMPRESS, -26.0, 2.5, 5.0, 5.0, 100.0, 6.0),
            mb_band(4, RAISE, -46.0, 1.5, 3.0, 20.0, 200.0, 8.0)),
        "Pads and Strings (Calmer Highs)": multiband(
            (200.0, 1_500.0, 6_000.0), mb_band(1, COMPRESS, -26.0, 2.0, 4.0, 50.0, 400.0, 10.0),
            mb_band(2, RAISE, -42.0, 1.5, 3.0, 50.0, 400.0, 10.0), mb_band(3, COMPRESS, -28.0, 2.0, 4.0, 20.0, 200.0, 8.0),
            mb_band(4, COMPRESS, -30.0, 2.5, 5.0, 10.0, 150.0, 6.0)),
        "Brass (Controlled Blare)": multiband(
            (200.0, 1_000.0, 4_000.0), mb_band(1, COMPRESS, -26.0, 2.0, 3.0, 20.0, 150.0, 8.0),
            mb_band(2, COMPRESS, -24.0, 2.0, 3.0, 15.0, 120.0, 8.0), mb_band(3, COMPRESS, -24.0, 3.0, 6.0, 5.0, 80.0, 6.0),
            mb_band(4, COMPRESS, -30.0, 3.0, 5.0, 2.0, 60.0, 6.0)),
    },
    # Pression: depth, time, the amounts up and down (0..1), each band's gain
    # (dB); in full, each band's thresholds, ratios and times.
    "pression": {
        "Full Pressure": {"pression.depth": 1.0},
        "Gentle Pressure": {"pression.depth": 0.35, "pression.time": 0.6},
        "Thick Bass": {"pression.depth": 0.6, "pression.lowGain": 3.0, "pression.highGain": -2.0, "pression.time": 0.65},
        "Upfront Vocal": {"pression.depth": 0.4, "pression.upward": 0.7, "pression.midGain": 2.0, "pression.lowGain": -2.0},
        "Crushed Drums": {"pression.depth": 0.8, "pression.time": 0.3, "pression.highGain": 1.5},
        "Bright Pad": {"pression.depth": 0.5, "pression.downward": 0.5, "pression.highGain": 3.0},
        "Upward Only": {"pression.depth": 0.6, "pression.downward": 0.0},
        "Downward Only": {"pression.depth": 0.7, "pression.upward": 0.0},
        # Voices.
        "Lead Vocal (Subtle Density)": pression(
            0.25, 0.6, 0.6, 0.8, (-1.0, 0.0, 1.0), (150.0, 3_500.0), (-28.0, 4.0, -48.0, 2.0, 30.0, 250.0),
            (-24.0, 4.0, -45.0, 2.5, 15.0, 200.0), (-28.0, 6.0, -48.0, 2.0, 5.0, 120.0)),
        "Rap Vocal (Dense and Upfront)": pression(
            0.4, 0.45, 0.8, 1.0, (-1.5, 1.0, 1.5), (180.0, 4_000.0), (-26.0, 6.0, -46.0, 2.5, 25.0, 200.0),
            (-22.0, 6.0, -44.0, 3.0, 10.0, 150.0), (-26.0, 10.0, -46.0, 2.5, 3.0, 80.0)),
        "Backing Vocals (Compact Block)": pression(
            0.5, 0.7, 1.0, 0.7, (-3.0, 0.0, 1.0), (200.0, 4_000.0), (-30.0, 8.0, -50.0, 3.0, 40.0, 300.0),
            (-26.0, 8.0, -46.0, 3.0, 20.0, 250.0), (-30.0, 10.0, -48.0, 3.0, 10.0, 150.0)),
        "Podcast (Even Voice)": pression(
            0.3, 0.7, 0.8, 0.7, (-1.5, 0.0, 0.5), (120.0, 4_500.0), (-30.0, 6.0, -50.0, 2.0, 40.0, 300.0),
            (-24.0, 4.0, -46.0, 2.5, 20.0, 250.0), (-30.0, 8.0, -50.0, 2.0, 5.0, 120.0)),
        # Drums.
        "Drum Bus (Parallel Energy)": pression(
            0.35, 0.3, 1.0, 1.0, (1.0, 0.0, 1.0), (100.0, 3_000.0), (-30.0, 20.0, -44.0, 3.5, 30.0, 200.0),
            (-28.0, 20.0, -44.0, 3.5, 15.0, 200.0), (-32.0, 20.0, -42.0, 3.5, 8.0, 100.0)),
        # The mids are only held down (their lower threshold at the floor).
        "Kick (Sub and Click)": pression(
            0.5, 0.25, 0.8, 1.0, (1.0, -2.0, 1.5), (90.0, 3_000.0), (-24.0, 8.0, -45.0, 3.0, 30.0, 150.0),
            (-30.0, 20.0, -80.0, 1.0, 15.0, 150.0), (-34.0, 10.0, -40.0, 4.0, 5.0, 60.0)),
        "Snare (Crack and Body)": pression(
            0.45, 0.35, 0.9, 1.0, (-1.0, 1.0, 1.5), (180.0, 4_000.0), (-30.0, 10.0, -50.0, 2.0, 20.0, 150.0),
            (-26.0, 10.0, -44.0, 3.0, 10.0, 150.0), (-30.0, 10.0, -42.0, 3.5, 5.0, 80.0)),
        "Pumped-Up Drum Room": pression(
            0.8, 0.4, 1.0, 1.0, (-2.0, 0.0, -1.0), (120.0, 3_500.0), (-32.0, 30.0, -46.0, 4.0, 30.0, 250.0),
            (-30.0, 30.0, -46.0, 4.0, 15.0, 250.0), (-34.0, 30.0, -44.0, 4.0, 8.0, 150.0)),
        "Lo-Fi Hip-Hop (Squashed Loop)": pression(
            0.5, 0.55, 0.8, 1.0, (1.0, 0.0, -3.0), (110.0, 2_500.0), (-28.0, 10.0, -46.0, 3.0, 40.0, 250.0),
            (-28.0, 10.0, -46.0, 3.0, 20.0, 250.0), (-32.0, 10.0, -48.0, 2.5, 10.0, 150.0)),
        # Bass.
        "Synth Bass (Big and Even)": pression(
            0.6, 0.5, 0.7, 1.0, (1.5, 1.0, -1.0), (120.0, 2_000.0), (-26.0, 20.0, -44.0, 3.0, 40.0, 250.0),
            (-28.0, 20.0, -44.0, 3.5, 20.0, 200.0), (-32.0, 10.0, -46.0, 2.5, 10.0, 120.0)),
        "DI Bass (Grit and Definition)": pression(
            0.35, 0.6, 0.6, 0.9, (0.0, 1.5, 0.0), (100.0, 1_500.0), (-26.0, 8.0, -46.0, 2.0, 40.0, 280.0),
            (-26.0, 8.0, -44.0, 3.0, 20.0, 200.0), (-30.0, 6.0, -48.0, 2.0, 10.0, 120.0)),
        "Bass Music Crushed Hard": pression(
            1.0, 0.35, 1.0, 1.0, (1.0, 0.0, 2.0), (100.0, 2_500.0), (-34.0, 66.7, -42.0, 4.17, 40.0, 250.0),
            (-30.0, 66.7, -42.0, 4.17, 20.0, 250.0), (-36.0, 66.7, -40.0, 4.17, 10.0, 120.0), input_gain=4.0, output=-4.0),
        # Synths and instruments.
        "Synth Lead (Aggressive and Bright)": pression(
            0.7, 0.3, 1.0, 1.0, (-3.0, 0.0, 2.5), (200.0, 3_000.0), (-32.0, 40.0, -42.0, 4.0, 30.0, 200.0),
            (-30.0, 40.0, -42.0, 4.0, 15.0, 200.0), (-34.0, 40.0, -40.0, 4.5, 8.0, 100.0), input_gain=2.0, output=-2.0),
        "Plucks and Arpeggios (Details Forward)": pression(
            0.55, 0.2, 1.0, 0.6, (-1.0, 0.0, 1.5), (150.0, 3_000.0), (-30.0, 10.0, -46.0, 3.0, 20.0, 150.0),
            (-28.0, 10.0, -46.0, 3.5, 10.0, 120.0), (-30.0, 10.0, -44.0, 3.5, 5.0, 80.0)),
        "Acoustic Guitar (Playing Details)": pression(
            0.25, 0.6, 0.8, 0.6, (-1.5, 0.0, 1.0), (180.0, 4_000.0), (-28.0, 6.0, -50.0, 2.0, 30.0, 250.0),
            (-26.0, 5.0, -48.0, 2.5, 15.0, 200.0), (-30.0, 6.0, -48.0, 2.5, 5.0, 120.0)),
        "Electric Guitar (Compact Wall)": pression(
            0.35, 0.5, 0.5, 1.0, (-1.0, 0.5, -1.0), (140.0, 2_800.0), (-26.0, 10.0, -48.0, 2.0, 20.0, 150.0),
            (-24.0, 8.0, -46.0, 2.0, 15.0, 150.0), (-28.0, 10.0, -50.0, 2.0, 5.0, 100.0)),
        "Piano (Sustain and Sparkle)": pression(
            0.3, 0.75, 0.9, 0.5, (0.0, 0.0, 1.0), (200.0, 3_500.0), (-28.0, 4.0, -50.0, 2.5, 40.0, 300.0),
            (-26.0, 4.0, -50.0, 2.5, 25.0, 300.0), (-28.0, 4.0, -50.0, 2.5, 15.0, 200.0)),
        "Brass (Compact Blare)": pression(
            0.35, 0.45, 0.7, 1.0, (-1.0, 0.5, 0.0), (200.0, 3_000.0), (-28.0, 8.0, -48.0, 2.0, 25.0, 200.0),
            (-24.0, 10.0, -46.0, 2.5, 12.0, 150.0), (-26.0, 10.0, -48.0, 2.5, 6.0, 100.0)),
        # Master.
        "Electronic Master (Light Dose)": pression(
            0.15, 0.5, 0.6, 0.8, (0.0, 0.0, 0.0), (88.0, 2_500.0), (-30.0, 20.0, -44.0, 3.0, 40.0, 250.0),
            (-28.0, 20.0, -44.0, 3.0, 20.0, 250.0), (-32.0, 20.0, -42.0, 3.0, 12.0, 130.0)),
    },
    # Velours: the depth (dB), sharpness and selectivity (0..1), the zone (Hz).
    "velours": {
        "Soft Vocal": {"velours.depth": 5.0, "velours.sharpness": 0.4, "velours.focusLow": 300.0, "velours.focusHigh": 10_000.0},
        "Acoustic Guitar": {"velours.depth": 6.0, "velours.focusLow": 150.0, "velours.focusHigh": 6_000.0},
        "Aggressive Bus": {"velours.depth": 4.0, "velours.sharpness": 0.3, "velours.selectivity": 0.3},
        "Sibilance": {"velours.depth": 8.0, "velours.focusLow": 4_000.0, "velours.focusHigh": 12_000.0, "velours.sharpness": 0.7,
                      "velours.selectivity": 0.7, "velours.attack": 1.0},
        "Low Resonances": {"velours.depth": 8.0, "velours.focusLow": 60.0, "velours.focusHigh": 400.0, "velours.release": 150.0},
        "Hard and Precise": {"velours.mode": 1.0, "velours.depth": 10.0, "velours.sharpness": 0.8, "velours.selectivity": 0.8},
        # Voices.
        "Pop Lead Vocal (Harsh Highs)": velours(6.0, 0.5, 0.6, 3.0, 70.0, 1_500.0, 9_000.0, output=0.5),
        "Rap Vocal (Nasal and Sibilant)": velours(7.0, 0.6, 0.55, 2.0, 60.0, 800.0, 10_000.0, output=0.5),
        "Podcast Spoken Voice": velours(5.0, 0.45, 0.5, 5.0, 100.0, 150.0, 8_000.0, mix=0.9, output=0.3),
        "Blended Backing Vocals": velours(8.0, 0.4, 0.4, 10.0, 150.0, 300.0, 12_000.0, output=1.0),
        # Guitars and instruments.
        "Distorted Guitar (Fizz and Honk)": velours(7.0, 0.6, 0.5, 5.0, 80.0, 1_500.0, 8_000.0, output=0.5),
        "Fingerpicked Acoustic (Softened Attacks)": velours(5.0, 0.55, 0.6, 1.0, 50.0, 2_000.0, 10_000.0, output=0.3, link=0.8),
        "Piano (Soundboard Resonances)": velours(5.0, 0.5, 0.7, 10.0, 150.0, 100.0, 5_000.0, output=0.3, link=0.6),
        "Strings (Bow Harshness)": velours(6.0, 0.45, 0.5, 15.0, 200.0, 1_500.0, 7_000.0, output=0.5, link=0.8),
        "Brass (Hard Blare)": velours(6.0, 0.5, 0.5, 5.0, 80.0, 1_000.0, 6_000.0, output=0.5),
        "Synth Lead (Filter Resonance)": velours(8.0, 0.7, 0.8, 1.0, 40.0, 500.0, 12_000.0, hard=True),
        "Synth Pad (Softness)": velours(5.0, 0.35, 0.4, 20.0, 300.0, 300.0, 14_000.0, mix=0.8, output=0.5, link=0.7),
        "Bass (Notes That Jump Out)": velours(6.0, 0.5, 0.6, 10.0, 200.0, 40.0, 400.0, output=0.5),
        # Drums.
        "Snare (Head Ring)": velours(9.0, 0.7, 0.8, 2.0, 120.0, 200.0, 2_000.0, hard=True),
        "Kick (Shell Boom)": velours(6.0, 0.6, 0.7, 5.0, 150.0, 60.0, 500.0),
        "Overheads and Cymbals": velours(7.0, 0.55, 0.45, 1.0, 40.0, 3_000.0, 16_000.0, output=0.5, link=0.8),
        "Drums (Bells and Rings)": velours(6.0, 0.6, 0.7, 2.0, 80.0, 300.0, 6_000.0, hard=True),
        # Rooms, buses, master.
        "Boxy Room (Room Modes)": velours(8.0, 0.6, 0.8, 15.0, 250.0, 80.0, 600.0, output=0.5),
        "Mix Bus (Gentle Finish)": velours(3.0, 0.4, 0.5, 10.0, 150.0, 200.0, 12_000.0, mix=0.7),
        "Master (2 to 5 kHz Harshness)": velours(3.0, 0.45, 0.6, 5.0, 120.0, 2_000.0, 5_000.0, mix=0.8),
    },
    # Sonomètre: the target (see meter()), the history (s), the true peak allowed.
    "sonometre": {
        "Streaming -14 LUFS": meter(0, 120.0, -1.0),
        "Streaming -16 LUFS": meter(1, 120.0, -1.0),
        "Radio and TV EBU R128": meter(4, 300.0, -1.0),
        "CD and Club": meter(6, 120.0, -0.3),
        "Streaming -15 LUFS": meter(2, 120.0, -1.0),
        "Podcast -16 LUFS": meter(3, 300.0, -1.0),
        "US Television -24 LUFS": meter(5, 300.0, -2.0),
        "Games and Web (-24 LUFS)": meter(5, 120.0, -1.0),
        "Film and Documentary (Long Programme)": meter(4, 600.0, -1.0),
        "Cautious Streaming (Peaks at -2 dB)": meter(0, 120.0, -2.0),
        "Cautious Spoken Voice (Peaks at -3 dB)": meter(3, 300.0, -3.0),
        "Mixing in Progress (Short History)": meter(0, 15.0, -1.0),
        "Whole Album (Long History)": meter(0, 600.0, -1.0),
    },
}


def eq_band(number: int, shape: int, hz: float, gain: float = 0.0, q: float | None = None, slope: int | None = None,
            dynamic: tuple | None = None) -> dict:
    """An Égaliseur Pro band: its shape (0 bell, 1 low shelf, 2 high shelf,
    3 low cut, 4 high cut, 5 notch, 6 band-pass, 7 tilt), frequency, gain,
    Q, a cut's slope (0 12 … 3 48 dB/oct); `dynamic`: (range, threshold,
    attack, release[, 1 to hear the sidechain])."""
    band = "eqpro.b%d" % number
    values = {band + "Enabled": 1.0, band + "Type": float(shape), band + "Frequency": float(hz), band + "Gain": float(gain),
              band + "Q": float(q if q is not None else (0.71 if shape in (1, 2, 3, 4, 7) else 1.0))}
    if slope is not None:
        values[band + "Slope"] = float(slope)
    if dynamic:
        reach, threshold, attack, release = dynamic[:4]
        values.update({band + "Dynamic": 1.0, band + "Range": float(reach), band + "Threshold": float(threshold),
                       band + "Attack": float(attack), band + "Release": float(release),
                       band + "Detector": float(dynamic[4] if len(dynamic) > 4 else 0.0)})
    return values


def eq_preset(*bands: dict, output: float | None = None) -> dict:
    values: dict = {}
    for band in bands:
        values.update(band)
    if output is not None:
        values["eqpro.output"] = float(output)
    return values


BELL, LOW_SHELF, HIGH_SHELF, LOW_CUT, HIGH_CUT, NOTCH, BAND_PASS, TILT = range(8)
SIDECHAIN = 1.0


def hum_notches(mains: float) -> dict:
    """A low cut under the mains' hum and notches on it and its harmonics."""
    bands = [eq_band(1, LOW_CUT, mains * 0.5, slope=1)]
    bands += [eq_band(n, NOTCH, mains * (n - 1), q=12.0) for n in range(2, 9)]
    return eq_preset(*bands)


PRESETS["eq-pro"] = {
    "Clean Low Cut": eq_preset(eq_band(1, 3, 80.0, slope=1)),
    "Present Vocal": eq_preset(eq_band(1, 3, 90.0, slope=1), eq_band(2, 0, 250.0, -2.0, 1.2), eq_band(3, 0, 3_200.0, 2.0, 1.0),
                               eq_band(4, 2, 10_000.0, 2.0)),
    "Softened Sibilance (De-esser)": eq_preset(eq_band(1, 3, 80.0, slope=1),
                                               eq_band(2, 0, 6_500.0, 0.0, 2.0, dynamic=(-8.0, -32.0, 1.0, 60.0))),
    "Vocal Without Mud (Dynamic)": eq_preset(eq_band(1, 3, 90.0, slope=1),
                                             eq_band(2, 0, 300.0, 0.0, 1.2, dynamic=(-5.0, -28.0, 10.0, 150.0))),
    "Bass Making Room for the Kick (Sidechain)": eq_preset(eq_band(1, 3, 30.0, slope=2),
                                                           eq_band(2, 0, 60.0, 0.0, 1.0, dynamic=(-6.0, -30.0, 2.0, 120.0, 1.0))),
    "Kick Click Forward": eq_preset(eq_band(1, 1, 60.0, 2.0), eq_band(2, 0, 350.0, -3.0, 1.5),
                                    eq_band(3, 0, 3_500.0, 0.0, 1.4, dynamic=(4.0, -30.0, 0.5, 80.0))),
    "Acoustic Guitar Without Resonances": eq_preset(eq_band(1, 3, 70.0, slope=1),
                                                    eq_band(2, 0, 200.0, 0.0, 1.5, dynamic=(-4.0, -30.0, 10.0, 150.0)),
                                                    eq_band(3, 0, 2_500.0, 0.0, 3.0, dynamic=(-3.0, -34.0, 5.0, 100.0))),
    "Softened Highs (Dynamic)": eq_preset(eq_band(1, 2, 8_000.0, 0.0, dynamic=(-4.0, -32.0, 5.0, 120.0))),
    "Master with Controlled Lows": eq_preset(eq_band(1, 3, 25.0, slope=1),
                                             eq_band(2, 1, 120.0, 0.0, dynamic=(-3.0, -18.0, 20.0, 200.0))),
    "Gentle Sheen": eq_preset(eq_band(1, 2, 12_000.0, 2.5), eq_band(2, 0, 3_000.0, -1.0, 0.8)),
    # Voices.
    "Pop Lead Vocal (Air and De-esser)": eq_preset(
        eq_band(1, LOW_CUT, 85.0, slope=1), eq_band(2, BELL, 220.0, -1.0, 1.0, dynamic=(-4.0, -30.0, 15.0, 180.0)),
        eq_band(3, BELL, 800.0, -1.0, 1.5), eq_band(4, BELL, 3_000.0, 2.0, 0.9),
        eq_band(5, BELL, 7_000.0, 0.0, 3.0, dynamic=(-7.0, -34.0, 1.0, 70.0)), eq_band(6, HIGH_SHELF, 12_000.0, 3.0),
        eq_band(7, HIGH_CUT, 19_000.0, slope=0), output=-1.0),
    "Rap Vocal (Presence and Sibilance)": eq_preset(
        eq_band(1, LOW_CUT, 100.0, slope=2), eq_band(2, BELL, 180.0, 1.5, 1.0), eq_band(3, BELL, 450.0, -2.5, 1.4),
        eq_band(4, BELL, 2_500.0, 2.5, 1.0), eq_band(5, BELL, 5_500.0, 0.0, 2.5, dynamic=(-6.0, -30.0, 1.0, 60.0)),
        eq_band(6, BELL, 8_000.0, 0.0, 3.0, dynamic=(-6.0, -32.0, 1.0, 50.0)), eq_band(7, HIGH_SHELF, 11_000.0, 2.0),
        output=-1.0),
    # The presence dips while the lead vocal (the sidechain) sings.
    "Backing Vocals Behind the Lead (Sidechain)": eq_preset(
        eq_band(1, LOW_CUT, 150.0, slope=1), eq_band(2, LOW_SHELF, 300.0, -2.0), eq_band(3, BELL, 1_500.0, -1.5, 0.8),
        eq_band(4, BELL, 3_200.0, 0.0, 1.2, dynamic=(-4.0, -30.0, 5.0, 120.0, SIDECHAIN)),
        eq_band(5, BELL, 6_500.0, 0.0, 2.5, dynamic=(-6.0, -34.0, 1.0, 60.0)), eq_band(6, HIGH_SHELF, 10_000.0, 1.5),
        eq_band(7, HIGH_CUT, 16_000.0, slope=0)),
    "Clear Podcast Voice": eq_preset(
        eq_band(1, LOW_CUT, 80.0, slope=2), eq_band(2, LOW_SHELF, 150.0, 0.0, dynamic=(-4.0, -26.0, 20.0, 200.0)),
        eq_band(3, BELL, 350.0, -2.5, 1.2), eq_band(4, BELL, 2_800.0, 2.0, 0.9),
        eq_band(5, BELL, 6_000.0, 0.0, 2.5, dynamic=(-6.0, -32.0, 2.0, 80.0)), eq_band(6, HIGH_SHELF, 10_000.0, 1.5),
        eq_band(7, HIGH_CUT, 16_000.0, slope=1)),
    "Telephone Voice": eq_preset(
        eq_band(1, LOW_CUT, 400.0, slope=3), eq_band(2, BELL, 1_000.0, 3.0, 1.0), eq_band(3, BELL, 2_200.0, 4.0, 1.4),
        eq_band(4, HIGH_CUT, 3_400.0, slope=3), output=-2.0),
    # Drums.
    "Electronic Kick (Weight and Click)": eq_preset(
        eq_band(1, LOW_CUT, 28.0, slope=3), eq_band(2, BELL, 55.0, 3.0, 1.4),
        eq_band(3, BELL, 110.0, 0.0, 1.2, dynamic=(-3.0, -20.0, 5.0, 120.0)), eq_band(4, BELL, 280.0, -4.0, 1.8),
        eq_band(5, BELL, 4_000.0, 3.0, 1.2), eq_band(6, HIGH_CUT, 12_000.0, slope=1), output=-1.5),
    # The snare's ring is held down only while it rings.
    "Snare (Body and Crack)": eq_preset(
        eq_band(1, LOW_CUT, 90.0, slope=1), eq_band(2, BELL, 200.0, 2.5, 1.2), eq_band(3, BELL, 450.0, -2.0, 2.0),
        eq_band(4, BELL, 900.0, 0.0, 6.0, dynamic=(-5.0, -28.0, 5.0, 150.0)), eq_band(5, BELL, 5_000.0, 2.5, 1.0),
        eq_band(6, HIGH_SHELF, 10_000.0, 1.5), output=-1.0),
    "Drum Bus (Punch and Air)": eq_preset(
        eq_band(1, LOW_CUT, 30.0, slope=1), eq_band(2, LOW_SHELF, 70.0, 1.5),
        eq_band(3, BELL, 350.0, -2.0, 1.0, dynamic=(-3.0, -24.0, 10.0, 150.0)), eq_band(4, BELL, 3_500.0, 1.5, 0.8),
        eq_band(5, BELL, 8_000.0, 0.0, 2.0, dynamic=(-3.0, -30.0, 2.0, 90.0)), eq_band(6, HIGH_SHELF, 12_000.0, 2.0),
        output=-1.0),
    "Softened Overheads and Cymbals": eq_preset(
        eq_band(1, LOW_CUT, 300.0, slope=1), eq_band(2, BELL, 800.0, -1.0, 1.0),
        eq_band(3, BELL, 3_500.0, 0.0, 2.0, dynamic=(-4.0, -30.0, 2.0, 80.0)),
        eq_band(4, BELL, 7_000.0, 0.0, 2.5, dynamic=(-4.0, -32.0, 1.0, 60.0)), eq_band(5, HIGH_SHELF, 12_000.0, 2.0)),
    # Bass.
    "DI Bass (Definition and Held Lows)": eq_preset(
        eq_band(1, LOW_CUT, 35.0, slope=2), eq_band(2, LOW_SHELF, 80.0, 2.0, dynamic=(-3.0, -18.0, 20.0, 200.0)),
        eq_band(3, BELL, 250.0, -2.5, 1.2), eq_band(4, BELL, 800.0, 2.0, 1.5), eq_band(5, BELL, 2_000.0, 2.0, 1.2),
        eq_band(6, HIGH_CUT, 6_000.0, slope=1), output=-1.0),
    "Round Controlled Synth Bass": eq_preset(
        eq_band(1, LOW_CUT, 30.0, slope=3), eq_band(2, BELL, 45.0, 0.0, 1.4, dynamic=(-4.0, -20.0, 10.0, 150.0)),
        eq_band(3, BELL, 100.0, 1.5, 1.0), eq_band(4, BELL, 300.0, -2.0, 1.5), eq_band(5, BELL, 1_200.0, 2.0, 1.0),
        eq_band(6, HIGH_CUT, 8_000.0, slope=1)),
    # Guitars, keys, strings.
    "Strummed Acoustic Guitar (Bright)": eq_preset(
        eq_band(1, LOW_CUT, 90.0, slope=1), eq_band(2, BELL, 180.0, 0.0, 1.2, dynamic=(-4.0, -28.0, 15.0, 180.0)),
        eq_band(3, BELL, 400.0, -2.0, 1.0), eq_band(4, BELL, 3_000.0, 1.5, 0.8),
        eq_band(5, BELL, 6_000.0, 0.0, 4.0, dynamic=(-4.0, -34.0, 1.0, 60.0)), eq_band(6, HIGH_SHELF, 9_000.0, 3.0),
        eq_band(7, HIGH_CUT, 18_000.0, slope=0), output=-1.0),
    "Distorted Guitar Without Mud": eq_preset(
        eq_band(1, LOW_CUT, 90.0, slope=2), eq_band(2, BELL, 120.0, 0.0, 1.5, dynamic=(-4.0, -24.0, 10.0, 120.0)),
        eq_band(3, BELL, 400.0, -3.0, 1.4), eq_band(4, BELL, 1_500.0, 1.5, 1.0),
        eq_band(5, BELL, 3_500.0, 0.0, 3.0, dynamic=(-4.0, -30.0, 3.0, 80.0)), eq_band(6, HIGH_CUT, 9_000.0, slope=2)),
    "Clear Piano Without Mud": eq_preset(
        eq_band(1, LOW_CUT, 40.0, slope=0), eq_band(2, BELL, 250.0, -2.0, 1.0, dynamic=(-3.0, -26.0, 15.0, 200.0)),
        eq_band(3, BELL, 600.0, -1.0, 1.5), eq_band(4, BELL, 3_000.0, 1.5, 0.8), eq_band(5, HIGH_SHELF, 9_000.0, 2.0),
        eq_band(6, HIGH_CUT, 19_000.0, slope=0)),
    "Silky Strings": eq_preset(
        eq_band(1, LOW_CUT, 60.0, slope=1), eq_band(2, BELL, 300.0, -1.5, 1.0),
        eq_band(3, BELL, 3_000.0, 0.0, 2.0, dynamic=(-4.0, -30.0, 10.0, 150.0)), eq_band(4, HIGH_SHELF, 10_000.0, 2.5),
        eq_band(5, HIGH_CUT, 18_000.0, slope=0)),
    "Bright Brass": eq_preset(
        eq_band(1, LOW_CUT, 80.0, slope=1), eq_band(2, BELL, 300.0, -2.0, 1.0), eq_band(3, BELL, 1_000.0, 1.0, 1.0),
        eq_band(4, BELL, 2_800.0, 0.0, 2.0, dynamic=(-4.0, -28.0, 5.0, 100.0)), eq_band(5, HIGH_SHELF, 8_000.0, 2.0),
        eq_band(6, HIGH_CUT, 16_000.0, slope=0)),
    "Cutting Synth Lead": eq_preset(
        eq_band(1, LOW_CUT, 150.0, slope=1), eq_band(2, BELL, 400.0, -2.0, 1.0), eq_band(3, BELL, 2_000.0, 3.0, 0.8),
        eq_band(4, BELL, 4_500.0, 0.0, 3.0, dynamic=(-4.0, -28.0, 2.0, 60.0)), eq_band(5, HIGH_SHELF, 10_000.0, 2.0),
        output=-1.5),
    # The pad's middle dips while the vocal (the sidechain) sings.
    "Pad Making Room for the Vocal (Sidechain)": eq_preset(
        eq_band(1, LOW_CUT, 150.0, slope=1), eq_band(2, BELL, 400.0, -1.5, 0.8),
        eq_band(3, BELL, 2_500.0, 0.0, 0.7, dynamic=(-6.0, -30.0, 10.0, 200.0, SIDECHAIN)),
        eq_band(4, HIGH_SHELF, 8_000.0, 1.0), eq_band(5, HIGH_CUT, 15_000.0, slope=0)),
    # Master and repair.
    "Gentle Master (Tilt and Held Lows)": eq_preset(
        eq_band(1, LOW_CUT, 20.0, slope=2), eq_band(2, LOW_SHELF, 60.0, 0.0, dynamic=(-2.0, -16.0, 30.0, 250.0)),
        eq_band(3, BELL, 250.0, -0.7, 0.8), eq_band(4, BELL, 3_000.0, 0.0, 1.5, dynamic=(-2.0, -28.0, 10.0, 150.0)),
        eq_band(5, TILT, 1_000.0, 0.8), eq_band(6, HIGH_SHELF, 16_000.0, 1.5), output=-0.5),
    "Mains Hum Removal 50 Hz": hum_notches(50.0),
    "Mains Hum Removal 60 Hz": hum_notches(60.0),
}
