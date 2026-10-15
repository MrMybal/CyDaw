"""Builds the example projects that show CyDAW 2's effects, from the
templates already in templates/, through the control port.

    python tools/templates/build_v2_examples.py [--exe "<CyDAW 2.exe>"]

Each example opens a template, swaps or adds devices, sets their values and
is saved next to it in templates/ (audio paths stay relative). The acoustic
ones are written from nothing: the factory instruments (sampler and sound
models) playing a short piece, and "12-tous-les-instruments": every factory
instrument, its samples' track and its sound model's beside it, one after
the other in the arrangement (--only all-instruments makes only that one: it
is large, every model in it, and it plays every bank's samples). The session
keeps the master at -60 dB and never plays; the master is put back to the
template's level only for the save.
"""
import argparse
import json
import math
import os
import pathlib
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
TEMPLATES = ROOT / "templates"
DEFAULT_EXE = ROOT / "build" / "agent-graph" / "CyDAW2_artefacts" / "Release" / "CyDAW 2.exe"
PORT = 47831

# The equaliser's shapes and slopes (EffectCatalogue.h, BiquadDesign.h).
BELL, LOW_SHELF, HIGH_SHELF, LOW_CUT, HIGH_CUT, NOTCH, BAND_PASS, TILT = range(8)
DB12, DB24, DB36, DB48 = range(4)
# Note values for "sync" (EffectCatalogue.h's noteDivisions, 1 based).
SYNC = {"1/32": 1, "1/16T": 2, "1/16": 3, "1/8T": 4, "1/16.": 5, "1/8": 6, "1/4T": 7, "1/8.": 8,
        "1/4": 9, "1/4.": 10, "1/2": 11, "1/1": 12}


def request(command):
    with socket.create_connection(("127.0.0.1", PORT), timeout=5.0) as connection:
        connection.settimeout(120)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(65536)
            if not chunk:
                break
            data += chunk
    answer = json.loads(data.decode("utf-8"))
    if not answer.get("ok"):
        raise RuntimeError("%s: %s" % (command.get("cmd"), answer.get("error")))
    return answer.get("result") or {}


def parameter_keys(track):
    """The track's published knobs by their last two names ("eqpro.b1Type")."""
    keys = {}
    for parameter in request({"cmd": "list_parameters", "track": track}).get("parameters", []):
        keys[".".join(parameter["key"].split(".")[-2:])] = parameter["key"]
    return keys


def set_values(track, values):
    keys = parameter_keys(track)
    for suffix, value in values.items():
        if suffix not in keys:
            raise RuntimeError("%s has no %s" % (track, suffix))
        request({"cmd": "set_parameter", "track": track, "key": keys[suffix], "value": value})


def insert(track, device, values=None):
    request({"cmd": "insert_device", "track": track, "key": device})
    if values:
        set_values(track, values)


def remove(track, device_type):
    graph = request({"cmd": "get_track_graph", "track": track})
    for node in graph.get("nodes", []):
        if node.get("type") == device_type:
            request({"cmd": "remove_device", "track": track, "nodeId": node["id"]})
            return
    raise RuntimeError("%s has no %s" % (track, device_type))


def collapse(track, device_type):
    """Folds a card to its title so the devices after it are in view."""
    graph = request({"cmd": "get_track_graph", "track": track})
    for node in graph.get("nodes", []):
        if node.get("type") == device_type:
            request({"cmd": "set_node", "track": track, "nodeId": node["id"], "collapsed": True})
            return
    raise RuntimeError("%s has no %s" % (track, device_type))


def track_gain(track, source, change_db):
    """The track's volume raised from the template's by change_db."""
    data = json.loads((TEMPLATES / source).read_text(encoding="utf-8"))
    for item in data.get("tracks", []) + data.get("midiTracks", []):
        if item.get("name") == track:
            request({"cmd": "set_track", "track": track, "gainDb": item.get("gainDb", 0.0) + change_db})
            return
    raise RuntimeError("no track %s in %s" % (track, source))


def automate(track, suffix, points):
    """A lane for a device's parameter: [(seconds, value)], shown under the track."""
    keys = parameter_keys(track)
    if suffix not in keys:
        raise RuntimeError("%s has no %s" % (track, suffix))
    request({"cmd": "add_automation", "track": track, "key": keys[suffix], "clear": True,
             "points": [{"time": time, "value": value} for time, value in points]})


def equaliser(track, bands, output=0.0):
    """Adds the parametric equaliser with bands of (shape, Hz, dB, Q, slope)."""
    values = {"eqpro.output": output}
    for number, (shape, hz, gain, q, slope) in enumerate(bands, 1):
        prefix = "eqpro.b%d" % number
        values.update({prefix + "Type": shape, prefix + "Frequency": hz, prefix + "Gain": gain, prefix + "Q": q,
                       prefix + "Slope": slope, prefix + "Enabled": 1})
    insert(track, "eq-pro", values)


def save(source, name):
    master = json.loads((TEMPLATES / source).read_text(encoding="utf-8")).get("masterGainDb", 0.0)
    request({"cmd": "set_master_gain", "gainDb": master})
    path = TEMPLATES / (name + ".cydaw")
    request({"cmd": "save_project", "path": str(path)})
    request({"cmd": "set_master_gain", "gainDb": -60})
    print("saved", path.name)


def open_template(source):
    request({"cmd": "open_project", "path": str(TEMPLATES / source)})
    request({"cmd": "set_master_gain", "gainDb": -60})


def example_equaliser():
    """The rhythm template, each part shaped by the parametric equaliser."""
    open_template("05-rythmique.cydaw")
    equaliser("Kick", [(LOW_CUT, 32, 0, 0.71, DB24), (BELL, 58, 4.0, 1.3, DB24), (BELL, 380, -5.0, 2.4, DB24),
                       (HIGH_SHELF, 6500, -3.5, 0.71, DB24)], output=-3.0)
    equaliser("Snare", [(LOW_CUT, 120, 0, 0.71, DB24), (BELL, 210, 3.0, 1.1, DB24), (BELL, 850, -3.5, 3.0, DB24),
                        (BELL, 5200, 4.0, 0.9, DB24)], output=-3.0)
    equaliser("Hats", [(LOW_CUT, 420, 0, 0.71, DB48), (NOTCH, 7200, 0, 6.0, DB24), (HIGH_SHELF, 11000, 3.0, 0.71, DB24)])
    equaliser("Basse", [(BELL, 95, 3.0, 1.0, DB24), (BELL, 260, -4.0, 1.8, DB24), (HIGH_CUT, 2800, 0, 0.9, DB24)])
    # The hats' high shelf moves down and back up over the loop.
    automate("Hats", "eqpro.b3Frequency", [(0.0, 11000.0), (4.8, 4000.0), (9.6, 11000.0)])
    save("05-rythmique.cydaw", "07-egaliseur-pro")


def example_echoes():
    """The arpeggio, its echo swapped for a dotted-eighth ping-pong, panned
    in quarter notes and widened by a chorus."""
    open_template("03-arpege.cydaw")
    remove("Arpege", "wide-echo")
    collapse("Arpege", "poly-pluck")
    # The echo it loses and the dry share the effects keep: 9 dB back.
    track_gain("Arpege", "03-arpege.cydaw", 9.0)
    insert("Arpege", "tremolo-pan", {"tremolo.depth": 0.6, "tremolo.spread": 1.0, "tremolo.sync": SYNC["1/4"]})
    insert("Arpege", "ping-pong", {"pingpong.sync": SYNC["1/8."], "pingpong.feedback": 0.45, "pingpong.mix": 0.32,
                                   "pingpong.width": 1.0})
    insert("Arpege", "chorus", {"chorus.mix": 0.35, "chorus.depth": 0.45})
    # The echoes swell, then let go.
    automate("Arpege", "pingpong.feedback", [(0.0, 0.2), (8.0, 0.7), (16.0, 0.2)])
    save("03-arpege.cydaw", "08-echos-et-mouvement")


def example_dynamics():
    """The glitch template through the dynamics and the texture effects."""
    open_template("06-tipper-glitch.cydaw")
    insert("Kick", "transient-shaper", {"transient.attack": 0.6, "transient.sustain": -0.35, "transient.output": -6.5})
    insert("Snare", "gate", {"gate.threshold": -36.0, "gate.attack": 0.5, "gate.release": 90.0, "gate.range": -30.0})
    insert("Hats", "bitcrusher", {"crusher.bits": 7.0, "crusher.downsample": 2.0, "crusher.drive": 3.0, "crusher.mix": 0.45})
    insert("Glitch", "auto-filter", {"autofilter.cutoff": 900.0, "autofilter.resonance": 3.5, "autofilter.depth": 0.7,
                                     "autofilter.sync": SYNC["1/16"]})
    insert("Glitch", "limiter", {"limiter.gain": 6.0, "limiter.ceiling": -1.5, "limiter.release": 60.0})
    for device in ("drive", "hard-clip", "gain"):
        collapse("Glitch", device)
    insert("Nappe", "phaser", {"phaser.rate": 0.25, "phaser.depth": 0.8, "phaser.mix": 0.45})
    save("06-tipper-glitch.cydaw", "09-glitch-dynamique")


def example_compressor():
    """The rhythm template through the studio compressor: the kick's attack
    let through, the snare compressed in parallel, the hats levelled, the
    bass held steady with its detector filtered."""
    open_template("05-rythmique.cydaw")
    remove("Kick", "compressor")
    insert("Kick", "comp-pro", {"comppro.threshold": -16.0, "comppro.ratio": 3.0, "comppro.knee": 6.0,
                                "comppro.attack": 10.0, "comppro.release": 70.0, "comppro.makeup": 2.5})
    insert("Snare", "comp-pro", {"comppro.threshold": -34.0, "comppro.ratio": 8.0, "comppro.knee": 6.0,
                                 "comppro.attack": 2.0, "comppro.release": 90.0, "comppro.makeup": 8.5,
                                 "comppro.mix": 0.45})
    insert("Hats", "comp-pro", {"comppro.threshold": -32.0, "comppro.ratio": 3.0, "comppro.knee": 9.0,
                                "comppro.attack": 0.5, "comppro.release": 60.0, "comppro.makeup": 6.0})
    insert("Basse", "comp-pro", {"comppro.threshold": -26.0, "comppro.ratio": 4.0, "comppro.knee": 6.0,
                                 "comppro.attack": 15.0, "comppro.release": 150.0, "comppro.makeup": 4.0,
                                 "comppro.sidechain": 80.0})
    # More of the compressed snare as the loop goes on.
    automate("Snare", "comppro.mix", [(0.0, 0.15), (9.6, 0.8)])
    save("05-rythmique.cydaw", "10-compresseur-pro")


