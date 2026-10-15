"""Reverbs, echoes, the filter and the colour effects.

Values by binding suffix; what is not given keeps its default.
"""
from __future__ import annotations


def values(prefix: str, **settings: float) -> dict[str, float]:
    """A preset's values: each setting's binding suffix under the effect's prefix."""
    return {f"{prefix}.{name}": float(value) for name, value in settings.items()}


# Note values of the sync settings (0 is free): 1 1/32, 2 1/16T, 3 1/16,
# 4 1/8T, 5 1/16., 6 1/8, 7 1/4T, 8 1/8., 9 1/4, 10 1/4., 11 1/2, 12 1/1.
SYNC_16, SYNC_16_DOTTED, SYNC_8, SYNC_4_TRIPLET, SYNC_8_DOTTED, SYNC_4, SYNC_2, SYNC_BAR = 3, 5, 6, 7, 8, 9, 11, 12

# Filtre Pro: the filter types, the LFO shapes, the modulation sources and
# destinations (ProEffectsDesign.h).
LOW_PASS, HIGH_PASS, BAND_PASS, NOTCH, PEAK, COMB = range(6)
SINE, TRIANGLE, SAW_UP, SAW_DOWN, SQUARE, RANDOM_HELD, RANDOM_SMOOTH = range(7)
LFO1, LFO2, FOLLOWER = 1, 2, 3
(CUTOFF1, RESONANCE1, CUTOFF2, RESONANCE2, CUTOFF3, RESONANCE3, CUTOFF4, RESONANCE4, ALL_CUTOFFS, DRIVE1, DRIVE2, MIX,
 OUTPUT) = range(1, 14)
SERIAL, PARALLEL, PAIRS = range(3)


def unit(number: int, kind: int, slope: int, cutoff: float, resonance: float, drive: float = 0.0, spread: float = 0.0) -> dict:
    """One of the four filters, on (slope: 0 12 dB, 1 24 dB, 2 48 dB per octave)."""
    return {f"filtpro.f{number}On": 1.0, f"filtpro.f{number}Type": float(kind), f"filtpro.f{number}Slope": float(slope),
            f"filtpro.f{number}Cutoff": float(cutoff), f"filtpro.f{number}Resonance": float(resonance),
            f"filtpro.f{number}Drive": float(drive), f"filtpro.f{number}Spread": float(spread)}


def lfo(number: int, rate: float, shape: int, sync: int = 0, stereo: float = 0.0) -> dict:
    return {f"filtpro.lfo{number}Rate": float(rate), f"filtpro.lfo{number}Shape": float(shape),
            f"filtpro.lfo{number}Sync": float(sync), f"filtpro.lfo{number}Stereo": float(stereo)}


def follow(attack: float, release: float, gain: float) -> dict:
    return {"filtpro.followAttack": float(attack), "filtpro.followRelease": float(release), "filtpro.followGain": float(gain)}


def route(slot: int, source: int, destination: int, amount: float) -> dict:
    return {f"filtpro.m{slot}Source": float(source), f"filtpro.m{slot}Destination": float(destination),
            f"filtpro.m{slot}Amount": float(amount)}


def filter_preset(*parts: dict, routing: int = SERIAL, mix: float = 1.0, output: float = 0.0) -> dict:
    """A Filtre Pro preset: the filters not given are off, the modulation
    slots not given empty."""
    preset: dict[str, float] = {}
    for part in parts:
        preset.update(part)
    for number in range(1, 5):
        preset.setdefault(f"filtpro.f{number}On", 0.0)
    for slot in range(1, 5):
        if f"filtpro.m{slot}Source" not in preset:
            preset.update(route(slot, 0, 0, 0.0))
    preset.update({"filtpro.routing": float(routing), "filtpro.mix": float(mix), "filtpro.output": float(output)})
    return preset


