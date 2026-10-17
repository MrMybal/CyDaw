"""Factory presets of the containers (Containers.h, InstrumentContainers.h).

An effect container's preset holds all it holds: each branch's devices (each set
from one of its own factory presets, then by binding suffix), the branches'
names, the container's own settings. An instrument container's holds its pads
or layers: each an instrument (set from one of its presets), its mix, its keys
and velocities; a selector's way of choosing.

English names only, no brands, no "/" in a name.
"""
from __future__ import annotations

# The binding prefix of each container's own settings.
PREFIX = {
    "fx-chain": "fxChain",
    "fx-layer": "fxLayer",
    "fx-selector": "fxSelector",
    "mid-side-split": "midSide",
    "stereo-split": "stereoSplit",
    "multiband-2": "multiband2",
    "multiband-3": "multiband3",
    "xy-fx": "xyFx",
    "replacer": "replacer",
}

# Containers whose branches can be added (the others have fixed ones).
VARIABLE = {"fx-layer", "fx-selector"}


def branch(name: str, *devices: tuple) -> tuple[str, list[tuple]]:
    """A branch: its name (empty: its usual one), its devices as (key, preset, {suffix: value})."""
    return name, [device if len(device) == 3 else (*device, {}) for device in devices]


# ---- Effect containers: {type: {preset: {"head": {setting: value}, "branches": [branch, ...]}}}
EFFECTS: dict[str, dict[str, dict]] = {
    "fx-chain": {
        "Warm Tape Chain": {"branches": [branch("", ("warm-drive", "Tape Warmth"), ("echo-bande", "Piano Soft Tape Echo"))]},
        "Lo-Fi Chain": {"branches": [branch("", ("patine", "Lo-Fi Hip-Hop"), ("multimode-filter", "Warm Low-Pass"))]},
        "Vocal Polish Chain": {"branches": [branch("", ("comp-pro", "Gentle Vocal"), ("reverb-pro", "Lead Vocal Plate"))]},
        "Drum Bus Chain": {"branches": [branch("", ("transient-shaper", "Punchy Drums"), ("fournaise", "Drum Bus Console Glue"))]},
    },
    "fx-layer": {
        "Parallel Saturation": {"head": {"volume2": -6.0},
                                "branches": [branch("Dry"), branch("Heat", ("fournaise", "Drum Bus Console Glue"))]},
        "Parallel Crush": {"head": {"volume2": -9.0},
                           "branches": [branch("Dry"), branch("Crushed", ("bitcrusher", "Parallel Crush"))]},
        "Wide Doubler": {"head": {"volume2": -8.0, "volume3": -10.0, "pan2": -0.4, "pan3": 0.4},
                         "branches": [branch("Dry"), branch("Chorus", ("chorus", "Wide Ensemble")),
                                      branch("Echo", ("ping-pong", "Eighth Ping-Pong"))]},
        "Shimmer Send": {"head": {"volume2": -10.0},
                         "branches": [branch("Dry"), branch("Shimmer", ("grains", "Shimmer Cloud"), ("reverb-pro", "Immense Pad"))]},
    },
    "fx-selector": {
        "Three Colours": {"branches": [branch("Clean"), branch("Warm", ("warm-drive", "Subtle Glow")),
                                       branch("Lo-Fi", ("patine", "AM Radio"))]},
        "Delay Choices": {"branches": [branch("Eighth", ("ping-pong", "Eighth Ping-Pong")),
                                       branch("Dotted", ("ping-pong", "Dotted Eighth")),
                                       branch("Tape", ("echo-bande", "Dub Echo"))]},
        "Filter Moves": {"branches": [branch("Low-Pass", ("multimode-filter", "Warm Low-Pass")),
                                      branch("High-Pass", ("multimode-filter", "Clean High-Pass")),
                                      branch("Band", ("multimode-filter", "Telephone Band"))]},
    },
    "mid-side-split": {
        "Wide Reverb Sides": {"head": {"sideGain": 2.0},
                              "branches": [branch(""), branch("", ("chorus", "Subtle Thickener"), ("reverb-pro", "Bright Plate"))]},
        "Solid Mid": {"head": {"sideGain": -2.0}, "branches": [branch("", ("pression", "Gentle Pressure")), branch("")]},
        "Air on the Sides": {"branches": [branch(""), branch("", ("eq-pro", "Gentle Sheen"))]},
    },
    "stereo-split": {
        "Split Tape Echoes": {"branches": [branch("", ("echo-bande", "Eighth Notes")), branch("", ("echo-bande", "Dotted Quarter"))]},
        "Split Detune": {"branches": [branch("", ("decaleur", "Subtle Detune Shimmer")), branch("", ("chorus", "Subtle Thickener"))]},
        "Left Right Motion": {"branches": [branch("", ("phaser", "Slow Swirl")), branch("", ("flanger", "Subtle Flange"))]},
    },
    "multiband-2": {
        "Saturated Highs": {"head": {"crossover": 1500.0}, "branches": [branch(""), branch("", ("fournaise", "Biting Valve"))]},
        "Tight Lows": {"head": {"crossover": 150.0}, "branches": [branch("", ("transient-shaper", "Dry and Tight")), branch("")]},
        "Air Shimmer": {"head": {"crossover": 6000.0, "volume2": -4.0}, "branches": [branch(""), branch("", ("grains", "Shimmer Cloud"))]},
    },
    "multiband-3": {
        "Three Band Glue": {"branches": [branch("", ("comp-pro", "Bus Glue")), branch("", ("comp-pro", "Bus Glue")),
                                         branch("", ("comp-pro", "Bus Glue"))]},
        "Warm Lows Bright Tops": {"branches": [branch("", ("warm-drive", "Bass Grit")), branch(""),
                                               branch("", ("chorus", "Subtle Thickener"))]},
        "Mid Focus": {"head": {"volume1": -2.0, "volume3": -2.0},
                      "branches": [branch(""), branch("", ("eq-pro", "Clear Piano Without Mud")), branch("")]},
    },
    "xy-fx": {
        "Four Spaces": {"branches": [branch("Room", ("reverb-pro", "Snare Room Slam")), branch("Hall", ("reverb-pro", "Ballad Vocal Hall")),
                                     branch("Bounce", ("ping-pong", "Quarter Bounce")), branch("Cloud", ("grains", "Endless Pad Bed"))]},
        "Texture Pad": {"branches": [branch("Smear", ("givre", "Subtle Smear")), branch("Shimmer", ("grains", "Shimmer Cloud")),
                                     branch("Faded", ("patine", "Faded Memory Ambient")), branch("Spiral", ("decaleur", "Spiral Ascent"))]},
        "Drive Corners": {"branches": [branch("Glow", ("warm-drive", "Subtle Glow")), branch("Fat", ("fournaise", "Fat Bass")),
                                       branch("Crushed", ("bitcrusher", "Retro 12-bit")), branch("Radio", ("patine", "AM Radio")),
                                       branch("", ("multimode-filter", "Warm Low-Pass"))]},
    },
    "replacer": {
        "Kick Reinforce": {"head": {"generator": 0, "pitch": 52.0, "decay": 280.0, "threshold": -26.0, "mix": 0.6}, "branches": []},
        "Sub Thump": {"head": {"generator": 1, "pitch": 45.0, "decay": 400.0, "threshold": -24.0, "mix": 0.5}, "branches": []},
        "Snare Crack": {"head": {"generator": 2, "decay": 120.0, "threshold": -22.0, "mix": 0.45},
                        "branches": [branch("", ("transient-shaper", "More Attack"))]},
        "Click Attack": {"head": {"generator": 3, "decay": 20.0, "threshold": -20.0, "mix": 0.4}, "branches": []},
    },
}