# ---- 11: the factory instruments ---------------------------------------------------------
# The canon's chords, a bar each: root pitch class, minor.
CANON = [(2, False), (9, False), (11, True), (6, True), (7, False), (2, False), (7, False), (9, False)]


def near(pitch_class, low):
    """The first note of that pitch class from `low` up."""
    return low + (pitch_class - low) % 12


def triad(root, minor, low=57, high=72):
    """The chord in close position between low and high."""
    third = 3 if minor else 4
    tones = sorted(near((root + offset) % 12, low) for offset in (0, third, 7))
    return [tone if tone <= high else tone - 12 for tone in tones]


def notes_of(events, offset_beats):
    """Events (note, start, length, velocity[, extras]) as notes; the extras
    are a note's expressions ({"pitch": [...], "gain": [...]})."""
    notes = []
    for event in events:
        note, start, length, velocity = event[:4]
        entry = {"note": note, "start": round(start - offset_beats, 5), "length": round(length, 5),
                 "velocity": round(velocity, 4)}
        if len(event) > 4:
            entry.update(event[4])
        notes.append(entry)
    return notes


def clip(track, name, start, length, events, colour):
    request({"cmd": "add_midi_clip", "track": track, "name": name, "startBeats": start, "lengthBeats": length,
             "colour": colour})
    request({"cmd": "add_notes", "track": track, "clip": name, "notes": notes_of(events, start)})


def piano_part(chords, start, velocity=0.6):
    """Bass held, the chord struck twice a bar."""
    events = []
    for bar, (root, minor) in enumerate(chords):
        at = start + bar * 4
        events.append((near(root, 36), at, 4.0, velocity + 0.04))
        for beat in (0.0, 2.0):
            for tone in triad(root, minor):
                events.append((tone, at + beat, 1.9, velocity - (0.06 if beat else 0.0)))
    return events


def harp_part(chords, start, velocity=0.5):
    """Eighth notes up and down the chord, let ring."""
    events = []
    for bar, (root, minor) in enumerate(chords):
        third = 3 if minor else 4
        base = near(root, 43)
        for step, offset in enumerate((0, 7, 12, 12 + third, 19, 12 + third, 12, 7)):
            events.append((base + offset, start + bar * 4 + step * 0.5, 1.5, velocity + (0.08 if step == 0 else 0.0)))
    return events


def factory_track(key, model=False, gain_db=0.0, reverb=None):
    track = request({"cmd": "add_factory_instrument", "key": key, "model": model})["track"]
    if not model:
        request({"cmd": "sampler_status", "track": track, "wait": 120})
    if gain_db:
        request({"cmd": "set_track", "track": track, "gainDb": gain_db})
    if reverb is not None:
        insert(track, "reverb", {"reverb.mix": reverb, "reverb.roomSize": 0.72})
    return track


def compose(tracks):
    """The piece (the canon's chords, 72 BPM) on the tracks given by role: piano,
    harp and violin (bars 1-8), harpsichord and pizzicato (9-16), another piano
    each for 17-20 ("compare"), 21-24 ("soft", broken chords) and 25-28
    ("stride"), then piano, harp and violin together to end (29-32)."""
    # 1-8: the canon on the piano, the harp, the violin's long line.
    clip(tracks["piano"], "Canon", 0, 32, piano_part(CANON, 0), "#e0a458")
    clip(tracks["harp"], "Arpeges", 0, 32, harp_part(CANON, 0), "#8fc8ff")
    line = [78, 76, 74, 73, 71, 69, 71, 73]
    clip(tracks["violin"], "Melodie", 0, 32,
         [(note, bar * 4, 3.9, 0.62 + 0.06 * (bar % 2)) for bar, note in enumerate(line)], "#d4776b")

    # 9-16: the harpsichord in sixteenths, the pizzicato bass.
    events = []
    for bar, (root, minor) in enumerate(CANON):
        third = 3 if minor else 4
        at = 32 + bar * 4
        top = near(root, 60)
        for step, offset in enumerate((0, third, 7, 12, 7, third, 0, third, 7, 12, 12 + third, 12, 7, third, 7, 12)):
            events.append((top + offset, at + step * 0.25, 0.3, 0.72))
        bottom = near(root, 41)
        for beat, offset in enumerate((0, 7, 12, 7)):
            events.append((bottom + offset, at + beat, 0.9, 0.7))
    clip(tracks["harpsichord"], "Clavecin", 32, 32, events, "#c9a86b")
    clip(tracks["pizzicato"], "Pizz", 32, 32,
         [(near(root if beat == 0 else (root + 7) % 12, 55), 32 + bar * 4 + beat * 2, 0.5, 0.62)
          for bar, (root, minor) in enumerate(CANON) for beat in (0, 1)], "#d4776b")

    # 17-20: the piano part again, on another piano.
    clip(tracks["compare"], "Canon (comparaison)", 64, 16, piano_part(CANON[:4], 64), "#c49cff")

    # 21-24: broken chords, softly.
    events = []
    for bar, (root, minor) in enumerate(CANON[:4]):
        at = 80 + bar * 4
        events.append((near(root, 36), at, 4.0, 0.5))
        for beat, tone in enumerate(triad(root, minor, 60, 76) + [triad(root, minor, 60, 76)[1]]):
            events.append((tone, at + beat, 1.8, 0.45))
    clip(tracks["soft"], "Doux", 80, 16, events, "#a89f91")

    # 25-28: a stride: bass on 1 and 3, chord on 2 and 4.
    events = []
    for bar, (root, minor) in enumerate([(2, False), (7, False), (9, False), (2, False)]):
        at = 96 + bar * 4
        for beat in range(4):
            if beat % 2 == 0:
                events.append((near(root if beat == 0 else (root + 7) % 12, 38), at + beat, 0.8, 0.72))
            else:
                for tone in triad(root, minor, 60, 74):
                    events.append((tone, at + beat, 0.5, 0.62))
    clip(tracks["stride"], "Stride", 96, 16, events, "#7ed878")

    # 29-32: all together to end on D, held.
    ending = [(7, False), (9, False), (2, False), (2, False)]
    events = piano_part(ending[:2], 112, 0.66)
    events += [(near(2, 36), 120, 8.0, 0.66)] + [(tone, 120, 8.0, 0.6) for tone in triad(2, False)]
    clip(tracks["piano"], "Final", 112, 16, events, "#e0a458")
    clip(tracks["harp"], "Final", 112, 16, harp_part(ending[:3], 112, 0.52) + [(near(2, 50) + 12, 124, 4.0, 0.5)],
         "#8fc8ff")
    clip(tracks["violin"], "Final", 112, 16, [(71, 112, 3.9, 0.66), (73, 116, 3.9, 0.7), (74, 120, 7.8, 0.74)],
         "#d4776b")


def new_piece():
    request({"cmd": "new_project"})
    request({"cmd": "set_master_gain", "gainDb": -60})
    request({"cmd": "set_tempo", "bpm": 72})
    # A new project's empty instrument track goes.
    for meter in request({"cmd": "status"}).get("meters", []):
        request({"cmd": "remove_track", "track": meter["name"]})


def save_piece(name):
    request({"cmd": "set_master_gain", "gainDb": 0.0})
    path = TEMPLATES / (name + ".cydaw")
    request({"cmd": "save_project", "path": str(path)})
    request({"cmd": "set_master_gain", "gainDb": -60})
    print("saved", path.name)


# Each instrument twice, side by side: its samples, then its sound model. In
# the arrangement the same phrase plays on the samples (4 bars), then at once
# on the model (4 bars).
COMPARISON = [
    # key, phrase, the pair's level, the model's against the samples' (dB,
    # measured on the phrase: the two equally loud, a fair comparison)
    ("grand-steinway", "piano", 0.0, -1.0),
    ("harp-concert", "harp", -2.0, -3.7),
    ("violin-arco", "violin", 0.0, 3.5),
    ("harpsichord-french", "harpsichord", -6.0, 3.7),
]


def phrase(kind, start):
    """Four bars of the canon's chords as the instrument plays them."""
    chords = CANON[:4]
    if kind == "piano":
        return piano_part(chords, start)
    if kind == "harp":
        return harp_part(chords, start, 0.55)
    if kind == "violin":
        line = [(78, 2), (76, 2), (74, 2), (73, 2), (71, 2), (69, 2), (71, 2), (73, 2)]
        events, at = [], start
        for note, length in line:
            events.append((note, at, length - 0.05, 0.7))
            at += length
        return events
    events = []
    for bar, (root, minor) in enumerate(chords):
        third = 3 if minor else 4
        top = near(root, 60)
        for step, offset in enumerate((0, third, 7, 12, 7, third, 0, third, 7, 12, 12 + third, 12, 7, third, 7, 12)):
            events.append((top + offset, start + bar * 4 + step * 0.25, 0.3, 0.72))
        for beat, offset in enumerate((0, 7, 12, 7)):
            events.append((near(root, 41) + offset, start + bar * 4 + beat, 0.9, 0.7))
    return events


def example_comparison():
    """Samples against sound models: piano, harp, violin, harpsichord, each
    one's two tracks side by side, the same phrase on the samples then on the
    model."""
    new_piece()
    at = 0
    for key, kind, pair_gain, model_gain in COMPARISON:
        samples = factory_track(key, gain_db=pair_gain, reverb=0.15)
        model = factory_track(key, model=True, gain_db=pair_gain + model_gain, reverb=0.15)
        renamed = samples + " (\u00e9chantillons)"
        request({"cmd": "set_track", "track": samples, "name": renamed})
        samples = renamed
        clip(samples, "Echantillons", at, 16, phrase(kind, at), "#e0a458")
        clip(model, "Modele", at + 16, 16, phrase(kind, at + 16), "#c49cff")
        at += 32
    save_piece("11-echantillons-et-modeles")


# ---- 12: every factory instrument, samples then model ------------------------------------
# Two bars of chords, two beats each (D, A, B minor, G), and what each family
# plays on them; the notes are then put in the instrument's range.
CHORDS = [(2, False), (9, False), (11, True), (7, False)]
MELODY = [78, 76, 74, 73, 71, 69, 71, 74]
FAMILY_COLOURS = {"keys": "#e0a458", "plucked": "#8fc8ff", "melody": "#d4776b", "low": "#b07d62", "bass": "#9b8bd6",
                  "pizz": "#d49a6b", "mallets": "#7ed878", "bells": "#c9e27a", "pad": "#c49cff", "drums": "#a89f91"}
