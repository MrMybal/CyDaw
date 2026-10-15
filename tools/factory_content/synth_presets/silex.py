"""Silex's factory kits (a drum machine whose pieces are synthesised; see build_synth_presets.py).

A kit is a few settings over the defaults ("Init" first): keys as in Silex's table ("kick.decay", "hat.open",
"kit.character"); the character by its English name ("Analog", "Digital", "Lo-fi"). Each key of the drum map is a
piece: 36 the kick, 38 the snare, 39 the clap, 42/44/46 the hats, 41-50 the toms, 49 the crash, 51 the ride, 37 the
rimshot, 56 the cowbell, 70 the maracas, 75 the claves. Its level is set on the kick (note 36): C4 is silent here.
"""

PRESETS = {
    "Init": {},
    # ---- Classic boxes, in spirit ----------------------------------------------------------------
    "Eighties Analog": {
        # The long, round boom of the first analog boxes: a kick that barely falls, a crisp snare, metal hats.
        "kick.tune": 48, "kick.sweep": 7, "kick.time": 0.05, "kick.decay": 1.4, "kick.click": 0.1, "kick.drive": 0.05,
        "snare.tune": 238, "snare.tone": 0.55, "snare.snappy": 0.7, "snare.colour": 0.65, "snare.decay": 0.18,
        "clap.spread": 0.012, "clap.tone": 1_000, "clap.decay": 0.35,
        "hat.metal": 0.9, "hat.tone": 0.55, "hat.closed": 0.05, "hat.open": 0.45,
        "tom.tune": 90, "tom.drop": 3, "tom.decay": 0.7, "tom.tone": 0.0,
        "cowbell.level": 0.55, "cowbell.decay": 0.35, "clave.level": 0.6, "kit.character": "Analog",
    },
    "Punchy Analog": {
        # The punchy box after it: a kick that snaps down, a bright snare, a tight clap, hats with an edge.
        "kick.tune": 55, "kick.sweep": 26, "kick.time": 0.03, "kick.decay": 0.45, "kick.click": 0.55, "kick.drive": 0.3,
        "snare.tune": 190, "snare.tone": 0.6, "snare.snappy": 0.75, "snare.colour": 0.7, "snare.decay": 0.22,
        "clap.spread": 0.01, "clap.tone": 1_250, "hat.metal": 0.55, "hat.tone": 0.65, "hat.closed": 0.06,
        "hat.open": 0.4, "crash.tone": 0.6, "crash.decay": 2.5, "ride.tone": 0.6, "ride.bell": 0.25, "ride.decay": 3.0,
        "tom.tune": 105, "tom.drop": 6, "tom.decay": 0.45, "tom.tone": 0.35, "kit.character": "Analog",
    },
    "Acid Kit": {
        # The punchy box driven a little: a kick that bites, hats and a ride to ride the bass line.
        "kick.tune": 52, "kick.sweep": 26, "kick.time": 0.03, "kick.decay": 0.5, "kick.click": 0.5, "kick.drive": 0.5,
        "snare.tone": 0.6, "snare.snappy": 0.7, "clap.tone": 1_100, "hat.metal": 0.6, "hat.tone": 0.6,
        "hat.closed": 0.05, "hat.open": 0.35, "ride.tone": 0.55, "ride.decay": 2.2, "kit.character": "Analog",
        "master.drive": 0.25,
    },
    # ---- House and techno ------------------------------------------------------------------------
    "House Kit": {
        # Four on the floor: a round kick, a big clap, open hats on the off-beat a little to the right.
        "kick.tune": 50, "kick.sweep": 22, "kick.time": 0.035, "kick.decay": 0.55, "kick.click": 0.4, "kick.drive": 0.25,
        "snare.tone": 0.65, "clap.tone": 1_300, "clap.decay": 0.45, "clap.level": 0.8, "hat.tone": 0.6,
        "hat.closed": 0.05, "hat.open": 0.35, "hat.level": 0.6, "hat.pan": 0.15, "shaker.level": 0.45,
        "shaker.pan": -0.2,
    },
    "Deep House": {
        # Warm and dark: a soft kick, a low snare, a long clap, dull hats, the rimshot to the fore.
        "kick.tune": 46, "kick.sweep": 18, "kick.time": 0.04, "kick.decay": 0.7, "kick.click": 0.2, "kick.drive": 0.1,
        "snare.tune": 170, "snare.tone": 0.45, "snare.colour": 0.35, "snare.decay": 0.3, "clap.tone": 950,
        "clap.decay": 0.6, "hat.metal": 0.5, "hat.tone": 0.4, "hat.closed": 0.07, "hat.open": 0.5,
        "rim.level": 0.65, "kit.character": "Analog", "master.tone": -0.3,
    },
    "Techno Kit": {
        # Sharp and driven: a kick that knocks, short bright hats, a dry clap, a pinging ride.
        "kick.tune": 50, "kick.sweep": 30, "kick.time": 0.025, "kick.decay": 0.6, "kick.click": 0.6, "kick.drive": 0.45,
        "hat.metal": 0.6, "hat.tone": 0.7, "hat.closed": 0.04, "hat.open": 0.3, "clap.tone": 1_500, "clap.decay": 0.25,
        "ride.tone": 0.7, "ride.bell": 0.2, "ride.decay": 2.0, "kit.character": "Digital", "master.drive": 0.15,
    },
    "Warehouse Techno": {
        # Big and saturated: a long driven kick, a noisy snare, a clap in a large room, dark open hats.
        "kick.tune": 44, "kick.sweep": 34, "kick.time": 0.03, "kick.decay": 0.9, "kick.click": 0.5, "kick.drive": 0.8,
        "snare.tone": 0.8, "snare.snappy": 0.9, "snare.colour": 0.4, "snare.decay": 0.35, "clap.tone": 900,
        "clap.decay": 0.8, "hat.metal": 0.4, "hat.tone": 0.45, "hat.closed": 0.05, "hat.open": 0.6,
        "kit.character": "Analog", "master.drive": 0.35, "master.tone": -0.15,
    },
    "Minimal Kit": {
        # Small and clicky: a short kick, a thin snare, a tight clap, ticking hats, rimshot and claves.
        "kick.tune": 58, "kick.sweep": 14, "kick.time": 0.02, "kick.decay": 0.3, "kick.click": 0.7, "kick.drive": 0.1,
        "snare.tune": 260, "snare.tone": 0.35, "snare.snappy": 0.3, "snare.decay": 0.12, "clap.spread": 0.006,
        "clap.tone": 1_800, "clap.decay": 0.15, "hat.metal": 0.3, "hat.tone": 0.75, "hat.closed": 0.03, "hat.open": 0.2,
        "rim.level": 0.7, "rim.decay": 0.03, "clave.level": 0.6, "kit.character": "Digital", "kit.decay": 0.8,
    },
    "Big Room": {
        # Huge: a kick with a long fall and a lot of drive, a wide clap and snare, a long crash.
        "kick.tune": 50, "kick.sweep": 36, "kick.time": 0.03, "kick.decay": 0.8, "kick.click": 0.7, "kick.drive": 0.7,
        "kick.level": 1.0, "snare.tone": 0.7, "snare.decay": 0.5, "clap.spread": 0.014, "clap.tone": 1_200,
        "clap.decay": 1.0, "clap.level": 0.85, "crash.decay": 4.0, "kit.character": "Digital", "master.drive": 0.2,
    },
    # ---- Electro, hip-hop and trap ---------------------------------------------------------------
    "Electro Kit": {
        # Booming and crisp: a long kick, a snappy snare, metal hats, the cowbell and the toms in front.
        "kick.tune": 45, "kick.sweep": 12, "kick.time": 0.06, "kick.decay": 1.0, "kick.click": 0.15, "kick.drive": 0.1,
        "snare.tune": 240, "snare.tone": 0.45, "snare.snappy": 0.8, "snare.colour": 0.7, "snare.decay": 0.15,
        "clap.spread": 0.013, "clap.tone": 1_050, "clap.decay": 0.4, "hat.metal": 0.95, "hat.tone": 0.6,
        "hat.closed": 0.04, "hat.open": 0.35, "cowbell.level": 0.6, "tom.tune": 80, "tom.drop": 4, "tom.decay": 0.8,
        "tom.tone": 0.0, "kit.character": "Digital",
    },
    "Boom Bap": {
        # Dusty and knocking: a punchy kick, a fat snare, soft hats, the whole kit crunched a little.
        "kick.tune": 56, "kick.sweep": 16, "kick.time": 0.03, "kick.decay": 0.45, "kick.click": 0.45, "kick.drive": 0.35,
        "snare.tune": 175, "snare.tone": 0.6, "snare.snappy": 0.5, "snare.colour": 0.45, "snare.decay": 0.3,
        "hat.metal": 0.4, "hat.tone": 0.45, "hat.closed": 0.06, "hat.open": 0.4, "shaker.level": 0.35,
        "kit.character": "Lo-fi", "master.tone": -0.2,
    },
    "Trap Kit": {
        # A long sub kick, a bright snare and clap, short hats that roll (the velocity counts).
        "kick.tune": 42, "kick.sweep": 10, "kick.time": 0.07, "kick.decay": 2.2, "kick.click": 0.2, "kick.drive": 0.35,
        "snare.tune": 210, "snare.tone": 0.6, "snare.snappy": 0.6, "snare.colour": 0.75, "snare.decay": 0.2,
        "clap.spread": 0.009, "clap.tone": 1_400, "clap.decay": 0.35, "clap.level": 0.8, "hat.metal": 0.6,
        "hat.tone": 0.75, "hat.closed": 0.035, "hat.open": 0.4, "kit.character": "Digital", "kit.velocity": 0.8,
    },
    "Trap Long Boom": {
        # The kick as a bass: low, very long, driven so it is heard on small speakers; short hats.
        "kick.tune": 38, "kick.sweep": 6, "kick.time": 0.08, "kick.decay": 3.5, "kick.click": 0.1, "kick.drive": 0.55,
        "kick.level": 1.0, "snare.tune": 220, "snare.tone": 0.55, "snare.colour": 0.7, "snare.decay": 0.18,
        "clap.tone": 1_500, "clap.decay": 0.3, "hat.tone": 0.8, "hat.closed": 0.03, "hat.open": 0.3,
        "kit.character": "Digital", "kit.velocity": 0.8, "master.drive": 0.1,
    },
    # ---- Lo-fi and dirt --------------------------------------------------------------------------
    "Lo-fi Beats": {
        # Soft and dark, its rate and its bits reduced: a gentle kick, a muted snare, dull hats.
        "kick.tune": 52, "kick.sweep": 12, "kick.time": 0.04, "kick.decay": 0.5, "kick.click": 0.2, "kick.drive": 0.2,
        "snare.tune": 200, "snare.tone": 0.5, "snare.snappy": 0.4, "snare.colour": 0.3, "snare.decay": 0.25,
        "hat.metal": 0.35, "hat.tone": 0.35, "rim.level": 0.65, "shaker.level": 0.6, "kit.character": "Lo-fi",
        "kit.velocity": 0.75, "master.tone": -0.5,
    },
    "Crushed Lo-fi": {
        # Crunchy all through: driven pieces, reduced and saturated again at the output.
        "kick.decay": 0.6, "kick.click": 0.5, "kick.drive": 0.6, "snare.tone": 0.7, "snare.snappy": 0.8,
        "snare.decay": 0.3, "clap.decay": 0.4, "hat.metal": 0.7, "hat.tone": 0.55, "kit.character": "Lo-fi",
        "master.drive": 0.6, "master.tone": -0.1,
    },
    "Industrial": {
        # Metal and noise: a kick hit hard, a snare all noise, a clap in a hangar, low metal hats and cymbals.
        "kick.tune": 40, "kick.sweep": 40, "kick.time": 0.05, "kick.decay": 1.2, "kick.click": 1.0, "kick.drive": 1.0,
        "snare.tune": 140, "snare.tone": 0.85, "snare.snappy": 1.0, "snare.colour": 0.2, "snare.decay": 0.6,
        "clap.spread": 0.02, "clap.tone": 700, "clap.decay": 1.2, "hat.tune": -7, "hat.metal": 1.0, "hat.tone": 0.3,
        "hat.open": 1.2, "crash.tune": -9, "crash.decay": 5.0, "ride.tune": -12, "ride.bell": 0.8,
        "kit.character": "Lo-fi", "master.drive": 0.7,
    },
    # ---- Pop, dance and the rest -----------------------------------------------------------------
    "Synth Pop": {
        # A big gated-sounding snare, toms that fall a long way across the stereo, a bright clap.
        "kick.tune": 54, "kick.sweep": 20, "kick.decay": 0.5, "snare.tune": 220, "snare.tone": 0.55,
        "snare.snappy": 0.8, "snare.decay": 0.5, "tom.tune": 85, "tom.drop": 12, "tom.decay": 1.2, "tom.tone": 0.2,
        "tom.spread": 0.8, "clap.level": 0.7, "kit.character": "Analog",
    },
    "Disco Kit": {
        # Tight and lively: a short kick, an open snare, hissing open hats, falling toms, cowbell and maracas.
        "kick.tune": 56, "kick.sweep": 16, "kick.decay": 0.35, "kick.click": 0.35, "snare.tune": 200, "snare.tone": 0.5,
        "snare.snappy": 0.55, "snare.decay": 0.2, "hat.tone": 0.6, "hat.closed": 0.05, "hat.open": 0.35,
        "tom.tune": 110, "tom.drop": 9, "tom.decay": 0.5, "cowbell.level": 0.5, "shaker.level": 0.55,
        "kit.character": "Analog",
    },
    "Drum and Bass": {
        # Fast and light: a short punchy kick, a high cracking snare, bright ticking hats.
        "kick.tune": 58, "kick.sweep": 28, "kick.time": 0.02, "kick.decay": 0.35, "kick.click": 0.65, "kick.drive": 0.2,
        "snare.tune": 280, "snare.tone": 0.55, "snare.snappy": 0.85, "snare.colour": 0.8, "snare.decay": 0.22,
        "hat.tone": 0.8, "hat.closed": 0.035, "ride.tone": 0.7, "ride.decay": 2.0, "kit.character": "Digital",
    },
    "UK Garage": {
        # Skippy and crisp: a short snare, the rimshot up front, a quick clap, ticking hats and maracas.
        "kick.tune": 50, "kick.sweep": 20, "kick.decay": 0.45, "snare.tune": 230, "snare.tone": 0.5,
        "snare.decay": 0.15, "rim.tune": 520, "rim.level": 0.75, "clap.spread": 0.007, "clap.tone": 1_600,
        "clap.decay": 0.25, "hat.tone": 0.7, "hat.closed": 0.04, "hat.open": 0.25, "shaker.level": 0.55,
        "kit.character": "Digital",
    },
    "Dub Kit": {
        # Deep and wide: a round kick, a dark snare, a ringing rimshot, long low toms, the highs taken down.
        "kick.tune": 45, "kick.sweep": 10, "kick.decay": 0.9, "kick.click": 0.1, "snare.tune": 160, "snare.tone": 0.4,
        "snare.snappy": 0.35, "snare.colour": 0.3, "snare.decay": 0.4, "rim.tune": 400, "rim.decay": 0.06,
        "rim.level": 0.8, "tom.tune": 70, "tom.drop": 5, "tom.decay": 1.5, "hat.tone": 0.35,
        "kit.character": "Analog", "master.tone": -0.4,
    },
    "Latin Percussion": {
        # The percussions to the fore: cowbell, claves and maracas spread out, toms tuned high as hand drums.
        "kick.tune": 60, "kick.sweep": 10, "kick.decay": 0.35, "cowbell.tune": 2, "cowbell.level": 0.7,
        "cowbell.pan": 0.3, "clave.level": 0.7, "clave.pan": -0.4, "shaker.level": 0.6, "shaker.decay": 0.09,
        "shaker.pan": 0.4, "tom.tune": 150, "tom.drop": 2, "tom.decay": 0.4, "tom.tone": 0.15, "tom.spread": 0.9,
        "rim.tune": 600, "rim.level": 0.7, "kit.character": "Analog",
    },
    "Crisp Digital": {
        # Clean and bright: every attack sharp, the noises open, nothing rounded.
        "kick.click": 0.6, "kick.drive": 0.1, "snare.colour": 0.8, "snare.snappy": 0.8, "hat.tone": 0.8,
        "hat.metal": 0.5, "hat.level": 0.4, "clap.tone": 1_600, "crash.tone": 0.7, "ride.tone": 0.7, "kit.character": "Digital",
        "master.tone": 0.3,
    },
    "Tuned Down Heavy": {
        # The whole kit a fourth down and longer: big, slow, dark pieces.
        "kick.decay": 0.8, "kick.drive": 0.4, "snare.tone": 0.55, "snare.decay": 0.4, "tom.decay": 1.0,
        "kit.tune": -5, "kit.decay": 1.4, "kit.character": "Analog", "master.drive": 0.2, "master.tone": -0.2,
    },
}