def layer(name: str, instrument: str, preset: str | None = None, **mix: float) -> dict:
    """A pad or a layer: its name, its instrument (a catalogue key), one of its presets, its mix."""
    return {"name": name, "instrument": instrument, "preset": preset, "mix": mix}


# ---- Instrument containers: {card type: {preset: {"pads": [...], "selector": {setting: value}}}}
# A drum machine's pads by key ("note" in their mix); a kit is its kick's
# preset and Silex's for the rest.
def kit(kick: str, silex: str) -> dict:
    return {"pads": [layer("Kick", "kick-instrument", kick, note=36), layer("Snare", "silex-instrument", silex, note=38),
                     layer("Clap", "silex-instrument", silex, note=39), layer("Closed Hat", "silex-instrument", silex, note=42, choke=1),
                     layer("Open Hat", "silex-instrument", silex, note=46, choke=1), layer("Low Tom", "silex-instrument", silex, note=41),
                     layer("High Tom", "silex-instrument", silex, note=45), layer("Crash", "silex-instrument", silex, note=49)]}


INSTRUMENTS: dict[str, dict[str, dict]] = {
    "drum-rack": {
        "Techno Kit": kit("Techno Hard", "Techno Kit"),
        "House Kit": kit("House Classic", "House Kit"),
        "Trap Kit": kit("Trap Boom", "Trap Kit"),
        "Lo-fi Kit": kit("Lo-Fi Dusty", "Lo-fi Beats"),
        "Electro Kit": kit("808 Short", "Electro Kit"),
        "Minimal Kit": kit("Minimal Tight Knock", "Minimal Kit"),
    },
    "instrument-layer": {
        "Piano and Pad": {"pads": [layer("Piano", "jade-instrument", "Classic Tine Piano"),
                                   layer("Pad", "nebuleuse-instrument", "Chord Cloud", gainDb=-8.0)]},
        "Bass and Keys Split": {"pads": [layer("Bass", "argile-instrument", "Fat Saw Bass", keyHigh=59),
                                         layer("Keys", "jade-instrument", "Bright Ballad", keyLow=60)]},
        "Velocity Layers": {"pads": [layer("Soft", "jade-instrument", "Classic Tine Piano", velocityHigh=95),
                                     layer("Hard", "opale-instrument", "Glass Keys", velocityLow=96)]},
        "Organ and Strings": {"pads": [layer("Organ", "tirette-instrument", "Gentle Hymn"),
                                       layer("Strings", "derive-instrument", "Dreamy Strings", gainDb=-6.0)]},
    },
    "instrument-selector": {
        "Synth Choices": {"selector": {"mode": 0, "layer": 1},
                          "pads": [layer("Pad", "opale-instrument", "Hollow Pad"), layer("Lead", "argile-instrument", "Classic Lead"),
                                   layer("Brass", "ambre-instrument", "Brass")]},
        "Keyswitch Keys": {"selector": {"mode": 3, "keyswitch": 24, "layer": 1},
                           "pads": [layer("Piano", "jade-instrument", "Classic Tine Piano"), layer("Organ", "tirette-instrument", "Full Organ"),
                                    layer("Glass", "opale-instrument", "Glass Keys")]},
        "Round Robin Kicks": {"selector": {"mode": 1},
                              "pads": [layer("Kick 1", "kick-instrument", "House Classic"),
                                       layer("Kick 2", "kick-instrument", "House Punchy Thump"),
                                       layer("Kick 3", "kick-instrument", "Deep House Warm Round")]},
        "Mod Wheel Pads": {"selector": {"mode": 4, "controller": 1},
                           "pads": [layer("Cloud", "nebuleuse-instrument", "Chord Cloud"), layer("Breath", "derive-instrument", "Breathing Pad"),
                                    layer("Hollow", "opale-instrument", "Hollow Pad")]},
    },
}

# The catalogue's instrument that makes each card.
INSTRUMENT_OF = {"drum-rack": "rack-instrument", "instrument-layer": "layer-instrument", "instrument-selector": "selector-instrument"}