# Each key's family where its category does not say it.
FAMILY_OF_KEY = {
    "tuba": "low", "tuba-war": "low", "contrabass-arco": "low", "double-bass-rubner": "low", "tagelharpa": "low",
    "baritone-sax": "low", "bassoon": "low", "cello-section": "melody", "cello-solo": "melody",
    "tubular-bells": "bells", "hand-chimes": "bells", "string-cyborgs": "pad", "pipe-organ": "keys",
    "dan-tranh": "plucked", "cithara": "plucked", "zither-hungarian": "plucked", "banjo": "plucked",
}
FAMILY_OF_CATEGORY = {"Pianos": "keys", "Pianos électriques": "keys", "Clavecins": "keys", "Orgues": "keys",
                      "Harpes": "plucked", "Guitares": "plucked", "Basses": "bass",
                      "Percussions mélodiques": "mallets", "Batteries et percussions": "drums",
                      "Synthés échantillonnés": "keys"}
# A kit's kick, snare, closed and open hi-hat, crash (General MIDI where it
# has them; the others as their samples are laid out).
KIT_NOTES = {"bobobo": (36, 40, 45, 43, 38), "body-percussion": (40, 61, 56, 44, 60)}


def family_of(key, category):
    if key in FAMILY_OF_KEY:
        return FAMILY_OF_KEY[key]
    if key.endswith("-pizz"):
        return "pizz"
    return FAMILY_OF_CATEGORY.get(category, "melody")


def instrument_range(map_file):
    """The notes the instrument's samples were recorded at, lowest and highest
    (two semitones more each way: a sample stretched that far stays itself);
    the keys a zone covers often reach far beyond."""
    try:
        data = json.loads(pathlib.Path(map_file).read_text(encoding="utf-8"))
        roots = [zone["root"] for zone in data.get("zones", []) if "root" in zone]
        if roots:
            return max(0, min(roots) - 2), min(127, max(roots) + 2)
    except (OSError, ValueError):
        pass
    return 36, 96


def family_phrase(family, start, kit=None):
    """Eight beats as the family plays the chords: (note, start, length, velocity)."""
    events = []
    for index, (root, minor) in enumerate(CHORDS):
        at = start + index * 2
        third = 3 if minor else 4
        tones = triad(root, minor, 60, 74)
        if family == "keys":
            events.append((near(root, 38), at, 2.0, 0.66))
            for beat in (0.0, 1.0):
                events += [(tone, at + beat, 0.95, 0.58 - 0.06 * beat) for tone in tones]
        elif family == "pad":
            events += [(tone, at, 1.98, 0.6) for tone in [near(root, 48)] + tones]
        elif family == "plucked":
            base = near(root, 50)
            for step, offset in enumerate((0, 7, 12, 12 + third)):
                events.append((base + offset, at + step * 0.5, 1.4, 0.62 + (0.06 if step == 0 else 0.0)))
        elif family == "mallets":
            base = near(root, 62)
            for step, offset in enumerate((0, third, 7, 12, 12 + third, 12, 7, third)):
                events.append((base + offset, at + step * 0.25, 0.4, 0.66))
        elif family == "pizz":
            base = near(root, 55)
            for beat, offset in enumerate((0, 7)):
                events.append((base + offset, at + beat, 0.5, 0.68))
        elif family == "bass":
            base = near(root, 28)
            events += [(base, at, 0.9, 0.72), (base + 7, at + 1.0, 0.45, 0.64), (base + 12, at + 1.5, 0.45, 0.66)]
        elif family == "low":
            events.append((near(root, 36), at, 1.95, 0.66))
        elif family == "drums":
            kick, snare, hat, open_hat, crash = kit
            events.append((kick, at, 0.25, 0.8))
            events.append((snare, at + 1.0, 0.25, 0.74))
            if index == 3:
                events.append((kick, at + 1.5, 0.25, 0.7))
            for step in range(4):
                note = open_hat if (index == 3 and step == 3) else hat
                events.append((note, at + step * 0.5, 0.2, 0.5 + (0.12 if step % 2 == 0 else 0.0)))
    if family == "keys":
        events += [(note, start + beat, 0.95, 0.66) for beat, note in enumerate(MELODY)]
    elif family == "melody":
        events += [(note, start + beat, 0.97 if beat < 7 else 1.0, 0.7) for beat, note in enumerate(MELODY)]
    elif family == "bells":
        events += [(note, start + beat * 2, 1.9, 0.66) for beat, note in enumerate((78, 74, 71, 74))]
    elif family == "drums":
        events.append((kit[4], start, 1.0, 0.7))
    return events


def fit_range(events, low, high, family):
    """The phrase moved by octaves into the instrument's range (the move that
    leaves the fewest notes out, nearest to where it is written); a note
    still out is folded in by octaves."""
    if family == "drums" or not events:
        return events
    notes = [note for note, _, _, _ in events]
    best = None
    for shift in range(-48, 49, 12):
        outside = sum(1 for note in notes if not low <= note + shift <= high)
        score = (outside, abs(shift))
        if best is None or score < best[0]:
            best = (score, shift)
    shift = best[1]
    fitted = []
    for note, at, length, velocity in events:
        moved = note + shift
        while moved < low and moved + 12 <= high:
            moved += 12
        while moved > high and moved - 12 >= low:
            moved -= 12
        fitted.append((max(low, min(high, moved)), at, length, velocity))
    return fitted


def example_all_instruments():
    """Every factory instrument, in the order CyDAW lists them: its samples'
    track, then beside it its sound model's (when it has one). In the
    arrangement each instrument has four bars: its phrase on the samples
    (bars 1-2), then the same on the model (3-4); a kit plays its groove twice."""
    new_piece()
    request({"cmd": "set_tempo", "bpm": 90})
    instruments = request({"cmd": "factory_instruments"}).get("instruments", [])
    at = 0
    for item in instruments:
        key, name = item["key"], item["name"]
        family = family_of(key, item.get("category", ""))
        low, high = instrument_range(item.get("map", ""))
        kit = None
        if family == "drums":
            kit = KIT_NOTES.get(key.replace("drums-", ""), KIT_NOTES.get(key, (36, 38, 42, 46, 49)))
        events = fit_range(family_phrase(family, at, kit), low, high, family)
        colour = FAMILY_COLOURS[family]
        samples = request({"cmd": "add_factory_instrument", "key": key})["track"]
        renamed = name + " (\u00e9chantillons)"
        request({"cmd": "set_track", "track": samples, "name": renamed})
        if item.get("model"):
            clip(renamed, "Echantillons", at, 8, events, colour)
            model = request({"cmd": "add_factory_instrument", "key": key, "model": True})["track"]
            model_name = name + " (mod\u00e8le)"
            request({"cmd": "set_track", "track": model, "name": model_name})
            clip(model_name, "Modele", at + 8, 8, [(note, start + 8, length, velocity)
                                                  for note, start, length, velocity in events], colour)
        else:
            twice = events + [(note, start + 8, length, velocity) for note, start, length, velocity in events]
            clip(renamed, "Echantillons", at, 16, twice, colour)
        print("%-28s %-8s %3d-%3d  %s" % (key, family, low, high, "model" if item.get("model") else "samples only"),
              flush=True)
        at += 16
    save_piece("12-tous-les-instruments")


# ---- 13: Prisme, the wavetable synth ---------------------------------------------------
# A minor: A, F, C, G, a bar each; 124 BPM. Each Prisme track plays one of its
# factory sounds (Presets/device/prism-synth).
PROGRESSION = [(9, True), (5, False), (0, False), (7, False)]