PRESETS = {
    # Réverbe Pro: decay in seconds, the decays per band as multiples of it,
    # the early reflections' level in dB, the ducking in dB.
    "reverb-pro": {
        "Small Room": {"revpro.space": 0.2, "revpro.decay": 0.6, "revpro.predelay": 4.0, "revpro.distance": 0.2,
                       "revpro.brightness": 0.1, "revpro.character": 0.25, "revpro.mix": 0.2, "revpro.lowDecay": 1.1,
                       "revpro.highDecay": 0.6, "revpro.early": 2.0},
        "Wooden Studio": {"revpro.space": 0.35, "revpro.decay": 1.1, "revpro.predelay": 10.0, "revpro.distance": 0.3,
                          "revpro.brightness": -0.2, "revpro.character": 0.3, "revpro.mix": 0.22, "revpro.lowDecay": 1.2,
                          "revpro.highDecay": 0.45, "revpro.highCut": 9_000.0},
        "Bright Plate": {"revpro.space": 0.45, "revpro.decay": 1.8, "revpro.predelay": 18.0, "revpro.distance": 0.55,
                         "revpro.brightness": 0.45, "revpro.character": 0.6, "revpro.diffusion": 1.0, "revpro.early": -24.0,
                         "revpro.mix": 0.25, "revpro.lowDecay": 0.8, "revpro.highDecay": 0.9, "revpro.lowCut": 200.0},
        "Large Hall": {"revpro.space": 0.8, "revpro.decay": 2.8, "revpro.predelay": 24.0, "revpro.distance": 0.45,
                       "revpro.character": 0.4, "revpro.mix": 0.28, "revpro.lowDecay": 1.35, "revpro.highDecay": 0.5,
                       "revpro.width": 1.2, "revpro.lowCut": 40.0, "revpro.highCut": 14_000.0, "revpro.diffusion": 0.8},
        "Cathedral": {"revpro.space": 1.0, "revpro.decay": 7.5, "revpro.predelay": 40.0, "revpro.distance": 0.7,
                      "revpro.brightness": -0.15, "revpro.character": 0.5, "revpro.mix": 0.32, "revpro.lowDecay": 1.4,
                      "revpro.highDecay": 0.4, "revpro.width": 1.3},
        "Vocal Up Front (Ducking)": {"revpro.space": 0.5, "revpro.decay": 2.0, "revpro.sync": 6.0, "revpro.ducking": 10.0,
                                     "revpro.duckRelease": 350.0, "revpro.mix": 0.3, "revpro.lowCut": 250.0,
                                     "revpro.highCut": 11_000.0},
        "Immense Pad": {"revpro.space": 1.0, "revpro.decay": 15.0, "revpro.character": 0.8, "revpro.width": 1.5,
                        "revpro.mix": 0.45, "revpro.lowCut": 150.0, "revpro.distance": 0.8, "revpro.highDecay": 0.7,
                        "revpro.diffusion": 1.0, "revpro.early": -18.0},
        # Voices.
        "Lead Vocal Plate": values("revpro", space=0.45, decay=1.6, predelay=35.0, sync=0, distance=0.3, brightness=0.25,
                                   character=0.35, width=1.1, mix=0.22, lowDecay=0.7, lowFrequency=250.0, midDecay=1.0,
                                   midFrequency=1_500.0, highDecay=0.75, highFrequency=6_000.0, lowCut=220.0, highCut=12_000.0,
                                   ducking=4.0, duckRelease=300.0, diffusion=0.95, early=-12.0, output=0.0),
        "Ballad Vocal Hall": values("revpro", space=0.7, decay=2.8, predelay=45.0, sync=0, distance=0.45, brightness=0.15,
                                    character=0.4, width=1.3, mix=0.25, lowDecay=0.8, lowFrequency=250.0, midDecay=1.0,
                                    midFrequency=1_500.0, highDecay=0.7, highFrequency=6_000.0, lowCut=200.0, highCut=12_000.0,
                                    ducking=4.0, duckRelease=500.0, diffusion=0.9, early=-6.0, output=0.0),
        "Pop Vocal Tempo Hall (Ducked)": values("revpro", space=0.6, decay=2.0, sync=SYNC_16_DOTTED, distance=0.4,
                                                brightness=0.2, character=0.35, width=1.3, mix=0.3, lowDecay=0.75,
                                                lowFrequency=280.0, midDecay=1.0, midFrequency=1_800.0, highDecay=0.8,
                                                highFrequency=7_000.0, lowCut=250.0, highCut=13_000.0, ducking=12.0,
                                                duckRelease=600.0, diffusion=0.9, early=-9.0, output=0.0),
        "Trap Vocal Tight Plate": values("revpro", space=0.35, decay=0.9, sync=SYNC_16, distance=0.25, brightness=0.35,
                                         character=0.25, width=1.3, mix=0.18, lowDecay=0.5, lowFrequency=300.0, midDecay=1.0,
                                         midFrequency=2_000.0, highDecay=0.8, highFrequency=7_000.0, lowCut=300.0,
                                         highCut=13_000.0, ducking=8.0, duckRelease=200.0, diffusion=0.95, early=-10.0,
                                         output=0.0),
        "Gospel Choir Church": values("revpro", space=0.95, decay=4.0, predelay=30.0, sync=0, distance=0.6, brightness=0.0,
                                      character=0.4, width=1.4, mix=0.3, lowDecay=1.1, lowFrequency=200.0, midDecay=1.05,
                                      midFrequency=1_000.0, highDecay=0.55, highFrequency=5_000.0, lowCut=100.0,
                                      highCut=12_000.0, ducking=0.0, diffusion=0.9, early=-4.0, output=0.0),
        # Drums and percussion.
        "Snare Room Slam": values("revpro", space=0.3, decay=0.9, predelay=5.0, sync=0, distance=0.15, brightness=0.3,
                                  character=0.2, width=1.3, mix=0.3, lowDecay=0.6, lowFrequency=300.0, midDecay=1.1,
                                  midFrequency=2_000.0, highDecay=0.6, highFrequency=7_000.0, lowCut=180.0, highCut=14_000.0,
                                  ducking=0.0, diffusion=0.85, early=2.0, output=0.0),
        "Drum Bus Ambience": values("revpro", space=0.25, decay=0.6, predelay=2.0, sync=0, distance=0.1, brightness=0.0,
                                    character=0.15, width=1.4, mix=0.15, lowDecay=0.5, lowFrequency=250.0, midDecay=1.0,
                                    midFrequency=1_200.0, highDecay=0.5, highFrequency=5_000.0, lowCut=250.0, highCut=10_000.0,
                                    ducking=0.0, diffusion=0.7, early=3.0, output=0.0),
        "Dark Kick Room": values("revpro", space=0.2, decay=0.45, predelay=0.0, sync=0, distance=0.2, brightness=-0.5,
                                 character=0.1, width=0.8, mix=0.15, lowDecay=1.4, lowFrequency=150.0, midDecay=0.9,
                                 midFrequency=800.0, highDecay=0.3, highFrequency=3_000.0, lowCut=40.0, highCut=6_000.0,
                                 ducking=0.0, diffusion=0.6, early=0.0, output=0.0),
        "Hand Percussion Live Room": values("revpro", space=0.4, decay=0.8, predelay=6.0, sync=0, distance=0.25,
                                            brightness=0.2, character=0.2, width=1.5, mix=0.2, lowDecay=0.8,
                                            lowFrequency=250.0, midDecay=1.0, midFrequency=1_500.0, highDecay=0.7,
                                            highFrequency=6_000.0, lowCut=150.0, highCut=15_000.0, ducking=0.0,
                                            diffusion=0.75, early=1.0, output=0.0),
        # Instruments.
        "Piano Concert Hall": values("revpro", space=0.75, decay=2.4, predelay=22.0, sync=0, distance=0.5, brightness=-0.05,
                                     character=0.3, width=1.2, mix=0.24, lowDecay=1.25, lowFrequency=200.0, midDecay=1.0,
                                     midFrequency=1_200.0, highDecay=0.6, highFrequency=5_000.0, lowCut=60.0, highCut=13_000.0,
                                     ducking=0.0, diffusion=0.8, early=-4.0, output=0.0),
        "Acoustic Guitar Chamber": values("revpro", space=0.4, decay=1.3, predelay=14.0, sync=0, distance=0.35,
                                          brightness=0.1, character=0.3, width=1.2, mix=0.2, lowDecay=0.8, lowFrequency=220.0,
                                          midDecay=1.05, midFrequency=1_500.0, highDecay=0.65, highFrequency=5_500.0,
                                          lowCut=120.0, highCut=12_000.0, ducking=0.0, diffusion=0.8, early=-2.0, output=0.0),
        "Strings Scoring Stage": values("revpro", space=0.85, decay=2.2, predelay=28.0, sync=0, distance=0.55,
                                        brightness=-0.1, character=0.35, width=1.4, mix=0.3, lowDecay=1.2, lowFrequency=220.0,
                                        midDecay=1.0, midFrequency=1_500.0, highDecay=0.55, highFrequency=5_000.0, lowCut=50.0,
                                        highCut=14_000.0, ducking=0.0, diffusion=0.85, early=-3.0, output=0.0),
        "Clean-Low Bass Ambience": values("revpro", space=0.3, decay=0.8, predelay=8.0, sync=0, distance=0.3, brightness=0.1,
                                          character=0.2, width=1.0, mix=0.15, lowDecay=0.3, lowFrequency=300.0, midDecay=1.0,
                                          midFrequency=1_200.0, highDecay=0.6, highFrequency=5_000.0, lowCut=400.0,
                                          highCut=8_000.0, ducking=0.0, diffusion=0.7, early=-6.0, output=0.0),
        "Synth Lead Wide Space": values("revpro", space=0.6, decay=1.8, sync=SYNC_8, distance=0.5, brightness=0.2,
                                        character=0.6, width=1.8, mix=0.25, lowDecay=0.6, lowFrequency=300.0, midDecay=1.0,
                                        midFrequency=1_500.0, highDecay=0.8, highFrequency=7_000.0, lowCut=300.0,
                                        highCut=12_000.0, ducking=8.0, duckRelease=400.0, diffusion=0.9, early=-10.0,
                                        output=0.0),
        # Genres and aesthetics.
        "Shoegaze Wash": values("revpro", space=1.0, decay=9.0, predelay=10.0, sync=0, distance=0.75, brightness=0.2,
                                character=0.85, width=1.7, mix=0.5, lowDecay=0.8, lowFrequency=200.0, midDecay=1.2,
                                midFrequency=1_500.0, highDecay=0.9, highFrequency=6_000.0, lowCut=200.0, highCut=11_000.0,
                                ducking=0.0, diffusion=1.0, early=-18.0, output=0.0),
        "Ambient Guitar Cloud": values("revpro", space=0.9, decay=7.0, sync=SYNC_8_DOTTED, distance=0.7, brightness=0.15,
                                       character=0.75, width=1.6, mix=0.42, lowDecay=0.7, lowFrequency=250.0, midDecay=1.1,
                                       midFrequency=1_500.0, highDecay=0.85, highFrequency=6_000.0, lowCut=200.0,
                                       highCut=11_000.0, ducking=5.0, duckRelease=900.0, diffusion=1.0, early=-15.0,
                                       output=0.0),
        "Endless Ambient Drone": values("revpro", space=1.0, decay=20.0, predelay=80.0, sync=0, distance=0.9, brightness=0.1,
                                        character=1.0, width=1.8, mix=0.55, lowDecay=0.6, lowFrequency=250.0, midDecay=1.0,
                                        midFrequency=1_500.0, highDecay=1.0, highFrequency=8_000.0, lowCut=250.0,
                                        highCut=10_000.0, ducking=0.0, diffusion=1.0, early=-24.0, output=0.0),
        "Cinematic Trailer Hall": values("revpro", space=1.0, decay=6.5, predelay=60.0, sync=0, distance=0.65,
                                         brightness=-0.25, character=0.45, width=1.5, mix=0.38, lowDecay=1.6,
                                         lowFrequency=180.0, midDecay=1.1, midFrequency=900.0, highDecay=0.45,
                                         highFrequency=4_000.0, lowCut=40.0, highCut=9_000.0, ducking=0.0, diffusion=0.9,
                                         early=-8.0, output=0.0),
        "Lo-Fi Dark Room": values("revpro", space=0.35, decay=1.2, predelay=12.0, sync=0, distance=0.6, brightness=-0.6,
                                  character=0.6, width=0.9, mix=0.3, lowDecay=1.0, lowFrequency=250.0, midDecay=1.0,
                                  midFrequency=1_000.0, highDecay=0.25, highFrequency=2_500.0, lowCut=150.0, highCut=5_000.0,
                                  ducking=0.0, diffusion=0.7, early=-6.0, output=0.0),
        "Muffled Next Room": values("revpro", space=0.4, decay=1.5, predelay=25.0, sync=0, distance=1.0, brightness=-0.8,
                                    character=0.3, width=0.7, mix=0.6, lowDecay=1.3, lowFrequency=200.0, midDecay=0.9,
                                    midFrequency=800.0, highDecay=0.2, highFrequency=2_000.0, lowCut=80.0, highCut=2_500.0,
                                    ducking=0.0, diffusion=0.9, early=-24.0, output=-2.0),
        # For a send (return track): only the reverb comes out.
        "Plate Send Fully Wet": values("revpro", space=0.5, decay=2.0, predelay=20.0, sync=0, distance=0.4, brightness=0.3,
                                       character=0.4, width=1.2, mix=1.0, lowDecay=0.7, lowFrequency=250.0, midDecay=1.0,
                                       midFrequency=1_500.0, highDecay=0.8, highFrequency=6_500.0, lowCut=250.0,
                                       highCut=12_000.0, ducking=0.0, diffusion=1.0, early=-24.0, output=0.0),
        "Hall Send Fully Wet": values("revpro", space=0.8, decay=2.6, predelay=25.0, sync=0, distance=0.5, brightness=0.0,
                                      character=0.4, width=1.3, mix=1.0, lowDecay=1.0, lowFrequency=220.0, midDecay=1.0,
                                      midFrequency=1_200.0, highDecay=0.55, highFrequency=5_000.0, lowCut=150.0,
                                      highCut=11_000.0, ducking=0.0, diffusion=0.85, early=-6.0, output=0.0),
    },
    # Halo: the algorithm (0 concert hall, 1 bright hall, 2 plate, 3 room,
    # 4 chamber, 5 random space, 6 chorus space, 7 ambience, 8 sanctuary,
    # 9 nonlinear), the colour (0 1970s, 1 1980s, 2 now), decay (s), size.
    "halo": {
        "Concert Hall": {"halo.mode": 0.0, "halo.decay": 3.2, "halo.size": 0.75, "halo.predelay": 25.0, "halo.mix": 0.3},
        "Bright 80s Hall": {"halo.mode": 1.0, "halo.colour": 1.0, "halo.decay": 2.6, "halo.size": 0.65,
                            "halo.damping": 12_000.0, "halo.mix": 0.3},
        "Vocal Plate": {"halo.mode": 2.0, "halo.decay": 1.8, "halo.predelay": 40.0, "halo.lowCut": 200.0, "halo.mix": 0.25},
        "Vintage Plate": {"halo.mode": 2.0, "halo.colour": 0.0, "halo.decay": 2.4, "halo.predelay": 20.0, "halo.mix": 0.3},
        "Small Room": {"halo.mode": 3.0, "halo.decay": 0.7, "halo.size": 0.4, "halo.mix": 0.25, "halo.predelay": 5.0,
                       "halo.lowCut": 100.0, "halo.damping": 9_000.0, "halo.earlyDiffusion": 0.6, "halo.width": 0.9},
        "Echo Chamber": {"halo.mode": 4.0, "halo.decay": 1.6, "halo.size": 0.55, "halo.mix": 0.3, "halo.predelay": 15.0,
                         "halo.lowCut": 120.0, "halo.damping": 6_500.0, "halo.bassMultiplier": 1.0},
        "Drifting Space": {"halo.mode": 5.0, "halo.decay": 6.0, "halo.size": 0.85, "halo.modDepth": 0.8, "halo.mix": 0.35},
        "Chorus Pad": {"halo.mode": 6.0, "halo.decay": 4.5, "halo.size": 0.8, "halo.modDepth": 0.9, "halo.modRate": 0.4,
                       "halo.mix": 0.4},
        "Subtle Air": {"halo.mode": 7.0, "halo.decay": 0.5, "halo.size": 0.35, "halo.mix": 0.2, "halo.predelay": 3.0,
                       "halo.lowCut": 150.0, "halo.damping": 10_000.0},
        "Sanctuary": {"halo.mode": 8.0, "halo.decay": 12.0, "halo.size": 1.0, "halo.predelay": 60.0, "halo.mix": 0.4,
                      "halo.bassMultiplier": 1.5},
        "80s Gated Snare": {"halo.mode": 9.0, "halo.colour": 1.0, "halo.decay": 0.45, "halo.size": 0.5, "halo.mix": 0.35},
        "Dotted Eighth Hall": {"halo.mode": 0.0, "halo.decay": 2.0, "halo.sync": 8.0, "halo.mix": 0.3},
        "Endless Space": {"halo.mode": 5.0, "halo.decay": 20.0, "halo.size": 1.0, "halo.modDepth": 0.6, "halo.mix": 0.5,
                          "halo.damping": 4_000.0},
        # Voices.
        "Seventies Vocal Chamber": values("halo", mode=4, colour=0, decay=1.4, predelay=30.0, sync=0, size=0.5,
                                          highCut=8_000.0, lowCut=180.0, modDepth=0.3, modRate=0.5, attack=0.4,
                                          earlyDiffusion=0.6, lateDiffusion=0.7, bassMultiplier=0.9, bassCrossover=250.0,
                                          damping=6_000.0, width=0.8, mix=0.25),
        "Eighties Ballad Plate": values("halo", mode=2, colour=1, decay=2.6, sync=SYNC_8, size=0.6, highCut=13_000.0,
                                        lowCut=200.0, modDepth=0.35, modRate=0.7, attack=0.2, earlyDiffusion=0.8,
                                        lateDiffusion=0.8, bassMultiplier=0.8, bassCrossover=300.0, damping=10_000.0, width=1.0,
                                        mix=0.28),
        "Trap Vocal Ambience": values("halo", mode=7, colour=2, decay=0.6, sync=SYNC_16, size=0.4, highCut=14_000.0,
                                      lowCut=300.0, modDepth=0.2, modRate=0.6, attack=0.2, earlyDiffusion=0.8,
                                      lateDiffusion=0.7, bassMultiplier=0.6, bassCrossover=400.0, damping=12_000.0, width=1.0,
                                      mix=0.2),
        "Choir Cathedral": values("halo", mode=8, colour=2, decay=6.0, predelay=40.0, sync=0, size=1.0, highCut=10_000.0,
                                  lowCut=90.0, modDepth=0.4, modRate=0.3, attack=0.7, earlyDiffusion=0.8, lateDiffusion=0.9,
                                  bassMultiplier=1.3, bassCrossover=250.0, damping=6_000.0, width=1.0, mix=0.33),
        "Dialogue Room": values("halo", mode=3, colour=2, decay=0.5, predelay=4.0, sync=0, size=0.35, highCut=9_000.0,
                                lowCut=150.0, modDepth=0.1, modRate=0.5, attack=0.2, earlyDiffusion=0.6, lateDiffusion=0.6,
                                bassMultiplier=0.8, bassCrossover=300.0, damping=8_000.0, width=0.3, mix=0.15),
        # Drums.
        "Snare Plate Crack": values("halo", mode=2, colour=2, decay=1.3, predelay=10.0, sync=0, size=0.5, highCut=16_000.0,
                                    lowCut=250.0, modDepth=0.2, modRate=0.8, attack=0.0, earlyDiffusion=0.9,
                                    lateDiffusion=0.85, bassMultiplier=0.7, bassCrossover=350.0, damping=14_000.0, width=1.0,
                                    mix=0.28),
        "Synthwave Gated Toms": values("halo", mode=9, colour=1, decay=0.8, predelay=5.0, sync=0, size=0.7,
                                       highCut=12_000.0, lowCut=80.0, modDepth=0.1, modRate=0.5, attack=0.1,
                                       earlyDiffusion=0.9, lateDiffusion=0.8, bassMultiplier=1.4, bassCrossover=250.0,
                                       damping=9_000.0, width=1.0, mix=0.4),
        # Instruments.
        "Piano Bright Hall": values("halo", mode=1, colour=2, decay=2.3, predelay=18.0, sync=0, size=0.7, highCut=15_000.0,
                                    lowCut=70.0, modDepth=0.25, modRate=0.5, attack=0.5, earlyDiffusion=0.7,
                                    lateDiffusion=0.75, bassMultiplier=1.1, bassCrossover=250.0, damping=11_000.0, width=1.0,
                                    mix=0.25),
        "Strings Concert Hall": values("halo", mode=0, colour=2, decay=3.2, predelay=30.0, sync=0, size=0.85,
                                       highCut=12_000.0, lowCut=60.0, modDepth=0.35, modRate=0.4, attack=0.6,
                                       earlyDiffusion=0.75, lateDiffusion=0.8, bassMultiplier=1.25, bassCrossover=250.0,
                                       damping=7_500.0, width=1.0, mix=0.3),
        "Acoustic Guitar Room": values("halo", mode=3, colour=2, decay=0.9, predelay=8.0, sync=0, size=0.45,
                                       highCut=12_000.0, lowCut=120.0, modDepth=0.2, modRate=0.6, attack=0.3,
                                       earlyDiffusion=0.6, lateDiffusion=0.7, bassMultiplier=0.9, bassCrossover=300.0,
                                       damping=8_000.0, width=0.9, mix=0.22),
        "Synth Lead Eighties Hall": values("halo", mode=1, colour=1, decay=2.0, sync=SYNC_8_DOTTED, size=0.65,
                                           highCut=12_000.0, lowCut=250.0, modDepth=0.45, modRate=0.7, attack=0.4,
                                           earlyDiffusion=0.75, lateDiffusion=0.75, bassMultiplier=0.7, bassCrossover=300.0,
                                           damping=9_000.0, width=1.0, mix=0.25),
        # Genres and aesthetics.
        "Lo-Fi Hip-Hop Room": values("halo", mode=4, colour=0, decay=1.1, predelay=15.0, sync=0, size=0.45, highCut=5_000.0,
                                     lowCut=200.0, modDepth=0.6, modRate=0.8, attack=0.4, earlyDiffusion=0.6,
                                     lateDiffusion=0.6, bassMultiplier=0.8, bassCrossover=300.0, damping=4_000.0, width=0.7,
                                     mix=0.3),
        "Drifting Dub Space": values("halo", mode=5, colour=0, decay=4.0, sync=SYNC_8, size=0.8, highCut=6_000.0,
                                     lowCut=250.0, modDepth=0.8, modRate=0.4, attack=0.5, earlyDiffusion=0.7,
                                     lateDiffusion=0.8, bassMultiplier=0.7, bassCrossover=300.0, damping=4_500.0, width=1.0,
                                     mix=0.35),
        "Shoegaze Chorus Wall": values("halo", mode=6, colour=2, decay=8.0, predelay=20.0, sync=0, size=0.95,
                                       highCut=10_000.0, lowCut=200.0, modDepth=1.0, modRate=0.35, attack=0.7,
                                       earlyDiffusion=0.9, lateDiffusion=0.9, bassMultiplier=0.8, bassCrossover=300.0,
                                       damping=8_000.0, width=1.0, mix=0.5),
        "Ambient Sanctuary Drone": values("halo", mode=8, colour=2, decay=18.0, predelay=120.0, sync=0, size=1.0,
                                          highCut=9_000.0, lowCut=150.0, modDepth=0.7, modRate=0.2, attack=1.0,
                                          earlyDiffusion=0.9, lateDiffusion=1.0, bassMultiplier=1.0, bassCrossover=250.0,
                                          damping=5_000.0, width=1.0, mix=0.55),
        "Cinematic Dark Hall": values("halo", mode=0, colour=0, decay=7.0, predelay=50.0, sync=0, size=1.0, highCut=7_000.0,
                                      lowCut=40.0, modDepth=0.5, modRate=0.3, attack=0.8, earlyDiffusion=0.8,
                                      lateDiffusion=0.9, bassMultiplier=1.6, bassCrossover=220.0, damping=4_500.0, width=1.0,
                                      mix=0.38),
    },
    # Écho Bande: the mode (0 head 1, 1 head 2, 2 head 3, 3 heads 2 and 3,
    # 4 head 1 and the spring, 5 head 2 and spring, 6 head 3 and spring,
    # 7 heads 1 2 and spring, 8 heads 2 3 and spring, 9 heads 1 3 and spring,
    # 10 all three and spring, 11 the spring alone), the time (ms, head 1;
    # heads 2 and 3 play about two and three times later) or the sync, the
    # intensity (past 1 it runs away), bass and treble (dB, in the loop).
    "echo-bande": {
        "Slapback": {"echoband.mode": 0.0, "echoband.time": 90.0, "echoband.intensity": 0.1, "echoband.reverb": 0.0,
                     "echoband.mix": 0.45, "echoband.wow": 0.15},
        "Dub Echo": {"echoband.mode": 3.0, "echoband.time": 300.0, "echoband.intensity": 0.75, "echoband.bass": -3.0,
                     "echoband.treble": -4.0, "echoband.wow": 0.4, "echoband.age": 0.5},
        "Surf Spring": {"echoband.mode": 11.0, "echoband.reverb": 0.85, "echoband.mix": 0.5},
        "Three Heads": {"echoband.mode": 10.0, "echoband.time": 140.0, "echoband.intensity": 0.5, "echoband.reverb": 0.25},
        "Runaway": {"echoband.mode": 10.0, "echoband.time": 80.0, "echoband.intensity": 1.05, "echoband.mix": 0.5},
        "Dotted Quarter": {"echoband.mode": 4.0, "echoband.sync": 10.0, "echoband.intensity": 0.4, "echoband.reverb": 0.2},
        "Eighth Notes": {"echoband.mode": 0.0, "echoband.sync": 6.0, "echoband.intensity": 0.35, "echoband.reverb": 0.0},
        "Worn Tape": {"echoband.mode": 4.0, "echoband.time": 200.0, "echoband.age": 0.9, "echoband.wow": 0.7,
                      "echoband.intensity": 0.5},
        # Dub and reggae.
        "Dub Echoes Taking Off": values("echoband", mode=3, time=300.0, sync=SYNC_8, intensity=0.8, echo=0.8, reverb=0.0,
                                        bass=-2.0, treble=-5.0, wow=0.45, mix=0.45, drive=6.0, age=0.6, width=0.8),
        "Dub Spring and Heads": values("echoband", mode=8, time=280.0, sync=0, intensity=0.75, echo=0.75, reverb=0.45,
                                       bass=-3.0, treble=-4.0, wow=0.4, mix=0.45, drive=4.0, age=0.55, width=0.9),
        "Self-Oscillating Dub Siren": values("echoband", mode=1, time=200.0, sync=0, intensity=1.1, echo=0.6, reverb=0.0,
                                             bass=-4.0, treble=3.0, wow=0.5, mix=0.5, drive=8.0, age=0.4, width=0.5),
        "Reggae Skank Echo": values("echoband", mode=5, time=120.0, sync=SYNC_16, intensity=0.55, echo=0.65, reverb=0.3,
                                    bass=-6.0, treble=-2.0, wow=0.25, mix=0.4, drive=3.0, age=0.45, width=0.5),
        # Voices.
        "Rockabilly Vocal Slap": values("echoband", mode=0, time=110.0, sync=0, intensity=0.08, echo=0.65, reverb=0.0,
                                        bass=-4.0, treble=-2.0, wow=0.1, mix=0.4, drive=3.0, age=0.3, width=0.5),
        "Vocal Throw Quarter Notes": values("echoband", mode=0, time=250.0, sync=SYNC_4, intensity=0.55, echo=0.7,
                                            reverb=0.0, bass=-6.0, treble=-3.0, wow=0.15, mix=0.3, drive=0.0, age=0.35,
                                            width=0.5),
        "Trap Vocal Dotted Throw": values("echoband", mode=1, time=150.0, sync=SYNC_16_DOTTED, intensity=0.5, echo=0.7,
                                          reverb=0.0, bass=-8.0, treble=2.0, wow=0.05, mix=0.3, drive=0.0, age=0.1, width=0.5),
        "Vintage Vocal Spring Wash": values("echoband", mode=6, time=220.0, sync=0, intensity=0.4, echo=0.5, reverb=0.55,
                                            bass=-4.0, treble=-2.0, wow=0.3, mix=0.35, drive=2.0, age=0.4, width=0.5),
        # Guitars, keys, bass, drums.
        "Blues Guitar Driven Slap": values("echoband", mode=0, time=140.0, sync=0, intensity=0.25, echo=0.75, reverb=0.0,
                                           bass=-2.0, treble=-3.0, wow=0.25, mix=0.45, drive=14.0, age=0.6, width=0.5),
        "Surf Guitar Echo and Spring": values("echoband", mode=4, time=160.0, sync=0, intensity=0.3, echo=0.55, reverb=0.75,
                                              bass=-2.0, treble=2.0, wow=0.15, mix=0.5, drive=2.0, age=0.2, width=0.5),
        "Piano Soft Tape Echo": values("echoband", mode=4, time=320.0, sync=0, intensity=0.3, echo=0.45, reverb=0.3,
                                       bass=-4.0, treble=-5.0, wow=0.2, mix=0.25, drive=0.0, age=0.5, width=0.6),
        "Bass Tape Doubler": values("echoband", mode=0, time=45.0, sync=0, intensity=0.0, echo=0.5, reverb=0.0, bass=-12.0,
                                    treble=0.0, wow=0.1, mix=0.3, drive=6.0, age=0.2, width=0.5),
        "Snare Spring Splash": values("echoband", mode=11, time=120.0, sync=0, intensity=0.0, echo=0.0, reverb=0.8, bass=0.0,
                                      treble=0.0, wow=0.0, mix=0.35, drive=0.0, age=0.0, width=0.5),
        "Synthwave Lead Dotted Echo": values("echoband", mode=4, time=250.0, sync=SYNC_8_DOTTED, intensity=0.5, echo=0.65,
                                             reverb=0.25, bass=-3.0, treble=-1.0, wow=0.2, mix=0.35, drive=2.0, age=0.15,
                                             width=0.5),
        # Aesthetics.
        "Lo-Fi Hip-Hop Tape Delay": values("echoband", mode=1, time=200.0, sync=SYNC_8, intensity=0.45, echo=0.6, reverb=0.0,
                                           bass=-5.0, treble=-7.0, wow=0.6, mix=0.35, drive=6.0, age=0.85, width=0.3),
        "Wide Two-Head Stereo Echo": values("echoband", mode=3, time=200.0, sync=SYNC_8, intensity=0.4, echo=0.6,
                                            reverb=0.0, bass=-3.0, treble=0.0, wow=0.05, mix=0.3, drive=0.0, age=0.0,
                                            width=1.0),
        "Ambient Three-Head Cascade": values("echoband", mode=10, time=250.0, sync=SYNC_4, intensity=0.6, echo=0.6,
                                             reverb=0.4, bass=-4.0, treble=-4.0, wow=0.35, mix=0.4, drive=0.0, age=0.3,
                                             width=1.0),
        "Psychedelic Wobbly Tape": values("echoband", mode=7, time=240.0, sync=0, intensity=0.65, echo=0.7, reverb=0.35,
                                          bass=0.0, treble=-3.0, wow=0.9, mix=0.45, drive=6.0, age=0.7, width=0.8),
        "Long Tape Drift": values("echoband", mode=2, time=340.0, sync=0, intensity=0.75, echo=0.65, reverb=0.0, bass=-4.0,
                                  treble=-6.0, wow=0.55, mix=0.4, drive=3.0, age=0.6, width=0.5),
    },
    # Filtre Pro: four filters (types and LFO shapes named above), routed in
    # series, in parallel or in pairs; a full modulation moves a cutoff five
    # octaves, a drive 36 dB, the output 24 dB.
    "filter-pro": {
        "Gentle Low-Pass": {"filtpro.f1Type": 0.0, "filtpro.f1Slope": 1.0, "filtpro.f1Cutoff": 1_500.0,
                            "filtpro.f1Resonance": 0.15},
        "Auto-Wah": {"filtpro.f1Type": 2.0, "filtpro.f1Slope": 0.0, "filtpro.f1Cutoff": 500.0, "filtpro.f1Resonance": 0.55,
                     "filtpro.m1Source": 3.0, "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.45,
                     "filtpro.followAttack": 5.0, "filtpro.followRelease": 120.0, "filtpro.followGain": 6.0},
        "Slow Sweep": {"filtpro.f1Type": 0.0, "filtpro.f1Cutoff": 600.0, "filtpro.f1Resonance": 0.45, "filtpro.m1Source": 1.0,
                       "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.35, "filtpro.lfo1Shape": 1.0, "filtpro.lfo1Sync": 12.0},
        "Eighth-Note Wobble": {"filtpro.f1Type": 0.0, "filtpro.f1Cutoff": 400.0, "filtpro.f1Resonance": 0.6,
                               "filtpro.f1Drive": 6.0, "filtpro.m1Source": 1.0, "filtpro.m1Destination": 1.0,
                               "filtpro.m1Amount": 0.5, "filtpro.lfo1Sync": 6.0},
        "Telephone": {"filtpro.f1Type": 1.0, "filtpro.f1Cutoff": 400.0, "filtpro.f1Drive": 6.0, "filtpro.f2On": 1.0,
                      "filtpro.f2Type": 0.0, "filtpro.f2Cutoff": 3_200.0},
        "Vowels": {"filtpro.f1Type": 4.0, "filtpro.f1Cutoff": 800.0, "filtpro.f1Resonance": 0.6, "filtpro.f2On": 1.0,
                   "filtpro.f2Type": 4.0, "filtpro.f2Cutoff": 1_200.0, "filtpro.f2Resonance": 0.6, "filtpro.f3On": 1.0,
                   "filtpro.f3Type": 4.0, "filtpro.f3Cutoff": 2_500.0, "filtpro.f3Resonance": 0.5, "filtpro.m1Source": 1.0,
                   "filtpro.m1Destination": 9.0, "filtpro.m1Amount": 0.12, "filtpro.lfo1Rate": 0.3, "filtpro.output": -6.0},
        "Metallic Comb": {"filtpro.f1Type": 5.0, "filtpro.f1Cutoff": 220.0, "filtpro.f1Resonance": 0.85, "filtpro.mix": 0.6},
        "Stereo Swirl": {"filtpro.f1Type": 0.0, "filtpro.f1Cutoff": 1_200.0, "filtpro.f1Resonance": 0.4,
                         "filtpro.f1Spread": 1.0, "filtpro.m1Source": 1.0, "filtpro.m1Destination": 1.0,
                         "filtpro.m1Amount": 0.3, "filtpro.lfo1Rate": 0.5, "filtpro.lfo1Stereo": 0.5},
        "Random Chops": {"filtpro.f1Type": 2.0, "filtpro.f1Cutoff": 1_500.0, "filtpro.f1Resonance": 0.5,
                         "filtpro.m1Source": 1.0, "filtpro.m1Destination": 1.0, "filtpro.m1Amount": 0.5,
                         "filtpro.lfo1Shape": 5.0, "filtpro.lfo1Sync": 3.0},
        # Bass.
        "Wobble Bass Triplet Growl": filter_preset(
            unit(1, LOW_PASS, 1, 280.0, 0.62, drive=9.0), unit(2, HIGH_PASS, 0, 35.0, 0.1),
            lfo(1, 2.0, SINE, SYNC_4_TRIPLET), lfo(2, 0.25, TRIANGLE, SYNC_BAR),
            route(1, LFO1, CUTOFF1, 0.45), route(2, LFO1, RESONANCE1, 0.1), route(3, LFO2, DRIVE1, 0.08), output=-2.0),
        "Acid Bass Squelch": filter_preset(
            unit(1, LOW_PASS, 1, 420.0, 0.7, drive=14.0), unit(2, HIGH_PASS, 0, 30.0, 0.1),
            follow(1.0, 150.0, 3.0), lfo(1, 4.0, SAW_DOWN, SYNC_16), lfo(2, 0.25, TRIANGLE, SYNC_BAR),
            route(1, FOLLOWER, CUTOFF1, 0.3), route(2, LFO1, CUTOFF1, 0.12), route(3, LFO2, RESONANCE1, 0.06), output=-4.0),
        "Talking Bass Formants": filter_preset(
            unit(1, BAND_PASS, 0, 700.0, 0.7), unit(2, BAND_PASS, 0, 1_200.0, 0.7), unit(3, BAND_PASS, 0, 2_600.0, 0.6),
            unit(4, LOW_PASS, 1, 300.0, 0.2), lfo(1, 2.0, TRIANGLE, SYNC_4), lfo(2, 4.0, RANDOM_HELD, SYNC_8),
            route(1, LFO1, CUTOFF1, 0.12), route(2, LFO1, CUTOFF2, 0.18), route(3, LFO2, CUTOFF3, 0.1),
            routing=PARALLEL, output=8.0),
        # Guitars and keys.
        "Funk Guitar Envelope Filter": filter_preset(
            unit(1, LOW_PASS, 1, 220.0, 0.65, drive=3.0), follow(3.0, 90.0, 6.0),
            route(1, FOLLOWER, CUTOFF1, 0.6), route(2, FOLLOWER, RESONANCE1, -0.1), output=2.0),
        "Electric Guitar Band-Limit": filter_preset(
            unit(1, HIGH_PASS, 1, 90.0, 0.15), unit(2, LOW_PASS, 1, 9_000.0, 0.15)),
        "Shoegaze Swirling Peaks": filter_preset(
            unit(1, PEAK, 0, 800.0, 0.15, spread=0.3), unit(2, PEAK, 0, 2_400.0, 0.15, spread=-0.3),
            lfo(1, 0.3, SINE, stereo=0.5), lfo(2, 0.11, TRIANGLE, stereo=0.5),
            route(1, LFO1, CUTOFF1, 0.25), route(2, LFO2, CUTOFF2, 0.2), mix=0.7, output=-6.0),
        "Gritty Synth Lead Drive": filter_preset(
            unit(1, LOW_PASS, 1, 3_500.0, 0.45, drive=18.0), unit(2, HIGH_PASS, 0, 120.0, 0.1),
            lfo(1, 5.5, SINE, stereo=0.1), follow(5.0, 200.0, 0.0),
            route(1, LFO1, CUTOFF1, 0.05), route(2, FOLLOWER, DRIVE1, 0.1), output=-6.0),
        # Pads and textures.
        "Synthwave Pad Slow Sweep": filter_preset(
            unit(1, LOW_PASS, 1, 900.0, 0.35, drive=3.0, spread=0.3), lfo(1, 0.1, TRIANGLE), lfo(2, 0.07, SINE, stereo=0.25),
            route(1, LFO1, CUTOFF1, 0.3), route(2, LFO2, CUTOFF1, 0.06)),
        "Ambient Pad Drifting Filter": filter_preset(
            unit(1, LOW_PASS, 0, 1_200.0, 0.3, spread=0.4), unit(2, HIGH_PASS, 0, 120.0, 0.1),
            lfo(1, 0.12, RANDOM_SMOOTH, stereo=0.5), lfo(2, 0.05, SINE),
            route(1, LFO1, CUTOFF1, 0.25), route(2, LFO2, RESONANCE1, 0.1)),
        "Stereo Ping-Pong Filter": filter_preset(
            unit(1, LOW_PASS, 1, 1_500.0, 0.35), lfo(1, 2.0, SINE, SYNC_4, stereo=0.5), route(1, LFO1, CUTOFF1, 0.35)),
        "Notch Phaser Swirl": filter_preset(
            unit(1, NOTCH, 0, 400.0, 0.3, spread=0.5), unit(2, NOTCH, 0, 1_600.0, 0.3, spread=-0.5),
            lfo(1, 0.25, SINE, stereo=0.25), route(1, LFO1, ALL_CUTOFFS, 0.25), mix=0.6),
        # Drums.
        "Comb Flanger Drums": filter_preset(
            unit(1, COMB, 1, 600.0, 0.7, spread=0.2), unit(2, HIGH_PASS, 0, 60.0, 0.1),
            lfo(1, 0.25, TRIANGLE, SYNC_BAR, stereo=0.25), route(1, LFO1, CUTOFF1, 0.3), mix=0.5),
        "Trap Hat Random Band-Pass": filter_preset(
            unit(1, BAND_PASS, 0, 6_000.0, 0.4), unit(2, HIGH_PASS, 1, 2_000.0, 0.1), lfo(1, 4.0, RANDOM_HELD, SYNC_16),
            route(1, LFO1, CUTOFF1, 0.15), mix=0.7, output=2.0),
        "Drum Loop Lows and Highs": filter_preset(
            unit(1, LOW_PASS, 1, 180.0, 0.3), unit(3, HIGH_PASS, 1, 5_000.0, 0.2), lfo(1, 0.5, TRIANGLE, SYNC_2),
            route(1, LFO1, CUTOFF1, 0.15), route(2, LFO1, CUTOFF3, -0.15), routing=PAIRS, output=4.0),
        "Rhythmic Low-Pass Chop": filter_preset(
            unit(1, LOW_PASS, 2, 260.0, 0.25), lfo(1, 4.0, SQUARE, SYNC_16), route(1, LFO1, MIX, 0.5), mix=0.5),
        # Voices and effects.
        "Megaphone Vocal": filter_preset(
            unit(1, HIGH_PASS, 1, 600.0, 0.3), unit(2, LOW_PASS, 1, 2_800.0, 0.35, drive=14.0), unit(3, PEAK, 0, 1_500.0, 0.15),
            output=-8.0),
        "Old Radio Tuning Drift": filter_preset(
            unit(1, BAND_PASS, 1, 1_500.0, 0.35, drive=10.0), unit(2, HIGH_PASS, 0, 300.0, 0.1),
            lfo(1, 0.08, RANDOM_SMOOTH), lfo(2, 5.5, SINE), route(1, LFO1, CUTOFF1, 0.1), route(2, LFO2, OUTPUT, 0.02),
            output=4.0),
        "EDM Bar-Long High-Pass Rise": filter_preset(
            unit(1, HIGH_PASS, 1, 400.0, 0.45), lfo(1, 0.5, SAW_UP, SYNC_BAR),
            route(1, LFO1, CUTOFF1, 0.5), route(2, LFO1, RESONANCE1, 0.15)),
        "Lo-Fi Hip-Hop Muffle": filter_preset(
            unit(1, LOW_PASS, 0, 3_200.0, 0.15, drive=4.0), unit(2, HIGH_PASS, 0, 120.0, 0.1), lfo(1, 0.3, RANDOM_SMOOTH),
            route(1, LFO1, ALL_CUTOFFS, 0.04)),
        "Dub Send Filter Drift": filter_preset(
            unit(1, HIGH_PASS, 0, 300.0, 0.3), unit(2, LOW_PASS, 1, 2_500.0, 0.4), lfo(1, 0.1, SINE, stereo=0.25),
            route(1, LFO1, ALL_CUTOFFS, 0.2)),
    },
    # Patine: the modules' amounts (0..1), the noise (0 record, 1 tape, 2 mains)
    # and distortion (0 valve, 1 tape, 2 transistor) kinds.
    "patine": {
        "Old Record": {"patine.noise": 0.6, "patine.noiseType": 0.0, "patine.wobble": 0.3, "patine.distort": 0.15,
                       "patine.space": 0.1, "patine.magnetic": 0.1, "patine.tone": -0.3},
        "Worn Cassette": {"patine.noise": 0.4, "patine.noiseType": 1.0, "patine.wobble": 0.6, "patine.wobbleRate": 0.6,
                          "patine.magnetic": 0.6, "patine.distort": 0.1, "patine.space": 0.0, "patine.tone": -0.2},
        "AM Radio": {"patine.noise": 0.3, "patine.noiseType": 1.0, "patine.distort": 0.4, "patine.distortType": 2.0,
                     "patine.digital": 0.2, "patine.wobble": 0.1, "patine.space": 0.0, "patine.tone": -0.5, "patine.width": 0.0},
        "Lo-Fi Hip-Hop": {"patine.noise": 0.5, "patine.noiseType": 0.0, "patine.wobble": 0.35, "patine.digital": 0.25,
                          "patine.space": 0.2, "patine.magnetic": 0.3, "patine.tone": -0.25},
        "Warm Tape": {"patine.noise": 0.15, "patine.noiseType": 1.0, "patine.distort": 0.3, "patine.distortType": 1.0,
                      "patine.magnetic": 0.4, "patine.wobble": 0.15, "patine.space": 0.0},
        "12-Bit Sampler": {"patine.noise": 0.0, "patine.wobble": 0.0, "patine.distort": 0.1, "patine.digital": 0.6,
                           "patine.space": 0.0, "patine.magnetic": 0.0},
        "Mains Hum": {"patine.noise": 0.5, "patine.noiseType": 2.0, "patine.wobble": 0.0, "patine.distort": 0.1,
                      "patine.space": 0.0, "patine.magnetic": 0.0},
        "Just a Touch": {"patine.magnitude": 0.4},
        # Keys, pads, synths.
        "Lo-Fi Hip-Hop Dusty Keys": values("patine", noise=0.45, noiseType=0, noiseFollow=0, wobble=0.3, wobbleRate=0.2,
                                           distort=0.15, distortType=0, digital=0.3, space=0.15, spaceSize=0.35,
                                           magnetic=0.35, magnitude=1.0, mix=1.0, tone=-0.35, width=0.8, output=0.0),
        "Video Tape Pad": values("patine", noise=0.35, noiseType=1, noiseFollow=0, wobble=0.55, wobbleRate=0.15, distort=0.1,
                                 distortType=1, digital=0.0, space=0.1, spaceSize=0.5, magnetic=0.65, magnitude=1.0, mix=1.0,
                                 tone=-0.3, width=0.9, output=0.0),
        "Chillwave Dreamy Synth": values("patine", noise=0.2, noiseType=1, noiseFollow=1, wobble=0.5, wobbleRate=0.3,
                                         distort=0.1, distortType=1, digital=0.1, space=0.3, spaceSize=0.6, magnetic=0.4,
                                         magnitude=1.0, mix=0.85, tone=-0.15, width=1.3, output=0.0),
        "Eight-Bit Toy Keys": values("patine", noise=0.1, noiseType=1, noiseFollow=1, wobble=0.1, wobbleRate=0.8,
                                     distort=0.1, distortType=2, digital=0.65, space=0.1, spaceSize=0.2, magnetic=0.0,
                                     magnitude=1.0, mix=1.0, tone=0.2, width=0.7, output=-1.0),
        "Trap Melody Sample Grit": values("patine", noise=0.25, noiseType=0, noiseFollow=1, wobble=0.2, wobbleRate=0.25,
                                          distort=0.15, distortType=1, digital=0.4, space=0.1, spaceSize=0.4, magnetic=0.3,
                                          magnitude=1.0, mix=1.0, tone=-0.25, width=1.1, output=0.0),
        "Faded Memory Ambient": values("patine", noise=0.4, noiseType=1, noiseFollow=0, wobble=0.7, wobbleRate=0.1,
                                       distort=0.1, distortType=0, digital=0.2, space=0.5, spaceSize=0.9, magnetic=0.6,
                                       magnitude=0.8, mix=1.0, tone=-0.45, width=1.2, output=0.0),
        # Voices.
        "Old Phonograph Vocal": values("patine", noise=0.7, noiseType=0, noiseFollow=0, wobble=0.25, wobbleRate=0.35,
                                       distort=0.3, distortType=0, digital=0.0, space=0.05, spaceSize=0.2, magnetic=0.3,
                                       magnitude=1.0, mix=1.0, tone=-0.6, width=0.0, output=1.0),
        "Answering Machine Tape": values("patine", noise=0.4, noiseType=1, noiseFollow=0, wobble=0.5, wobbleRate=0.7,
                                         distort=0.45, distortType=2, digital=0.3, space=0.0, spaceSize=0.3, magnetic=0.7,
                                         magnitude=1.0, mix=1.0, tone=0.4, width=0.0, output=-2.0),
        "Shortwave Radio Voice": values("patine", noise=0.55, noiseType=1, noiseFollow=0, wobble=0.3, wobbleRate=0.9,
                                        distort=0.5, distortType=2, digital=0.35, space=0.0, spaceSize=0.3, magnetic=0.5,
                                        magnitude=1.0, mix=1.0, tone=0.6, width=0.0, output=-2.0),
        "Vocal Vintage Parallel Warmth": values("patine", noise=0.15, noiseType=1, noiseFollow=1, wobble=0.1,
                                                wobbleRate=0.3, distort=0.3, distortType=0, digital=0.0, space=0.1,
                                                spaceSize=0.3, magnetic=0.25, magnitude=1.0, mix=0.45, tone=0.15, width=1.0,
                                                output=0.0),
        # Guitars and drums.
        "Cassette Demo Guitar": values("patine", noise=0.3, noiseType=1, noiseFollow=0, wobble=0.35, wobbleRate=0.45,
                                       distort=0.2, distortType=1, digital=0.0, space=0.05, spaceSize=0.3, magnetic=0.5,
                                       magnitude=1.0, mix=1.0, tone=-0.15, width=0.9, output=0.0),
        "Humming Valve Amp": values("patine", noise=0.35, noiseType=2, noiseFollow=0, wobble=0.0, wobbleRate=0.3,
                                    distort=0.4, distortType=0, digital=0.0, space=0.15, spaceSize=0.25, magnetic=0.0,
                                    magnitude=1.0, mix=1.0, tone=0.1, width=1.0, output=-1.0),
        "Dusty Drum Break": values("patine", noise=0.4, noiseType=0, noiseFollow=0, wobble=0.15, wobbleRate=0.5,
                                   distort=0.35, distortType=1, digital=0.45, space=0.2, spaceSize=0.25, magnetic=0.3,
                                   magnitude=1.0, mix=1.0, tone=-0.2, width=0.8, output=0.0),
        "Crushed Drum Bus Parallel": values("patine", noise=0.0, noiseType=1, noiseFollow=0, wobble=0.0, wobbleRate=0.3,
                                            distort=0.35, distortType=2, digital=0.75, space=0.0, spaceSize=0.3,
                                            magnetic=0.0, magnitude=1.0, mix=0.6, tone=0.1, width=1.0, output=-1.0),
        # Whole mixes and scenes.
        "Mix Bus Analog Touch": values("patine", noise=0.08, noiseType=1, noiseFollow=1, wobble=0.06, wobbleRate=0.3,
                                       distort=0.15, distortType=1, digital=0.0, space=0.0, spaceSize=0.4, magnetic=0.15,
                                       magnitude=1.0, mix=0.6, tone=0.0, width=1.0, output=0.0),
        "Warped Vinyl Wobble": values("patine", noise=0.5, noiseType=0, noiseFollow=0, wobble=0.8, wobbleRate=0.05,
                                      distort=0.05, distortType=0, digital=0.0, space=0.1, spaceSize=0.3, magnetic=0.15,
                                      magnitude=1.0, mix=1.0, tone=-0.2, width=1.0, output=0.0),
        "Distant Old Ballroom": values("patine", noise=0.55, noiseType=0, noiseFollow=0, wobble=0.3, wobbleRate=0.2,
                                       distort=0.2, distortType=0, digital=0.0, space=0.6, spaceSize=0.8, magnetic=0.3,
                                       magnitude=1.0, mix=1.0, tone=-0.5, width=0.6, output=0.0),
        "Old Film Soundtrack": values("patine", noise=0.5, noiseType=0, noiseFollow=0, wobble=0.4, wobbleRate=0.6,
                                      distort=0.25, distortType=0, digital=0.0, space=0.2, spaceSize=0.45, magnetic=0.4,
                                      magnitude=1.0, mix=1.0, tone=-0.4, width=0.0, output=1.0),
    },
    # Fournaise: the character (0 tape, 1 console, 2 class A, 3 valve,
    # 4 pentode), the drive 0..10 (punish adds 20 dB), the tone -1..1, the
    # thump (a low bump at the low cut), steep (a 24 dB high cut), the
    # oversampling (0 1x, 1 2x, 2 4x), the automatic level.
    "fournaise": {
        "Warm Tape": {"fournaise.style": 0.0, "fournaise.drive": 4.0, "fournaise.tone": -0.15},
        "Mixing Console": {"fournaise.style": 1.0, "fournaise.drive": 3.0},
        "Class A Vocal": {"fournaise.style": 2.0, "fournaise.drive": 3.5, "fournaise.lowCut": 80.0},
        "Biting Valve": {"fournaise.style": 3.0, "fournaise.drive": 6.0, "fournaise.tone": 0.2},
        "Wrecked Pentode": {"fournaise.style": 4.0, "fournaise.drive": 7.0, "fournaise.punish": 1.0,
                            "fournaise.highCut": 9_000.0},
        "Pushed Drums": {"fournaise.style": 0.0, "fournaise.drive": 6.0, "fournaise.thump": 1.0, "fournaise.lowCut": 40.0},
        "Fat Bass": {"fournaise.style": 2.0, "fournaise.drive": 5.0, "fournaise.highCut": 6_000.0},
        "Lo-Fi": {"fournaise.style": 4.0, "fournaise.drive": 5.0, "fournaise.lowCut": 300.0, "fournaise.highCut": 4_000.0,
                  "fournaise.steep": 1.0},
        "Parallel Saturation": {"fournaise.style": 3.0, "fournaise.drive": 8.0, "fournaise.mix": 0.35},
        # Drums.
        "Kick Tape Thump": values("fournaise", style=0, drive=5.0, tone=-0.1, lowCut=30.0, highCut=16_000.0, steep=0,
                                  thump=1, punish=0, mix=1.0, output=0.0, auto=1, oversampling=2),
        "Snare Crack Valve": values("fournaise", style=3, drive=5.5, tone=0.25, lowCut=100.0, highCut=14_000.0, steep=0,
                                    thump=0, punish=0, mix=0.8, output=0.0, auto=1, oversampling=2),
        "Drum Bus Console Glue": values("fournaise", style=1, drive=4.0, tone=0.05, lowCut=25.0, highCut=18_000.0, steep=0,
                                        thump=0, punish=0, mix=1.0, output=0.0, auto=1, oversampling=1),
        "Parallel Drum Smash": values("fournaise", style=4, drive=8.0, tone=0.2, lowCut=60.0, highCut=10_000.0, steep=1,
                                      thump=0, punish=1, mix=0.25, output=0.0, auto=1, oversampling=2),
        "Industrial Punished Drums": values("fournaise", style=4, drive=9.0, tone=0.3, lowCut=80.0, highCut=8_000.0, steep=0,
                                            thump=1, punish=1, mix=0.6, output=0.0, auto=1, oversampling=2),
        "Room Mics Crushed Tape": values("fournaise", style=0, drive=8.5, tone=0.1, lowCut=70.0, highCut=11_000.0, steep=0,
                                         thump=0, punish=1, mix=0.5, output=0.0, auto=1, oversampling=1),
        # Voices.
        "Lead Vocal Console Warmth": values("fournaise", style=1, drive=3.5, tone=0.1, lowCut=90.0, highCut=18_000.0,
                                            steep=0, thump=0, punish=0, mix=0.85, output=0.0, auto=1, oversampling=2),
        "Rap Vocal Valve Edge": values("fournaise", style=3, drive=5.0, tone=0.3, lowCut=120.0, highCut=15_000.0, steep=0,
                                       thump=0, punish=0, mix=0.7, output=0.0, auto=1, oversampling=2),
        # Bass.
        "Bass Tape Weight": values("fournaise", style=0, drive=6.0, tone=-0.25, lowCut=30.0, highCut=8_000.0, steep=0,
                                   thump=1, punish=0, mix=1.0, output=0.0, auto=1, oversampling=1),
        "Trap Sub Bass Grit": values("fournaise", style=4, drive=6.0, tone=0.1, lowCut=25.0, highCut=7_000.0, steep=1,
                                     thump=0, punish=0, mix=0.6, output=0.0, auto=1, oversampling=2),
        "Synthwave Bass Console Drive": values("fournaise", style=1, drive=6.5, tone=0.2, lowCut=35.0, highCut=12_000.0,
                                               steep=0, thump=1, punish=0, mix=1.0, output=0.0, auto=1, oversampling=1),
        # Instruments.
        "Electric Piano Valve Bark": values("fournaise", style=3, drive=4.5, tone=0.1, lowCut=60.0, highCut=12_000.0,
                                            steep=0, thump=0, punish=0, mix=1.0, output=0.0, auto=1, oversampling=2),
        "Piano Gentle Class A": values("fournaise", style=2, drive=2.5, tone=-0.1, lowCut=40.0, highCut=17_000.0, steep=0,
                                       thump=0, punish=0, mix=1.0, output=0.0, auto=1, oversampling=1),
        "Acoustic Guitar Console Polish": values("fournaise", style=1, drive=2.5, tone=0.15, lowCut=80.0, highCut=18_000.0,
                                                 steep=0, thump=0, punish=0, mix=1.0, output=0.0, auto=1, oversampling=1),
        "Shoegaze Guitar Wall": values("fournaise", style=3, drive=8.0, tone=-0.2, lowCut=100.0, highCut=7_000.0, steep=1,
                                       thump=0, punish=1, mix=1.0, output=0.0, auto=1, oversampling=2),
        "Synth Lead Pentode Fuzz": values("fournaise", style=4, drive=7.5, tone=0.2, lowCut=150.0, highCut=9_000.0, steep=1,
                                          thump=0, punish=0, mix=1.0, output=0.0, auto=1, oversampling=2),
        "Brass Section Bite": values("fournaise", style=3, drive=4.0, tone=0.35, lowCut=110.0, highCut=13_000.0, steep=0,
                                     thump=0, punish=0, mix=0.9, output=0.0, auto=1, oversampling=2),
        "Orchestral Strings Tape Warmth": values("fournaise", style=0, drive=3.0, tone=-0.1, lowCut=50.0, highCut=16_000.0,
                                                 steep=0, thump=0, punish=0, mix=0.8, output=0.0, auto=1, oversampling=1),
        # Mixes and aesthetics.
        "Lo-Fi Hip-Hop Crunch": values("fournaise", style=2, drive=7.0, tone=-0.4, lowCut=120.0, highCut=5_000.0, steep=1,
                                       thump=0, punish=0, mix=0.7, output=0.0, auto=1, oversampling=1),
        "Master Bus Tape Kiss": values("fournaise", style=0, drive=1.8, tone=-0.05, lowCut=15.0, highCut=20_000.0, steep=0,
                                       thump=0, punish=0, mix=1.0, output=0.0, auto=1, oversampling=2),
    },
}