def prism_track(name, preset, gain_db=0.0):
    track = request({"cmd": "add_midi_track", "name": name, "instrument": "prism-instrument"})["name"]
    node = request({"cmd": "prism", "track": track})["nodeId"]
    request({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": preset})
    if gain_db:
        request({"cmd": "set_track", "track": track, "gainDb": gain_db})
    return track


def chord_tones(root, minor, low, high):
    third = 3 if minor else 4
    tones = sorted(near((root + offset) % 12, low) for offset in (0, third, 7))
    return [tone if tone <= high else tone - 12 for tone in tones]


def example_prism():
    """Prisme's sounds in a short piece: a pad and a pluck (bars 1-8), then the
    Reese bass, the supersaw lead and the bells (9-16), then the growl bass
    and the drums (17-24)."""
    new_piece()
    request({"cmd": "set_tempo", "bpm": 124})
    pad = prism_track("Nappe", "Evolving Pad", -4.0)
    pluck = prism_track("Pluck", "Digital Pluck", -2.0)
    bass = prism_track("Basse Reese", "Reese Bass", -3.0)
    lead = prism_track("Lead", "Supersaw Lead", -6.0)
    bells = prism_track("Cloches", "FM Bell", -8.0)
    growl = prism_track("Basse growl", "Growl Bass", -9.0)
    drums = request({"cmd": "add_factory_instrument", "key": "drums-seventies"})["track"]

    bars = lambda count: [PROGRESSION[bar % 4] for bar in range(count)]
    # The pad: the chords held, bars 1-24.
    events = []
    for bar, (root, minor) in enumerate(bars(24)):
        events += [(tone, bar * 4, 3.95, 0.6) for tone in chord_tones(root, minor, 57, 70)]
    clip(pad, "Accords", 0, 96, events, "#c49cff")
    # The pluck: sixteenths over the chord, bars 1-16.
    events = []
    for bar, (root, minor) in enumerate(bars(16)):
        tones = chord_tones(root, minor, 64, 79)
        pattern = [0, 1, 2, 1, 2, 0, 1, 2, 0, 2, 1, 2, 0, 1, 2, 1]
        for step, index in enumerate(pattern):
            events.append((tones[index] + (12 if step % 8 == 7 else 0), bar * 4 + step * 0.25, 0.2, 0.55 + (0.15 if step % 4 == 0 else 0.0)))
    clip(pluck, "Arpege", 0, 64, events, "#8fc8ff")
    # The Reese bass: eighths on the root, bars 9-16.
    events = []
    for bar, (root, minor) in enumerate(bars(8)):
        base = near(root, 33)
        for step in range(8):
            events.append((base + (12 if step in (3, 6) else 0), 32 + bar * 4 + step * 0.5, 0.45, 0.75))
    clip(bass, "Basse", 32, 32, events, "#b07d62")
    # The lead: a line over bars 9-16.
    line = [(76, 0, 1.5), (74, 1.5, 0.5), (72, 2, 2), (69, 4, 1.5), (72, 5.5, 0.5), (77, 6, 2), (76, 8, 1),
            (74, 9, 1), (72, 10, 1), (74, 11, 1), (76, 12, 3), (79, 15, 1), (81, 16, 2), (79, 18, 1), (76, 19, 1),
            (77, 20, 2), (76, 22, 1), (74, 23, 1), (72, 24, 2), (71, 26, 2), (72, 28, 4)]
    clip(lead, "Melodie", 32, 32, [(note, 32 + at, length - 0.05, 0.8) for note, at, length in line], "#e0a458")
    # The bells: a note a bar, high, bars 9-16.
    events = [(chord_tones(root, minor, 76, 88)[bar % 3], 32 + bar * 4 + 2, 1.5, 0.6) for bar, (root, minor) in enumerate(bars(8))]
    clip(bells, "Cloches", 32, 32, events, "#c9e27a")
    # The growl bass: a rhythm on the root, bars 17-24.
    events = []
    rhythm = [(0, 0.75), (0.75, 0.25), (1.5, 0.5), (2.5, 0.5), (3, 0.25), (3.5, 0.5)]
    for bar, (root, minor) in enumerate(bars(8)):
        base = near(root, 33)
        for at, length in rhythm:
            events.append((base, 64 + bar * 4 + at, length - 0.02, 0.8))
    clip(growl, "Growl", 64, 32, events, "#d4776b")
    # The drums, bars 17-24: kick, snare, hats.
    events = []
    for bar in range(8):
        at = 64 + bar * 4
        events += [(36, at, 0.2, 0.9), (36, at + 2.5, 0.2, 0.8), (38, at + 1, 0.2, 0.85), (38, at + 3, 0.2, 0.85)]
        events += [(42, at + step * 0.5, 0.1, 0.5 + (0.15 if step % 2 == 0 else 0.0)) for step in range(8)]
    clip(drums, "Batterie", 64, 32, events, "#a89f91")
    save_piece("13-prisme")


# ---- 14: "Pas de cinq", cool jazz in 5/4 -------------------------------------------------
# An original tune (theme, harmony and parts written here): D dorian, bars of
# five beats grouped 3 + 2, swung eighths, 160 BPM. Alto saxophone, piano,
# double bass and brushes, all factory instruments.
BEATS = 5
SWING = 0.62


def swung(beat):
    """A straight position as a swung one: the second eighth of a beat late."""
    whole = int(beat // 1)
    part = beat - whole
    if part <= 0.5:
        return whole + part / 0.5 * SWING
    return whole + SWING + (part - 0.5) / 0.5 * (1.0 - SWING)


def place(events, bar, offset, length, note, velocity):
    """A note at a bar's beat (straight), swung."""
    start = swung(bar * BEATS + offset)
    end = swung(bar * BEATS + offset + length)
    events.append((note, start, max(0.05, end - start - 0.03), velocity))


def phrase_events(bars, first_bar, velocity=0.72):
    events = []
    for index, notes in enumerate(bars):
        for offset, length, note in notes:
            place(events, first_bar + index, offset, length, note, velocity)
    return events


# The theme: eight bars over the vamp (Dm7 for three beats, G13 for two).
THEME = [
    [(0.5, 0.5, 74), (1, 1, 72), (2, 1.5, 69), (3.5, 0.5, 67), (4, 1, 69)],
    [(0, 2.5, 65), (3, 0.5, 64), (3.5, 0.5, 62), (4, 1, 64)],
    [(0.5, 0.5, 69), (1, 1, 71), (2, 0.5, 72), (2.5, 1.5, 74), (4, 1, 76)],
    [(0, 3, 74), (3.5, 1.5, 72)],
    [(0.5, 0.5, 77), (1, 0.5, 76), (1.5, 0.5, 74), (2, 1, 72), (3, 1, 71), (4, 1, 69)],
    [(0, 2, 67), (2, 0.5, 69), (2.5, 2.5, 65)],
    [(0.5, 0.5, 62), (1, 0.5, 64), (1.5, 0.5, 65), (2, 0.5, 67), (2.5, 0.5, 69), (3, 1, 72), (4, 1, 71)],
    [(0, 4, 69)],
]
THEME_END = [
    [(0.5, 0.5, 65), (1, 0.5, 67), (1.5, 0.5, 69), (2, 1, 71), (3, 1, 72), (4, 1, 73)],
    [(0, 4.5, 74)],
]
# The bridge, a chord a bar: Bbmaj7, Am7, Gm7, C9sus, Fmaj7, Em7b5, A7alt, A7alt.
BRIDGE_CHORDS = [(70, [62, 65, 69, 72]), (69, [60, 64, 67, 72]), (67, [65, 70, 74, 77]), (60, [64, 67, 70, 74]),
                 (65, [64, 67, 69, 72]), (64, [62, 67, 70, 74]), (69, [61, 65, 67, 70]), (69, [61, 65, 68, 72])]
BRIDGE = [
    [(0, 1.5, 74), (1.5, 0.5, 72), (2, 1, 69), (3, 2, 65)],
    [(0, 1, 72), (1, 0.5, 71), (1.5, 0.5, 69), (2, 2, 67), (4, 1, 64)],
    [(0, 1.5, 70), (1.5, 0.5, 69), (2, 1, 67), (3, 2, 62)],
    [(0, 1, 65), (1, 1, 67), (2, 1, 70), (3, 2, 74)],
    [(0, 2.5, 76), (2.5, 0.5, 74), (3, 2, 72)],
    [(0, 1, 70), (1, 1, 67), (2, 1, 64), (3, 2, 62)],
    [(0, 1, 61), (1, 1, 65), (2, 1, 68), (3, 1, 70), (4, 1, 72)],
    [(0, 1.5, 73), (1.5, 0.5, 72), (2, 1, 70), (3, 2, 68)],
]
DORIAN = [62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79]


def improvised(first_bar, bars, low, high, seed, density=0.75):
    """A line over the vamp: phrases of two bars, eighths moving by step with a
    leap now and then, each beat's chord tone landed on, a rest between
    phrases. The same seed, the same solo."""
    import random
    chance = random.Random(seed)
    scale = [n for n in range(low, high + 1) if n % 12 in (2, 4, 5, 7, 9, 11, 0)]
    events = []
    note = scale[len(scale) // 2]
    for bar in range(bars):
        phrase_bar = bar % 2
        in_dm = lambda beat: beat < 3
        for step in range(BEATS * 2):
            offset = step * 0.5
            # A breath at the end of each phrase.
            if phrase_bar == 1 and offset >= 3.5:
                continue
            if chance.random() > density and step % 2 == 1:
                continue
            chord = (2, 5, 9, 0) if in_dm(offset) else (7, 11, 2, 5)
            if step % 2 == 0 and chance.random() < 0.6:
                # On the beat: the nearest chord tone.
                candidates = [n for n in scale if n % 12 in chord]
                note = min(candidates, key=lambda n: abs(n - note) + chance.random() * 3)
            else:
                index = scale.index(min(scale, key=lambda n: abs(n - note)))
                move = chance.choice([-1, -1, 1, 1, 1, -2, 2, 3])
                note = scale[max(0, min(len(scale) - 1, index + move))]
            length = 0.5 if chance.random() < 0.8 else 1.0
            place(events, first_bar + bar, offset, length, note, 0.62 + 0.15 * (step % 2 == 0) + chance.random() * 0.08)
    return events


# ---- Playing it like a person ------------------------------------------------------------
# A note's expressions are curves along it, [[position 0..1, value], ...]:
# semitones for the pitch, dB for the gain (CyDAW's per-note expressions).

def curve_at(points, position):
    if not points:
        return 0.0
    if position <= points[0][0]:
        return points[0][1]
    for (a, va), (b, vb) in zip(points, points[1:]):
        if position <= b:
            return va + (vb - va) * (position - a) / max(1e-6, b - a)
    return points[-1][1]


def combine(parts):
    """Curves added together (each one flat outside its points)."""
    parts = [part for part in parts if part]
    if not parts:
        return []
    positions = sorted({round(position, 4) for part in parts for position, _ in part})
    return [[position, round(sum(curve_at(part, position) for part in parts), 3)] for position in positions]


def vibrato(seconds, onset, rate, depth, rise):
    """A vibrato that starts late and grows."""
    import math
    points = []
    step = 1.0 / (8.0 * rate)
    time = onset
    while time <= seconds + 1e-9:
        amount = depth * min(1.0, (time - onset) / max(1e-3, rise))
        points.append([min(1.0, time / seconds), amount * math.sin(2.0 * math.pi * rate * (time - onset))])
        time += step
    return points


def perform_wind(events, tempo, seed, laid_back_ms=9.0, legato=0.78):
    """A wind player's line, for an instrument set to legato (its voice
    allocator: one note at a time, sliding into a note played over the last).
    One note at a time: each note runs into the next a hair (slurred: it
    slides there, no new attack) or stops just short of it (tongued); rests
    stay rests. Then the phrases breathe (louder toward their middle), fresh
    attacks settle in pitch and phrase openings scoop up, long notes get a
    late vibrato and a swell, phrase ends fall off or fade, upbeats lean,
    some notes in a run are ghosted, and the timing sits a little behind the
    beat."""
    import random
    chance = random.Random(seed)
    beat = 60.0 / tempo
    # Behind the beat, never quite the same; lengths a touch uneven.
    moved = sorted(((note, max(0.0, start + (laid_back_ms + chance.gauss(0.0, 7.0)) / 1000.0 / beat),
                     length * chance.uniform(0.93, 1.05), velocity)
                    for note, start, length, velocity in events), key=lambda event: event[1])
    # One note at a time.
    line = []
    for index, (note, start, length, velocity) in enumerate(moved):
        end = start + length
        slurred = False
        if index + 1 < len(moved):
            following = moved[index + 1][1]
            if following - end < 0.2:
                slurred = chance.random() < legato
                end = following + 0.03 if slurred else following - 0.04
            end = min(end, following + 0.03)
        line.append([note, start, max(0.05, end - start), velocity, slurred])
    phrases, current = [], []
    for event in line:
        if current and event[1] - (current[-1][1] + current[-1][2]) >= 0.45:
            phrases.append(current)
            current = []
        current.append(event)
    if current:
        phrases.append(current)
    performed = []
    for phrase in phrases:
        count = len(phrase)
        peak = chance.uniform(0.4, 0.7)
        slid_in = False
        for index, (note, start, length, velocity, slurred) in enumerate(phrase):
            seconds = max(0.02, length * beat)
            at = lambda time: max(0.0, min(1.0, time / seconds))
            first, last = index == 0, index == count - 1
            place_in_phrase = index / max(1, count - 1)
            pitch, gain = [], []
            # The phrase's arc: full at its peak, a few dB under at its ends.
            spread = max(peak, 1.0 - peak)
            arc = -3.0 * ((place_in_phrase - peak) / spread) ** 2 if count > 2 else 0.0
            gain.append([[0.0, arc], [1.0, arc]])
            # A hair off overall; a fresh attack starts a touch flat and settles.
            drift = chance.gauss(0.0, 0.025)
            if slid_in:
                pitch.append([[0.0, drift], [1.0, drift]])
            else:
                pitch.append([[0.0, drift + chance.uniform(-0.14, -0.02)], [at(min(0.06, seconds * 0.25)), drift],
                              [1.0, drift]])
            if not slid_in and seconds > 0.12 and (first and chance.random() < 0.65 or seconds >= 0.5 and chance.random() < 0.3):
                depth = chance.uniform(0.55, 1.25) if first else chance.uniform(0.35, 0.8)
                span = min(0.09, seconds * 0.35)
                pitch.append([[0.0, -depth], [at(span * 0.45), -depth * 0.3], [at(span), 0.0]])
            if seconds >= 0.55:
                onset = min(0.38, seconds * chance.uniform(0.3, 0.45))
                depth = chance.uniform(0.12, 0.22) * (1.3 if seconds > 1.2 else 1.0)
                pitch.append(vibrato(seconds, onset, chance.uniform(5.0, 5.8), depth,
                                     min(0.45, (seconds - onset) * 0.6)))
            fell = False
            if last and seconds >= 0.3 and chance.random() < 0.35:
                span = min(0.22, seconds * 0.4)
                depth = chance.uniform(2.0, 4.5)
                pitch.append([[at(seconds - span), 0.0], [at(seconds - span * 0.45), -depth * 0.2], [1.0, -depth]])
                gain.append([[at(seconds - span), 0.0], [1.0, -14.0]])
                fell = True
            elif last and seconds >= 0.5:
                gain.append([[0.55, 0.0], [1.0, -chance.uniform(5.0, 9.0)]])
            elif last and chance.random() < 0.1:
                span = min(0.2, seconds * 0.3)
                pitch.append([[at(seconds - span), 0.0], [1.0, chance.uniform(1.5, 3.0)]])
            if seconds >= 0.7 and not fell:
                gain.append([[0.0, -chance.uniform(3.0, 6.0)], [chance.uniform(0.3, 0.5), 0.5], [1.0, -2.0]])
            if first and chance.random() < 0.3:
                gain.append([[0.0, 2.0], [at(0.08), 0.0]])
            # Velocity: upbeats leaned on, some notes in a run ghosted.
            if seconds < 0.3:
                velocity += 0.05 if start % 1.0 > 0.3 else -0.03
                if not first and not last and chance.random() < 0.1:
                    velocity *= 0.62
                    gain.append([[0.0, -5.0], [1.0, -5.0]])
            velocity = max(0.2, min(1.0, velocity + chance.gauss(0.0, 0.035)))
            extras = {}
            pitch_curve = combine(pitch)
            gain_curve = combine(gain)
            if pitch_curve:
                extras["pitch"] = pitch_curve
            if gain_curve:
                extras["gain"] = gain_curve
            performed.append((note, start, length, velocity, extras))
            slid_in = slurred
    return performed


def humanise(events, tempo, seed, timing_ms=6.0, velocity=0.05, length=0.06, push_ms=0.0):
    """Small random moves: timing, velocity and length."""
    import random
    chance = random.Random(seed)
    beat = 60.0 / tempo
    moved = []
    for event in events:
        note, start, duration, level = event[:4]
        shift = (push_ms + chance.gauss(0.0, timing_ms * 0.6)) / 1000.0 / beat
        moved.append((note, max(0.0, start + shift), max(0.04, duration * (1.0 + chance.uniform(-length, length))),
                      max(0.1, min(1.0, level + chance.gauss(0.0, velocity * 0.7)))) + tuple(event[4:]))
    return moved


def example_jazz5():
    """An original cool-jazz tune in 5/4: intro (4 bars), the theme twice, the
    bridge, the theme, a saxophone chorus, a piano chorus, the brushes alone,
    the theme to end."""
    new_piece()
    request({"cmd": "set_tempo", "bpm": 160, "numerator": 5, "denominator": 4})
    sax = factory_track("alto-sax", gain_db=-1.0, reverb=0.14)
    piano = factory_track("grand-steinway", gain_db=-6.0, reverb=0.16)
    bass = factory_track("upright-bass-jazz", gain_db=-1.0)
    drums = factory_track("drums-unruly-brushes", gain_db=-5.0, reverb=0.1)
    for track, name in ((sax, "Saxophone alto"), (piano, "Piano"), (bass, "Contrebasse"), (drums, "Balais")):
        request({"cmd": "set_track", "track": track, "name": name})
    sax, piano, bass, drums = "Saxophone alto", "Piano", "Contrebasse", "Balais"

    # The form, in bars: what each part plays where.
    sections = [("intro", 4), ("A", 8), ("A2", 8), ("B", 8), ("A3", 8), ("sax", 16), ("piano", 8), ("drums", 4),
                ("A4", 8), ("end", 2)]
    starts = {}
    at = 0
    for name, length in sections:
        starts[name] = (at, length)
        at += length
    total = at

    # The saxophone: the theme, the bridge, its chorus.
    events = []
    events += phrase_events(THEME, starts["A"][0])
    events += phrase_events(THEME[:6] + THEME_END, starts["A2"][0])
    events += phrase_events(BRIDGE, starts["B"][0], 0.76)
    events += phrase_events(THEME, starts["A3"][0])
    events += improvised(starts["sax"][0], 16, 57, 79, seed=55)
    events += phrase_events(THEME[:6] + THEME_END, starts["A4"][0])
    place(events, starts["end"][0], 0, 9.0, 74, 0.7)
    # Played, not typed: scoops, late vibrato, falls, swells, breath.
    events = perform_wind(events, 160, seed=21)
    clip(sax, "Theme et solo", starts["A"][0] * BEATS, (total - starts["A"][0]) * BEATS, events, "#e0a458")

    # The piano: rootless voicings, 3 + 2 (on 1, the "and" of 2, on 4).
    dm9 = [65, 69, 72, 76]
    g13 = [65, 69, 71, 76]
    events = []
    for bar in range(total):
        section = next(name for name, (first, length) in starts.items() if first <= bar < first + length)
        if section == "end":
            if bar == starts["end"][0]:
                for note in [50] + dm9 + [81]:
                    place(events, bar, 0, 9.0, note, 0.55)
            continue
        if section == "drums":
            continue
        if section == "B":
            index = bar - starts["B"][0]
            root, voicing = BRIDGE_CHORDS[index]
            for note in voicing:
                place(events, bar, 0, 1.5, note, 0.52)
                place(events, bar, 2.5, 0.4, note, 0.45)
                place(events, bar, 4, 0.9, note, 0.48)
            continue
        loud = 0.55 if section in ("intro", "piano") else 0.47
        variant = bar % 4
        hits_dm = [(0, 1.0), (1.5, 0.4)] if variant != 3 else [(0, 0.4), (1, 0.4), (2.5, 0.4)]
        hits_g = [(3, 1.8)] if variant % 2 == 0 else [(3.5, 1.4)]
        for offset, length in hits_dm:
            for note in dm9:
                place(events, bar, offset, length, note, loud)
        for offset, length in hits_g:
            for note in g13:
                place(events, bar, offset, length, note, loud - 0.04)
    # Its chorus: the right hand over the voicings.
    events += improvised(starts["piano"][0], 8, 69, 88, seed=8, density=0.85)
    # A chord's notes never land at once.
    events = humanise(events, 160, seed=31, timing_ms=7.0, velocity=0.05, length=0.08)
    clip(piano, "Piano", 0, total * BEATS, events, "#c49cff")

    # The bass: D, A, C under the minor chord, G and its passing note under the
    # thirteenth; walking under the bridge.
    events = []
    for bar in range(total):
        section = next(name for name, (first, length) in starts.items() if first <= bar < first + length)
        if section == "end":
            if bar == starts["end"][0]:
                place(events, bar, 0, 9.0, 38, 0.8)
            continue
        if section == "B":
            index = bar - starts["B"][0]
            root = BRIDGE_CHORDS[index][0] - 36
            following = BRIDGE_CHORDS[(index + 1) % 8][0] - 36 if index < 7 else 38
            walk = [root, root + 7, root + 12, root + 7, following + 1 if following < root else following - 1]
            for beat, note in enumerate(walk):
                place(events, bar, beat, 0.95, note, 0.78)
            continue
        if bar % 2 == 0:
            line = [(0, 38), (1, 45), (2, 48), (3, 43), (4, 47)]
        else:
            line = [(0, 38), (1, 41), (2, 45), (3, 43), (4, 42)]
        for beat, note in line:
            place(events, bar, beat, 0.95, note, 0.8 if beat in (0, 3) else 0.7)
    # The bass a hair ahead, the drums on it.
    events = humanise(events, 160, seed=41, timing_ms=5.0, velocity=0.06, length=0.05, push_ms=-3.0)
    clip(bass, "Contrebasse", 0, total * BEATS, events, "#b07d62")

    # The brushes: the snare stirred, the ride in 3 + 2, the hi-hat on 2 and 4,
    # the kick feathered; a fill before each section; alone for four bars.
    KICK, STIR_A, STIR_B, SNARE, RIDE, CHICK, CRASH = 36, 29, 30, 38, 51, 44, 49
    TOMS = [47, 45, 43, 41]
    events = []
    section_starts = {first for first, _ in starts.values()}
    for bar in range(total):
        section = next(name for name, (first, length) in starts.items() if first <= bar < first + length)
        if section == "end":
            if bar == starts["end"][0]:
                place(events, bar, 0, 2.0, CRASH, 0.5)
                place(events, bar, 0, 2.0, KICK, 0.5)
            continue
        if bar in section_starts and bar > 0:
            place(events, bar, 0, 1.0, CRASH, 0.42)
        if section == "drums":
            # Brushes alone: the toms answering the snare.
            pattern = [(0, SNARE, 0.7), (0.5, SNARE, 0.45), (1, TOMS[bar % 4], 0.65), (2, SNARE, 0.6), (2.5, TOMS[(bar + 1) % 4], 0.6),
                       (3, KICK, 0.6), (3.5, SNARE, 0.5), (4, TOMS[(bar + 2) % 4], 0.7), (4.5, SNARE, 0.55)]
            for offset, note, velocity in pattern:
                place(events, bar, offset, 0.4, note, velocity)
            continue
        place(events, bar, 0, 2.6, STIR_A, 0.5)
        place(events, bar, 3, 1.8, STIR_B, 0.45)
        for offset, velocity in ((0, 0.62), (1, 0.48), (1.5, 0.38), (2, 0.5), (3, 0.6), (3.5, 0.38), (4, 0.5)):
            place(events, bar, offset, 0.4, RIDE, velocity)
        place(events, bar, 1, 0.2, CHICK, 0.45)
        place(events, bar, 3, 0.2, CHICK, 0.45)
        place(events, bar, 0, 0.3, KICK, 0.32)
        place(events, bar, 3, 0.3, KICK, 0.28)
        if (bar + 1) in section_starts:
            for offset, velocity in ((3, 0.45), (3.5, 0.5), (4, 0.58), (4.5, 0.66)):
                place(events, bar, offset, 0.3, SNARE, velocity)
        elif bar % 2 == 1:
            place(events, bar, 4.5, 0.3, SNARE, 0.35)
    events = humanise(events, 160, seed=51, timing_ms=4.0, velocity=0.07, length=0.0)
    clip(drums, "Balais", 0, total * BEATS, events, "#a89f91")
    save_piece("14-pas-de-cinq")


# ---- 15-18: solo piano ------------------------------------------------------------------------
# Four original pieces (piano_pieces.py), each played with its pedal, rubato,
# voicing and dynamics: the subtleties a piano example should put to the test.
PIANO_COLOURS = {"luna": "#e07a5f", "greve": "#81b29a", "neons": "#c77dff", "cycles": "#f2cc8f"}


def example_piano(key):
    import piano_pieces
    piece = piano_pieces.PIECES[key]()
    new_piece()
    request({"cmd": "set_tempo", "bpm": piece.tempo, "numerator": piece.numerator, "denominator": piece.denominator})
    track = factory_track(piece.instrument, gain_db=piece.gain_db, reverb=piece.reverb)
    request({"cmd": "set_track", "track": track, "name": "Piano"})
    bar = piece.numerator * 4.0 / piece.denominator
    end = max(start + length for _, start, length, _ in piece.events)
    clip("Piano", piece.title, 0, math.ceil(end / bar) * bar, piece.events, PIANO_COLOURS[key])
    save_piece(piece.slug)


# ---- 19: the studio effects ----------------------------------------------------------------------
# "Pas de cinq" through the four studio effects, each from a factory preset:
# a hall on the piano, a plate and a gentle compressor on the saxophone, the
# bass held by the compressor, the brushes swirling through the filter, and
# the mix limited on the master for streaming.
def example_studio_effects():
    request({"cmd": "open_project", "path": str(TEMPLATES / "14-pas-de-cinq.cydaw")})
    request({"cmd": "set_master_gain", "gainDb": -60, "muted": True})
    for track in ("Saxophone alto", "Piano", "Contrebasse", "Balais"):
        request({"cmd": "sampler_status", "track": track, "wait": 120})

    def add(track, key, preset, values=None):
        answer = request({"cmd": "insert_device", "track": track, "key": key})
        # The card just put in: the last of its kind.
        node = [item["id"] for item in answer.get("nodes", []) if item.get("type") == key][-1]
        request({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": preset})
        for suffix, value in (values or {}).items():
            request({"cmd": "set_parameter", "track": track, "key": "node.%s.%s" % (node, suffix), "value": value})
        return node

    add("Saxophone alto", "comp-pro", "Gentle Vocal")
    add("Saxophone alto", "reverb-pro", "Bright Plate", {"revpro.mix": 0.18})
    add("Piano", "reverb-pro", "Large Hall", {"revpro.mix": 0.2})
    add("Contrebasse", "comp-pro", "Steady Bass")
    add("Balais", "filter-pro", "Stereo Swirl", {"filtpro.mix": 0.45, "filtpro.f1Cutoff": 3_000.0})
    # Its gain brought down until the integrated loudness reads -14 LUFS.
    add("master", "limiter-pro", "Streaming -14 LUFS", {"limpro.gain": 1.5})
    save_piece("19-effets-studio")


# ---- 21: Kick and the CyDAW Electro kits ------------------------------------------------------


def kit_key(kit_folder, sound):
    """The key an electro kit plays one of its sounds on ("Hats/909 Open Hat")."""
    path = ROOT / "content" / "factory" / "Instruments" / "Electro" / kit_folder / (kit_folder + ".cysampler")
    for zone in json.loads(path.read_text(encoding="utf-8"))["zones"]:
        if zone["file"].endswith("/" + sound + ".wav"):
            return zone["keys"][0]
    raise RuntimeError("%s plays no %s" % (kit_folder, sound))


def kick_track(name, preset, values=None, gain_db=0.0):
    track = request({"cmd": "add_midi_track", "name": name, "instrument": "kick-instrument"})["name"]
    node = request({"cmd": "kick", "track": track})["nodeId"]
    request({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": preset})
    if values:
        request({"cmd": "kick", "track": track, "set": values})
    if gain_db:
        request({"cmd": "set_track", "track": track, "gainDb": gain_db})
    return track


def example_electro():
    """Kick and the CyDAW Electro kits in a techno piece, A minor, 126 BPM: the
    groove (bars 1-8), the rolling bass (9-16), a breakdown with the pad and a
    riser (17-24), the drop (25-32). The kick is tuned to the key: it ends on
    A1 (55 Hz). The kits' sounds are made by CyDAW when the project opens."""
    new_piece()
    request({"cmd": "set_tempo", "bpm": 126})
    kick = kick_track("Kick", "Techno Hard", {"pitchEnd": 55}, -3.0)
    drums = factory_track("electro-909", gain_db=-5.0)
    request({"cmd": "set_track", "track": drums, "name": "Batterie 909"})
    drums = "Batterie 909"
    perc = factory_track("electro-perc-fx", gain_db=-8.0)
    request({"cmd": "set_track", "track": perc, "name": "Percussions et effets"})
    perc = "Percussions et effets"
    bass = prism_track("Basse roulante", "Reese Bass", -9.0)
    pad = prism_track("Nappe", "Evolving Pad", -13.0)
    kit = lambda sound: kit_key("Electro 909", sound)  # noqa: E731
    fx = lambda sound: kit_key("Electro Percussion and FX", sound)  # noqa: E731
    groove = list(range(0, 16)) + list(range(24, 32))
    bars = lambda first, count: [(first + bar, PROGRESSION[(first + bar) % 4]) for bar in range(count)]  # noqa: E731

    # The kick: four on the floor, a fill into the breakdown, a build into the drop.
    events = []
    for bar in groove:
        events += [(36, bar * 4 + beat, 0.25, 0.95 if beat == 0 else 0.86) for beat in range(4)]
    events += [(36, 15 * 4 + 3.5, 0.2, 0.6), (36, 15 * 4 + 3.75, 0.2, 0.7)]
    events += [(36, 22 * 4 + step, 0.2, 0.35 + 0.06 * step) for step in range(8)]
    events += [(36, 23 * 4 + step * 0.5, 0.15, 0.5 + 0.06 * step) for step in range(8)]
    clip(kick, "Kick", 0, 128, events, "#ff5a5a")

    # The 909: closed hats in sixteenths, open hats on the off-beats, the clap,
    # the rim's syncopation, the ride in the drop, a crash at each entry.
    events = []
    for bar in range(32):
        at = bar * 4
        if bar in groove:
            events += [(kit("Hats/909 Closed Hat"), at + step * 0.25, 0.1, 0.62 if step % 4 == 2 else 0.4)
                       for step in range(16)]
        elif bar >= 20:
            events += [(kit("Hats/909 Closed Hat"), at + step * 0.5, 0.1, 0.3 + 0.03 * (bar - 20)) for step in range(8)]
        if bar >= 8 and bar in groove:
            events += [(kit("Hats/909 Open Hat"), at + beat + 0.5, 0.2, 0.62) for beat in range(4)]
        if (4 <= bar < 16) or bar >= 20:
            events += [(kit("Claps/909 Clap"), at + beat, 0.2, 0.8 if bar in groove else 0.55) for beat in (1, 3)]
        if 8 <= bar < 16 or bar >= 24:
            events += [(kit("Percussion/909 Rim"), at + offset, 0.1, 0.55) for offset in (0.75, 2.25, 3.5)]
        if bar >= 24:
            events += [(kit("Cymbals/Ride"), at + beat, 0.3, 0.42) for beat in range(4)]
    events += [(kit("Cymbals/Crash"), 8 * 4, 1.0, 0.75), (kit("Cymbals/Crash"), 24 * 4, 1.0, 0.85)]
    clip(drums, "909", 0, 128, events, "#ffb05a")

    # Percussion and effects: the shaker and congas, a downlifter into the
    # breakdown, a riser and an impact into the drop.
    events = []
    for bar in range(8, 32):
        if bar in groove:
            events += [(fx("Percussion/Shaker"), bar * 4 + step * 0.25, 0.1, 0.5 if step % 2 else 0.32) for step in range(16)]
        if bar >= 24:
            events += [(fx("Percussion/808 Conga High"), bar * 4 + 1.75, 0.2, 0.6),
                       (fx("Percussion/808 Conga Mid"), bar * 4 + 2.5, 0.2, 0.55),
                       (fx("Percussion/808 Conga Low"), bar * 4 + 3.25, 0.2, 0.6)]
    riser_beats = 4.0 * 126 / 60
    events += [(fx("FX/Downlifter"), 16 * 4, 4.0, 0.7), (fx("FX/Riser 4s"), 24 * 4 - riser_beats, riser_beats, 0.75),
               (fx("FX/Impact"), 24 * 4, 4.0, 0.8)]
    clip(perc, "Percussions", 0, 128, events, "#6ad1a9")

    # The rolling bass: the three sixteenths between the kicks, on the root.
    events = []
    for bar, (root, minor) in bars(8, 8) + bars(24, 8):
        base = near(root, 33)
        for beat in range(4):
            for step, velocity in ((1, 0.7), (2, 0.82), (3, 0.7)):
                events.append((base + (12 if (beat, step) == (3, 3) else 0), bar * 4 + beat + step * 0.25, 0.2, velocity))
    clip(bass, "Basse", 0, 128, [event for event in events], "#b07d62")

    # The pad: the chords held through the breakdown and the drop.
    events = []
    for bar, (root, minor) in bars(16, 16):
        events += [(tone, bar * 4, 3.95, 0.55) for tone in chord_tones(root, minor, 57, 70)]
    clip(pad, "Accords", 0, 128, events, "#c49cff")
    save_piece("21-kick-et-batteries-electro")


# ---- 22: amps, cabinets and microphones --------------------------------------------------------


def chain_device(track, key, preset, values=None):
    """A device after the track's others, from one of its factory presets."""
    answer = request({"cmd": "insert_device", "track": track, "key": key})
    node = [item["id"] for item in answer.get("nodes", []) if item.get("type") == key][-1]
    request({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": preset})
    for suffix, value in (values or {}).items():
        request({"cmd": "set_parameter", "track": track, "key": "node.%s.%s" % (node, suffix), "value": value})
    return node


def example_amps():
    """The amp, cabinet and microphone simulators on sampled instruments, an
    original rock piece in A minor, 110 BPM: a clean guitar and a voice through
    an old radio (bars 1-4), the crunch rhythm guitar, the bass through its
    amp and the drums (5-12), a singing lead (13-20), the clean guitar again
    and the voice on the telephone (21-24)."""
    new_piece()
    request({"cmd": "set_tempo", "bpm": 110})
    clean = factory_track("guitar-hofner", gain_db=-4.0)
    request({"cmd": "set_track", "track": clean, "name": "Guitare clean"})
    clean = "Guitare clean"
    chain_device(clean, "amp-sim", "Bright Clean")
    chain_device(clean, "cab-sim", "Clean 2x12 Combo")
    rhythm = factory_track("guitar-gretsch", gain_db=-10.0)
    request({"cmd": "set_track", "track": rhythm, "name": "Guitare crunch"})
    rhythm = "Guitare crunch"
    chain_device(rhythm, "amp-sim", "Rock Crunch")
    chain_device(rhythm, "cab-sim", "Classic 4x12")
    lead = factory_track("guitar-epiphone", gain_db=-10.0)
    request({"cmd": "set_track", "track": lead, "name": "Guitare solo"})
    lead = "Guitare solo"
    chain_device(lead, "amp-sim", "Singing Solo")
    chain_device(lead, "cab-sim", "4x12 Dynamic and Ribbon")
    chain_device(lead, "reverb-pro", "Large Hall", {"revpro.mix": 0.18})
    bass = factory_track("bass-jazz", gain_db=-8.0)
    request({"cmd": "set_track", "track": bass, "name": "Basse"})
    bass = "Basse"
    chain_device(bass, "amp-sim", "Round Bass")
    chain_device(bass, "cab-sim", "Bass 8x10")
    voice = factory_track("voice-a", gain_db=-6.0)
    request({"cmd": "set_track", "track": voice, "name": "Voix radio"})
    voice = "Voix radio"
    chain_device(voice, "mic-sim", "Old Radio")
    phone = factory_track("voice-a", gain_db=-4.0)
    request({"cmd": "set_track", "track": phone, "name": "Voix téléphone"})
    phone = "Voix téléphone"
    chain_device(phone, "mic-sim", "Telephone")
    drums = factory_track("drums-seventies", gain_db=-6.0)
    request({"cmd": "set_track", "track": drums, "name": "Batterie"})
    drums = "Batterie"
    bars = lambda first, count: [(first + bar, PROGRESSION[(first + bar) % 4]) for bar in range(count)]  # noqa: E731

    # The clean guitar: the chord broken in eighths, bars 1-4 and 21-24.
    events = []
    for bar, (root, minor) in bars(0, 4) + bars(20, 4):
        tones = chord_tones(root, minor, 52, 67)
        for step, index in enumerate((0, 1, 2, 1, 0, 2, 1, 2)):
            events.append((tones[index] + (12 if step == 5 else 0), bar * 4 + step * 0.5, 0.9, 0.6 + (0.1 if step % 4 == 0 else 0.0)))
    clip(clean, "Arpèges", 0, 96, events, "#8fc8ff")
    # The rhythm guitar: power chords in eighths, the first of each beat
    # longer, bars 5-12.
    events = []
    for bar, (root, minor) in bars(4, 8):
        base = near(root, 40)
        for step in range(8):
            length = 0.45 if step % 2 == 0 else 0.3
            velocity = 0.85 if step % 2 == 0 else 0.7
            for offset in (0, 7, 12):
                events.append((base + offset, bar * 4 + step * 0.5, length, velocity))
    clip(rhythm, "Rythmique", 0, 96, events, "#e0a458")
    # The lead: an A minor pentatonic line, bars 13-20.
    line = [(69, 0, 1.0), (72, 1, 0.5), (74, 1.5, 0.5), (76, 2, 1.5), (74, 3.5, 0.5), (72, 4, 1.0), (69, 5, 1.0),
            (67, 6, 2.0), (69, 8, 0.5), (72, 8.5, 0.5), (74, 9, 1.0), (76, 10, 0.5), (79, 10.5, 0.5), (81, 11, 3.0),
            (79, 14, 1.0), (76, 15, 1.0), (74, 16, 1.5), (76, 17.5, 0.5), (74, 18, 0.5), (72, 18.5, 0.5), (69, 19, 1.0),
            (72, 20, 1.0), (74, 21, 1.0), (76, 22, 2.0), (72, 24, 0.5), (74, 24.5, 0.5), (72, 25, 1.0), (69, 26, 2.0),
            (67, 28, 1.0), (69, 29, 3.0)]
    clip(lead, "Solo", 0, 96, [(note, 48 + at, length - 0.05, 0.85) for note, at, length in line], "#d4776b")
    # The bass: the root in eighths, the fifth on the last, bars 5-20.
    events = []
    for bar, (root, minor) in bars(4, 16):
        base = near(root, 28)
        for step in range(8):
            events.append((base + (7 if step == 7 else 0), bar * 4 + step * 0.5, 0.45, 0.8 if step % 2 == 0 else 0.65))
    clip(bass, "Basse", 0, 96, events, "#b07d62")
    # The voice: long "a"s on the chords' tones, through the old radio, then
    # on the telephone.
    sung = lambda first: [(chord_tones(root, minor, 57, 69)[2], bar * 4, 3.8, 0.7) for bar, (root, minor) in bars(first, 4)]  # noqa: E731
    clip(voice, "Voix", 0, 96, sung(0), "#c9e27a")
    clip(phone, "Voix", 0, 96, sung(20), "#9fd27a")
    # The drums: kick and snare, hats in eighths, bars 5-20.
    events = []
    for bar in range(4, 20):
        at = bar * 4
        events += [(36, at, 0.2, 0.9), (36, at + 2.5, 0.2, 0.75), (38, at + 1, 0.2, 0.85), (38, at + 3, 0.2, 0.85)]
        events += [(42, at + step * 0.5, 0.1, 0.55 + (0.15 if step % 2 == 0 else 0.0)) for step in range(8)]
        if bar % 4 == 3:
            events += [(38, at + 3.5, 0.1, 0.6), (38, at + 3.75, 0.1, 0.7)]
    clip(drums, "Batterie", 0, 96, events, "#a89f91")
    save_piece("22-amplis-et-micros")


# ---- 23: the native instruments and effects ----------------------------------------------------
# D minor: Dm, Bb, F, C, a bar each; 100 BPM.
NATIVE = [(2, True), (10, False), (5, False), (0, False)]


def synth_track(name, instrument, model, preset, gain_db=0.0):
    """A track playing one of CyDAW's synths, from a factory sound."""
    track = request({"cmd": "add_midi_track", "name": name, "instrument": instrument})["name"]
    node = request({"cmd": "synth", "track": track, "model": model})["nodeId"]
    request({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": preset})
    if gain_db:
        request({"cmd": "set_track", "track": track, "gainDb": gain_db})
    return track


def example_native():
    """CyDAW's own instruments and effects in one piece, D minor, 100 BPM:
    the Nébuleuse pad worn by Patine and the Mosaïque bells through Écho Bande
    and Halo (bars 1-8); Batterie's pop groove pressed by Pression, the
    Colosse bass heated by Fournaise and the Ambre chords shaped by Tranche
    and Velours (9-16); the Prisme lead and a Kick under the drums (17-24);
    the pad again over a half-time groove (25-32). On the master: Multibande
    Pro, Finition, Sonomètre."""
    new_piece()
    request({"cmd": "set_tempo", "bpm": 100})
    bars = lambda first, count: [(first + bar, NATIVE[(first + bar) % 4]) for bar in range(count)]  # noqa: E731

    # ---- The tracks and their devices.
    pad = synth_track("Nappe Nébuleuse", "nebuleuse-instrument", "nebuleuse", "Chord Cloud", -18.0)
    chain_device(pad, "patine", "Worn Cassette")
    chain_device(pad, "halo", "Endless Space")
    bells = synth_track("Cloches Mosaïque", "mosaique-instrument", "mosaique", "Glass Bells", -3.0)
    chain_device(bells, "echo-bande", "Dotted Quarter")
    chain_device(bells, "halo", "Concert Hall")
    drums = request({"cmd": "add_midi_track", "name": "Batterie", "instrument": "batterie-instrument"})["name"]
    drum_node = request({"cmd": "batterie", "track": drums})["nodeId"]
    request({"cmd": "presets", "track": drums, "nodeId": drum_node, "action": "load", "name": "Classic Pop Kit"})
    request({"cmd": "set_track", "track": drums, "gainDb": -3.0})
    chain_device(drums, "pression", "Crushed Drums")
    chain_device(drums, "fournaise", "Pushed Drums")
    kick = kick_track("Kick", "Sub Kick", None, -11.0)
    chain_device(kick, "tranche", "Kick Drum")
    bass = synth_track("Basse Colosse", "colosse-instrument", "colosse", "Colossal Bass", 0.0)
    chain_device(bass, "fournaise", "Fat Bass")
    chain_device(bass, "tranche", "Round Bass")
    keys = synth_track("Accords Ambre", "ambre-instrument", "ambre", "Poly Keys", -7.0)
    chain_device(keys, "tranche", "Bright Synth")
    chain_device(keys, "velours", "Hard and Precise")
    lead = prism_track("Lead Prisme", "Supersaw Lead", -7.0)
    chain_device(lead, "echo-bande", "Eighth Notes")
    chain_device(lead, "halo", "Vintage Plate")
    chain_device("master", "multiband-pro", "Gentle Mastering")
    chain_device("master", "finition", "Balanced Streaming")
    chain_device("master", "sonometre", "Streaming -14 LUFS")

    # ---- The pad: the chords held, bars 1-8 and 25-32.
    events = []
    for bar, (root, minor) in bars(0, 8) + bars(24, 8):
        events += [(tone, bar * 4, 3.95, 0.6) for tone in chord_tones(root, minor, 50, 65)]
    clip(pad, "Accords", 0, 128, events, "#c49cff")

    # ---- The bells: the chord broken in eighths, high, bars 1-8 and 17-24.
    events = []
    for bar, (root, minor) in bars(0, 8) + bars(16, 8):
        tones = chord_tones(root, minor, 69, 82)
        for step, index in enumerate((0, 1, 2, 1, 2, 0, 1, 2)):
            events.append((tones[index] + (12 if step == 4 else 0), bar * 4 + step * 0.5, 0.45,
                           0.62 if step % 4 == 0 else 0.5))
    clip(bells, "Cloches", 0, 128, events, "#8fd3ff")

    # ---- Batterie: the pop groove (a snare fill into the lead, a tom fill
    # into the last part), then half time.
    request({"cmd": "groove", "track": drums, "name": "pop", "bars": 8, "fill": "fill-snare", "startBeats": 32})
    request({"cmd": "groove", "track": drums, "name": "pop", "bars": 8, "fill": "fill-toms", "startBeats": 64})
    request({"cmd": "groove", "track": drums, "name": "ballade", "bars": 4, "startBeats": 96})

    # ---- Kick: under the drums on each beat's start of the lead, bars 17-24.
    events = []
    for bar in range(16, 24):
        events += [(36, bar * 4 + beat, 0.3, 0.9 if beat == 0 else 0.8) for beat in (0, 2)]
    clip(kick, "Kick", 0, 128, events, "#ff5a5a")

    # ---- The bass: the root in eighths, its octave and fifth, bars 9-28.
    events = []
    for bar, (root, minor) in bars(8, 20):
        base = near(root, 33)
        for at, offset, length, velocity in ((0, 0, 0.9, 0.85), (1, 0, 0.4, 0.7), (1.5, 12, 0.4, 0.75),
                                             (2, 0, 0.9, 0.8), (3, 7, 0.4, 0.7), (3.5, 0, 0.4, 0.72)):
            events.append((base + offset, bar * 4 + at, length, velocity))
    clip(bass, "Basse", 0, 128, events, "#b07d62")

    # ---- The chords: offbeat stabs, bars 9-24.
    events = []
    for bar, (root, minor) in bars(8, 16):
        for at in (0.5, 1.5, 2.5, 3.0):
            events += [(tone, bar * 4 + at, 0.35, 0.62 if at == 0.5 else 0.55) for tone in chord_tones(root, minor, 57, 70)]
    clip(keys, "Stabs", 0, 128, events, "#ffd27a")

    # ---- The lead: a line over two rounds of the chords, bars 17-24.
    line = [(69, 0, 1.5), (65, 1.5, 0.5), (69, 2, 1.0), (74, 3, 1.0),
            (74, 4, 1.5), (72, 5.5, 0.5), (70, 6, 1.0), (69, 7, 1.0),
            (69, 8, 1.0), (72, 9, 1.0), (77, 10, 1.5), (76, 11.5, 0.5),
            (76, 12, 2.0), (74, 14, 1.0), (72, 15, 1.0),
            (77, 16, 1.5), (76, 17.5, 0.5), (74, 18, 1.0), (69, 19, 1.0),
            (70, 20, 1.0), (74, 21, 1.0), (77, 22, 1.0), (74, 23, 1.0),
            (72, 24, 1.5), (69, 25.5, 0.5), (72, 26, 1.0), (77, 27, 1.0),
            (76, 28, 1.0), (72, 29, 1.0), (74, 30, 2.0)]
    clip(lead, "Mélodie", 0, 128, [(note, 64 + at, length - 0.06, 0.8) for note, at, length in line], "#ff8fb1")
    save_piece("23-instruments-et-effets-natifs")


def catalogue_effect(track, key):
    """An effect after the track's others, from its first factory sound."""
    answer = request({"cmd": "insert_device", "track": track, "key": key})
    nodes = [item["id"] for item in answer.get("nodes", []) if item.get("type") == key]
    if not nodes:
        return
    listed = request({"cmd": "presets", "track": track, "nodeId": nodes[-1]}).get("presets", [])
    factory = [item["name"] for item in listed if item.get("factory") and item["name"] not in ("Init", "Default")]
    if factory:
        request({"cmd": "presets", "track": track, "nodeId": nodes[-1], "action": "load", "name": factory[0]})


# Every instrument on its own track, each with one or two effects, and between
# them every effect: (track name, how it is made, its sound, its effects).
CATALOGUE = [
    ("Opale", ("synth", "opale"), "Warm Keys", ["accords", "chorus", "reverb-pro"]),
    ("Quartz", ("synth", "quartz"), "Electric Piano", ["arpege", "phaser", "echo-bande"]),
    ("Saphir", ("synth", "saphir"), "Classic E-Piano", ["ping-pong", "eq-pro"]),
    ("Tirette", ("synth", "tirette"), "Full Organ", ["fournaise", "halo"]),
    ("Onyx", ("synth", "onyx"), "Vocal Pad", ["flanger", "filter-pro", "vocoder"]),
    ("Alliage", ("synth", "alliage"), "Acid Line", ["sequenceur", "auto-filter", "comp-pro"]),
    ("Argile", ("synth", "argile"), "Brass Section", ["multimode-filter", "delay", "decaleur"]),
    ("Dérive", ("synth", "derive"), "Saturated Lead", ["gamme", "amp-sim", "cab-sim"]),
    ("Bronze", ("synth", "bronze"), "Marimba", ["reverb", "eq-three", "resonateurs"]),
    ("Jade", ("synth", "jade"), "Suitcase Auto-Pan", ["rotor", "reverb-pro"]),
    ("Silex", ("synth", "silex"), "House Kit", ["roulement", "saccade", "pompe"]),
    ("Ambre", ("synth", "ambre"), "Brass", ["velours", "limiter-pro", "harmonie"]),
    ("Colosse", ("synth", "colosse"), "Abyssal Pad", ["pression", "multiband-pro", "anneau"]),
    ("Mosaïque", ("synth", "mosaique"), "Bowed Cello", ["tranche", "finition", "corps"]),
    ("Nébuleuse", ("synth", "nebuleuse"), "Cathedral Choir", ["stereo-width", "givre"]),
    ("Prisme", ("instrument", "prism-instrument", "prism-synth"), "Digital Marimba", ["echo-notes", "patine", "drive"]),
    ("Kick", ("instrument", "kick-instrument", "kick-synth"), "909 Punch", ["transient-shaper", "limiter"]),
    ("Batterie", ("batterie",), "Classic Pop Kit", ["compressor", "gate"]),
    ("Rack de pads", ("instrument", "rack-instrument", None), None, ["peigne", "utilitaire"]),
    ("Échantillonneur simple", ("simple",), None, ["bitcrusher", "lowpass"]),
    ("Nuée", ("nuee",), None, ["grains", "boucleur"]),
    ("Sampler", ("factory", "piano"), None, ["alea", "duree", "eq", "tremolo-pan", "empreinte"]),
    ("Modèle de son", ("model", "Alto Saxophone.cysound"), None, ["justesse", "de-esser", "tuner", "mic-sim", "highpass"]),
    ("Poly Keys", ("instrument", "synth-keys", None), None, ["hard-clip", "chorus"]),
]


def example_catalogue():
    """'24-catalogue-instruments-et-effets': CyDAW's instruments one per track,
    each with a sound of its own and one or two effects, between them every
    effect; the master's analysers. No clips: a project to look through."""
    new_piece()
    request({"cmd": "set_tempo", "bpm": 120})
    for name, how, sound, effects in CATALOGUE:
        kind = how[0]
        if kind == "synth":
            track = synth_track(name, how[1] + "-instrument", how[1], sound)
        elif kind == "instrument":
            track = request({"cmd": "add_midi_track", "name": name, "instrument": how[1]})["name"]
            if sound and how[2]:
                graph = request({"cmd": "get_track_graph", "track": track})
                node = [item["id"] for item in graph["nodes"] if item.get("type") == how[2]][0]
                request({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": sound})
        elif kind == "batterie":
            track = request({"cmd": "add_midi_track", "name": name, "instrument": "batterie-instrument"})["name"]
            graph = request({"cmd": "get_track_graph", "track": track})
            node = [item["id"] for item in graph["nodes"] if item.get("type") == "batterie"][0]
            request({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": sound})
        elif kind in ("simple", "nuee"):
            instrument = "simple-sampler-instrument" if kind == "simple" else "nuee-instrument"
            track = request({"cmd": "add_midi_track", "name": name, "instrument": instrument})["name"]
            sample = pathlib.Path("D:/CyDAW/Banks/bear-sax/Samples/ab3_vl3_rr2.wav")
            if sample.exists():
                request({"cmd": "load_sampler", "track": track, "file": str(sample).replace(os.sep, "/")})
        elif kind == "factory":
            listed = request({"cmd": "factory_instruments"}).get("instruments", [])
            chosen = next((item["key"] for item in listed if how[1] in item["key"] and item.get("installed", True)), None)
            if chosen is None:
                print("skip", name, "(no factory instrument with", how[1], ")")
                continue
            made = request({"cmd": "add_factory_instrument", "key": chosen})
            track = made.get("name") or made.get("track")
            request({"cmd": "set_track", "track": track, "name": name})
            track = name
        else:
            model = ROOT / "content" / "factory" / "Sound Models" / how[1]
            made = request({"cmd": "load_sound_model", "file": str(model).replace(os.sep, "/")})
            track = made.get("name") or made.get("track")
            request({"cmd": "set_track", "track": track, "name": name})
            track = name
        for effect in effects:
            catalogue_effect(track, effect)
        print("track", track, effects)
    for effect in ("spectrometer", "sonometre", "stereoscope"):
        request({"cmd": "insert_device", "track": "master", "key": effect})
    save_piece("24-catalogue-instruments-et-effets")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(DEFAULT_EXE))
    parser.add_argument("--only", default="",
                        help="acoustic: only the factory instruments' comparison; all-instruments: only the project "
                             "with every factory instrument; prism: only Prisme's piece; jazz: only 'Pas de cinq'; "
                             "pianos (or luna, greve, neons, cycles): the solo piano pieces; studio: 'Pas de cinq' "
                             "through the studio effects; electro: Kick and the CyDAW Electro kits; amps: the amp, cabinet and microphone simulators; "
                             "native: CyDAW's own synths, Batterie and mixing effects; catalogue: every instrument on its "
                             "own track with one or two effects")
    arguments = parser.parse_args()
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1")
    process = subprocess.Popen([arguments.exe, "--control-port=%d" % PORT, "--silent", "--window-size=1400x900"],
                               cwd=os.path.dirname(arguments.exe), env=environment)
    try:
        for _ in range(150):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        request({"cmd": "set_master_gain", "gainDb": -60})
        if arguments.only == "all-instruments":
            example_all_instruments()
            return 0
        if arguments.only == "prism":
            example_prism()
            return 0
        if arguments.only == "jazz":
            example_jazz5()
            return 0
        if arguments.only == "studio":
            example_studio_effects()
            return 0
        if arguments.only == "electro":
            example_electro()
            return 0
        if arguments.only == "amps":
            example_amps()
            return 0
        if arguments.only == "native":
            example_native()
            return 0
        if arguments.only == "catalogue":
            example_catalogue()
            return 0
        if arguments.only in ("pianos", "luna", "greve", "neons", "cycles"):
            for key in (("luna", "greve", "neons", "cycles") if arguments.only == "pianos" else (arguments.only,)):
                example_piano(key)
            return 0
        if arguments.only != "acoustic":
            example_equaliser()
            example_echoes()
            example_dynamics()
            example_compressor()
        example_comparison()
        example_prism()
        example_jazz5()
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:
            process.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
