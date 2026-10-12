"""Build CyDAW's factory instruments from the downloaded recordings.

    python tools/factory_content/build_factory_content.py --tool <cydaw_sample_tool.exe>
           [--sources DIR] [--out content/factory] [--only key,key] [--samples bank|flac]

The recordings come from fetch_banks.py (CC0 banks by Versilian Studios; manifest.json lists
the notes chosen). For every instrument below:
  - each note is measured by cydaw_sample_tool: where its sound starts and where it has faded
    (--samples bank, the default: the zone plays the bank's recording between the two,
    "bank:<id>/<path>", faded out at the end; CyDAW downloads the recordings, Factory ships
    only the maps), or it is converted, cut and faded, into a 16-bit FLAC in
    <out>/Instruments/<folder>/ (--samples flac, the samples shipped);
  - its pitch is measured (YIN, the companion's) to give the zone its root (the bank's note
    name, in the octave the sound has: banks number octaves differently) and its tuning;
  - the velocity layers of a note share the velocity range, softest first; round robins and
    loops are set where the instrument has them (a bowed violin is looped where its sound
    repeats best);
  - its level is set so that its loudest layer peaks near TARGET_PEAK_DB (the median over its
    notes), the layers keeping their differences: the instruments sit together in a mix;
  - the map is written as <folder>.cysampler, and the instrument is listed in
    <out>/catalogue.json with the card settings and envelope it was made for;
  - with --samples bank, <out>/Instruments/legacy.json maps the FLAC files earlier versions
    shipped to the recordings and the frames they were cut at (older projects play on).
Then the instruments made from others (DERIVED): a honky-tonk upright (each note twice, detuned
apart), a felt piano (the soft layers, darker). They play the same files.
<out>/LICENCE.txt credits the sources.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile
import textwrap
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "companion"))

from cydaw_companion.audio_io import mono, read_wav  # noqa: E402
from cydaw_companion.pitch import hz_to_midi, yin  # noqa: E402
from fetch_banks import default_destination  # noqa: E402
import factory_voicing  # noqa: E402

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
# Where an instrument's loudest layer peaks (dBFS, the median over its notes).
TARGET_PEAK_DB = -6.0
DYNAMICS = {"ppp": 1, "pp": 2, "p": 3, "mp": 4, "mf": 5, "f": 6, "ff": 7, "fff": 8,
            "soft": 2.5, "med": 4.5, "medium": 4.5, "loud": 7}


def note_name(note: int) -> str:
    return f"{NOTE_NAMES[note % 12]}{note // 12 - 1}"


# Each instrument: the downloaded set it comes from ("source", fetch_sources.py's key), which of
# its layers it takes ("layers": None for all; a dict picks some and names the instrument after
# them), its names, its place in the catalogue, how it is tuned ("note": every note to equal
# temperament; "global": only the instrument's mean, a piano keeps its stretch), and the card
# settings and envelope it plays with.
INSTRUMENTS = [
    dict(key="grand-steinway", source="grand-steinway", folder="Steinway B Grand",
         names={"fr": "Piano à queue Steinway B", "en": "Steinway B grand piano"}, category="pianos",
         tuning="global", settings={"velocity-db": 30}, envelope={"attack": 1, "release": 900}),
    dict(key="grand-kawai", source="grand-kawai", folder="Kawai Grand",
         names={"fr": "Piano à queue Kawai", "en": "Kawai grand piano"}, category="pianos",
         tuning="global", settings={"velocity-db": 18}, envelope={"attack": 1, "release": 900}),
    dict(key="upright-yamaha", source="upright-yamaha", folder="Yamaha Upright",
         names={"fr": "Piano droit Yamaha", "en": "Yamaha upright piano"}, category="pianos",
         tuning="global", settings={"velocity-db": 18}, envelope={"attack": 1, "release": 700}),
    dict(key="upright-knight", source="upright-knight", folder="Knight Upright",
         names={"fr": "Piano droit Knight", "en": "Knight upright piano"}, category="pianos",
         tuning="global", settings={"velocity-db": 18}, envelope={"attack": 1, "release": 700}),
    dict(key="fm-piano", source="fm-piano", folder="FM Piano",
         names={"fr": "Piano FM (TX81Z)", "en": "FM piano (TX81Z)"}, category="electric-pianos",
         tuning="note", settings={"velocity-db": 18}, envelope={"attack": 1, "release": 300}),
    dict(key="harpsichord-flemish-1", source="harpsichord-flemish", layers={"Low": None}, folder="Flemish Harpsichord I",
         names={"fr": "Clavecin flamand I", "en": "Flemish harpsichord I"}, category="harpsichords",
         tuning="note", settings={"velocity-db": 4}, envelope={"attack": 1, "release": 260}),
    dict(key="harpsichord-flemish-2", source="harpsichord-flemish", layers={"High": None}, folder="Flemish Harpsichord II",
         names={"fr": "Clavecin flamand II", "en": "Flemish harpsichord II"}, category="harpsichords",
         tuning="note", settings={"velocity-db": 4}, envelope={"attack": 1, "release": 260}),
    dict(key="harpsichord-french", source="harpsichord-french", folder="French Harpsichord",
         names={"fr": "Clavecin français", "en": "French harpsichord"}, category="harpsichords",
         tuning="note", settings={"velocity-db": 4}, envelope={"attack": 1, "release": 260}),
    dict(key="harpsichord-italian", source="harpsichord-italian", folder="Italian Harpsichord",
         names={"fr": "Clavecin italien", "en": "Italian harpsichord"}, category="harpsichords",
         tuning="note", settings={"velocity-db": 4}, envelope={"attack": 1, "release": 260}),
    dict(key="harpsichord-english", source="harpsichord-english", layers={"Normal": None}, folder="English Harpsichord",
         names={"fr": "Clavecin anglais", "en": "English harpsichord"}, category="harpsichords",
         tuning="note", settings={"velocity-db": 4}, envelope={"attack": 1, "release": 260}),
    dict(key="harpsichord-english-lute", source="harpsichord-english", layers={"Lute": None},
         folder="English Harpsichord Lute",
         names={"fr": "Clavecin anglais, jeu de luth", "en": "English harpsichord, lute stop"}, category="harpsichords",
         tuning="note", settings={"velocity-db": 4}, envelope={"attack": 1, "release": 200}),
    dict(key="harp-concert", source="harp-concert", folder="Concert Harp",
         names={"fr": "Harpe de concert", "en": "Concert harp"}, category="harps",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 2500}),
    dict(key="harp-folk", source="harp-folk", folder="Folk Harp",
         names={"fr": "Harpe celtique", "en": "Folk harp"}, category="harps",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 2200}),
    dict(key="violin-arco", source="violin-arco", folder="Violin Arco",
         names={"fr": "Violon (archet, vibrato)", "en": "Violin (bowed, vibrato)"}, category="strings",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 4, "release": 320}),
    dict(key="violin-pizz", source="violin-pizz", folder="Violin Pizzicato",
         names={"fr": "Violon pizzicato", "en": "Violin pizzicato"}, category="strings",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 400}),
    # The orchestra (VSCO 2 CE).
    dict(key="violin-section", source="violin-section", folder="Violin Section",
         names={"fr": "Violons (section)", "en": "Violin section"}, category="string-sections",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 10, "release": 360}),
    dict(key="viola-section", source="viola-section", folder="Viola Section",
         names={"fr": "Altos (section)", "en": "Viola section"}, category="string-sections",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 10, "release": 360}),
    dict(key="cello-section", source="cello-section", folder="Cello Section",
         names={"fr": "Violoncelles (section)", "en": "Cello section"}, category="string-sections",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 10, "release": 380}),
    dict(key="contrabass-arco", source="contrabass-arco", folder="Double Bass Arco",
         names={"fr": "Contrebasse (archet)", "en": "Double bass (bowed)"}, category="strings",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 10, "release": 380}),
    dict(key="violin-section-pizz", source="violin-section-pizz", folder="Violin Section Pizzicato",
         names={"fr": "Violons pizzicato (section)", "en": "Violin section pizzicato"}, category="string-sections",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 400}),
    dict(key="cello-section-pizz", source="cello-section-pizz", folder="Cello Section Pizzicato",
         names={"fr": "Violoncelles pizzicato (section)", "en": "Cello section pizzicato"}, category="string-sections",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 500}),
    dict(key="contrabass-pizz", source="contrabass-pizz", folder="Double Bass Pizzicato",
         names={"fr": "Contrebasse pizzicato", "en": "Double bass pizzicato"}, category="strings",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 600}),
    dict(key="horn", source="horn", folder="French Horn",
         names={"fr": "Cor en fa", "en": "French horn"}, category="brass",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 12, "release": 300}),
    dict(key="trumpet", source="trumpet", folder="Trumpet",
         names={"fr": "Trompette", "en": "Trumpet"}, category="brass",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 250}),
    dict(key="trombone", source="trombone", folder="Tenor Trombone",
         names={"fr": "Trombone ténor", "en": "Tenor trombone"}, category="brass",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 8, "release": 280}),
    dict(key="tuba", source="tuba", folder="Tuba",
         names={"fr": "Tuba", "en": "Tuba"}, category="brass",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 10, "release": 300}),
    dict(key="flute", source="flute", folder="Flute",
         names={"fr": "Flûte", "en": "Flute"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 8, "release": 260}),
    dict(key="oboe", source="oboe", folder="Oboe",
         names={"fr": "Hautbois", "en": "Oboe"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 240}),
    dict(key="clarinet", source="clarinet", folder="Clarinet",
         names={"fr": "Clarinette", "en": "Clarinet"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 240}),
    dict(key="bassoon", source="bassoon", folder="Bassoon",
         names={"fr": "Basson", "en": "Bassoon"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 8, "release": 260}),
    # VCSL: organs, winds, tuned percussion, zithers, FM.
    dict(key="pipe-organ", source="pipe-organ", folder="Pipe Organ Full",
         names={"fr": "Orgue à tuyaux (grand jeu)", "en": "Pipe organ (full)"}, category="organs",
         tuning="note", loops=True, settings={"velocity-db": 0, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 260}),
    dict(key="pipe-organ-quiet", source="pipe-organ-quiet", folder="Pipe Organ Soft",
         names={"fr": "Orgue à tuyaux (jeu doux)", "en": "Pipe organ (soft)"}, category="organs",
         tuning="note", loops=True, settings={"velocity-db": 0, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 260}),
    dict(key="renaissance-organ", source="renaissance-organ", folder="Renaissance Organ",
         names={"fr": "Orgue Renaissance (8 pieds)", "en": "Renaissance organ (8 foot)"}, category="organs",
         tuning="note", loops=True, settings={"velocity-db": 0, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 4, "release": 200}),
    dict(key="recorder-alto", source="recorder-alto", folder="Alto Recorder",
         names={"fr": "Flûte à bec alto", "en": "Alto recorder"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 200}),
    dict(key="recorder-tenor", source="recorder-tenor", folder="Tenor Recorder",
         names={"fr": "Flûte à bec ténor", "en": "Tenor recorder"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 200}),
    dict(key="ocarina", source="ocarina", folder="Ocarina",
         names={"fr": "Ocarina", "en": "Ocarina"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 200}),
    dict(key="tenor-sax", source="tenor-sax", folder="Tenor Saxophone",
         names={"fr": "Saxophone ténor", "en": "Tenor saxophone"}, category="woodwinds",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 240}),
    dict(key="harmonica", source="harmonica", folder="Chromatic Harmonica",
         names={"fr": "Harmonica chromatique", "en": "Chromatic harmonica"}, category="folk",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 6, "release": 200}),
    dict(key="bowed-psaltery", source="bowed-psaltery", folder="Bowed Psaltery",
         names={"fr": "Psaltérion à archet", "en": "Bowed psaltery"}, category="folk",
         tuning="note", loops=True, settings={"velocity-db": 12, "velocity-crossfade": 1, "loop": 1},
         envelope={"attack": 20, "release": 600}),
    dict(key="dan-tranh", source="dan-tranh", folder="Dan Tranh",
         names={"fr": "Đàn tranh (cithare vietnamienne)", "en": "Đàn tranh (Vietnamese zither)"}, category="folk",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 1500}),
    dict(key="marimba", source="marimba", folder="Marimba",
         names={"fr": "Marimba", "en": "Marimba"}, category="mallets",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 900}),
    dict(key="vibraphone", source="vibraphone", folder="Vibraphone",
         names={"fr": "Vibraphone", "en": "Vibraphone"}, category="mallets",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 2500}),
    dict(key="xylophone", source="xylophone", folder="Xylophone",
         names={"fr": "Xylophone", "en": "Xylophone"}, category="mallets",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 500}),
    dict(key="glockenspiel", source="glockenspiel", folder="Glockenspiel",
         names={"fr": "Glockenspiel", "en": "Glockenspiel"}, category="mallets",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 2500}),
    dict(key="tubular-bells", source="tubular-bells", folder="Tubular Bells",
         names={"fr": "Cloches tubulaires", "en": "Tubular bells"}, category="mallets", octave=0,
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 4000}),
    dict(key="hand-chimes", source="hand-chimes", folder="Hand Chimes",
         names={"fr": "Carillon à main", "en": "Hand chimes"}, category="mallets",
         tuning="note", settings={"velocity-db": 16}, envelope={"attack": 1, "release": 3000}),
    dict(key="clavisynth", source="clavisynth", folder="FM Clavisynth",
         names={"fr": "Clavisynth FM (TX81Z)", "en": "FM clavisynth (TX81Z)"}, category="electric-pianos",
         tuning="note", settings={"velocity-db": 18}, envelope={"attack": 1, "release": 300}),
]

# Instruments made from another's notes: no file of their own, a map of their own. "detune":
# each zone twice, that many cents apart either way (a honky-tonk's unisons); "softest": only
# that many of each note's softest layers, spread over the whole velocity range; "filter": the
# voice filter's cutoff (Hz).
DERIVED = [
    dict(key="upright-honky-tonk", base="upright-yamaha", folder="Honky-Tonk Upright",
         names={"fr": "Piano droit honky-tonk", "en": "Honky-tonk upright piano"}, category="pianos",
         detune=14.0, settings={"velocity-db": 18, "width": 1.2}, envelope={"attack": 1, "release": 380}),
    dict(key="felt-piano", base="grand-kawai", folder="Felt Piano",
         names={"fr": "Piano feutré", "en": "Felt piano"}, category="pianos",
         softest=2, gain=16.0, filter=2600.0, settings={"velocity-db": 14}, envelope={"attack": 3, "release": 700}),
]

CATEGORIES = {
    "pianos": {"fr": "Pianos", "en": "Pianos"},
    "electric-pianos": {"fr": "Pianos électriques", "en": "Electric pianos"},
    "harpsichords": {"fr": "Clavecins", "en": "Harpsichords"},
    "harps": {"fr": "Harpes", "en": "Harps"},
    "strings": {"fr": "Cordes frottées et pincées", "en": "Bowed and plucked strings"},
    "string-sections": {"fr": "Cordes d'orchestre", "en": "Orchestral strings"},
    "brass": {"fr": "Cuivres", "en": "Brass"},
    "woodwinds": {"fr": "Bois", "en": "Woodwinds"},
    "organs": {"fr": "Orgues", "en": "Organs"},
    "mallets": {"fr": "Percussions mélodiques", "en": "Tuned percussion"},
    "folk": {"fr": "Instruments traditionnels", "en": "Folk instruments"},
    "guitars": {"fr": "Guitares", "en": "Guitars"},
    "basses": {"fr": "Basses", "en": "Basses"},
    "drums": {"fr": "Batteries et percussions", "en": "Drums and percussion"},
    "electro-drums": {"fr": "Batteries électroniques", "en": "Electronic drums"},
    "synths": {"fr": "Synthés échantillonnés", "en": "Sampled synths"},
    "voices": {"fr": "Voix", "en": "Voices"},
}


def layer_rank(layer: str) -> float:
    """Softest first: a layer's number, or its dynamic marking."""
    if layer.isdigit():
        return float(layer)
    return float(DYNAMICS.get(layer.lower(), 0))


def measure_pitch(audio: np.ndarray, rate: int) -> float:
    """The note heard, in MIDI notes (fractional), over the second after the loudest instant."""
    signal = mono(audio)
    peak = int(np.argmax(np.abs(signal)))
    start = min(len(signal) - 1, peak + int(0.03 * rate))
    section = signal[start:start + int(1.2 * rate)]
    if len(section) < rate // 10:
        section = signal[: int(1.2 * rate)]
    track = yin(section, rate, hop=512, minimum_hz=24.0, maximum_hz=4200.0, window=4096)
    f0 = track.median_f0()
    return hz_to_midi(f0) if f0 > 0 else -1.0


def find_loop(audio: np.ndarray, rate: int, crossfade: int) -> tuple[int, int] | None:
    """A sustain loop: the end near the end of the steady part, the start where the sound
    matches it best, at least 1.2 s before; frames of the source."""
    signal = mono(audio)
    frame = int(0.02 * rate)
    count = len(signal) // frame
    if count < 10:
        return None
    levels = np.sqrt(np.mean(signal[: count * frame].reshape(count, frame) ** 2, axis=1))
    loud = levels.max()
    steady = np.nonzero(levels > loud * 10 ** (-9 / 20))[0]
    if len(steady) == 0:
        return None
    first = max(int(0.6 * rate), steady[0] * frame + int(0.3 * rate))
    last = min(len(signal) - int(0.4 * rate), (steady[-1] + 1) * frame - int(0.2 * rate))
    if last - first < int(1.8 * rate):
        return None
    window = 2048
    end = last
    reference = signal[end - window:end]
    reference = (reference - reference.mean()) / (np.linalg.norm(reference - reference.mean()) + 1e-12)
    lo = max(first, crossfade + window)
    hi = end - int(1.2 * rate)
    if hi <= lo:
        return None
    region = signal[lo - window:hi]
    # Normalised correlation of the reference with every window of the region.
    correlation = np.correlate(region, reference, mode="valid")
    squares = np.concatenate([[0.0], np.cumsum(region ** 2)])
    sums = np.concatenate([[0.0], np.cumsum(region)])
    energy = squares[window:] - squares[:-window] - (sums[window:] - sums[:-window]) ** 2 / window
    score = correlation / np.sqrt(np.maximum(energy, 1e-12))
    best = int(np.argmax(score))
    start = lo + best
    return int(start), int(end)


def convert(tool: Path, source: Path, target: Path) -> dict:
    completed = subprocess.run([str(tool), "convert", str(source), str(target)], capture_output=True, text=True,
                               timeout=600)
    line = completed.stdout.strip().splitlines()[-1] if completed.stdout.strip() else "{}"
    result = json.loads(line)
    if "error" in result or completed.returncode != 0:
        raise RuntimeError(f"{source}: {result.get('error', completed.stderr)}")
    return result


# A note cut from a bank's recording fades out over its last frames (what the FLACs had baked in).
END_FADE_SECONDS = 0.06


def build_instrument(spec: dict, files: list[dict], sources: Path, out: Path, tool: Path, samples: str,
                     legacy: dict) -> dict:
    folder = out / "Instruments" / spec["folder"]
    folder.mkdir(parents=True, exist_ok=True)
    # Measured only: the converted files go to a folder of their own, then away.
    scratch = tempfile.TemporaryDirectory(prefix="cydaw-factory-")
    converted_folder = folder if samples == "flac" else Path(scratch.name)
    wanted = spec.get("layers")
    chosen = [f for f in files if f["instrument"] == spec["source"] and (wanted is None or f["layer"] in wanted)]
    if not chosen:
        raise RuntimeError(f"{spec['key']}: no file")

    # Pitch and loops on the source, conversion in parallel.
    measured = []
    for item in chosen:
        audio, rate = read_wav(sources / item["file"])
        heard = measure_pitch(audio, rate)
        loop = find_loop(audio, rate, crossfade=int(0.08 * rate)) if spec.get("loops") else None
        measured.append({"item": item, "heard": heard, "rate": rate, "loop": loop})
    # The bank's octave numbering against the sound's.
    offsets = [round((m["heard"] - m["item"]["nameNote"]) / 12.0) * 12 for m in measured if m["heard"] > 0]
    octave = int(np.median(offsets)) if offsets else 0
    # Heard otherwise than measured: a bell's strike note is a virtual pitch YIN takes an octave low.
    octave = spec.get("octave", octave)
    for m in measured:
        m["root"] = int(np.clip(m["item"]["nameNote"] + octave, 0, 127))
        m["deviation"] = (m["heard"] - m["root"]) * 100.0 if m["heard"] > 0 else 0.0
    deviations = [m["deviation"] for m in measured if abs(m["deviation"]) < 60.0]
    mean_deviation = float(np.median(deviations)) if deviations else 0.0
    for m in measured:
        if spec["tuning"] == "note" and abs(m["deviation"]) < 60.0:
            m["tune"] = round(-m["deviation"], 1)
        else:
            m["tune"] = round(-mean_deviation, 1)

    jobs = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(2, (os.cpu_count() or 4) - 2)) as pool:
        for m in measured:
            layer = m["item"]["layer"]
            suffix = "" if layer == "-" or (wanted is not None and len(wanted) == 1) else f"_{layer}"
            name = f"{note_name(m['root'])}{suffix}.flac"
            m["file"] = name
            jobs[pool.submit(convert, tool, sources / m["item"]["file"], converted_folder / name)] = m
        for done in concurrent.futures.as_completed(jobs):
            jobs[done]["converted"] = done.result()
    scratch.cleanup()

    # Velocity layers per note, softest first; keys spread between the roots.
    by_root: dict[int, list[dict]] = {}
    for m in measured:
        by_root.setdefault(m["root"], []).append(m)
    roots = sorted(by_root)
    # The level: the loudest layer of each note, their median, to the target.
    loudest = [max(by_root[root], key=lambda m: layer_rank(m["item"]["layer"]))["converted"]["peakDb"] for root in roots]
    gain_db = round(float(np.clip(TARGET_PEAK_DB - np.median(loudest), -12.0, 24.0)), 1)
    zones = []
    for index, root in enumerate(roots):
        low_key = 0 if index == 0 else (roots[index - 1] + root) // 2 + 1
        high_key = 127 if index == len(roots) - 1 else (root + roots[index + 1]) // 2
        layers = sorted(by_root[root], key=lambda m: layer_rank(m["item"]["layer"]))
        for rank, m in enumerate(layers):
            low_velocity = rank * 127 // len(layers) + 1
            high_velocity = (rank + 1) * 127 // len(layers)
            flac = f"factory:Instruments/{spec['folder']}/{m['file']}"
            cut = m["converted"]["cut"]
            frames = m["converted"]["frames"]
            end_fade = min(frames // 2, int(END_FADE_SECONDS * m["converted"]["rate"]))
            # In bank mode the zone's frames are the recording's: the cut ones, `cut` later.
            offset = 0 if samples == "flac" else cut
            zone = {
                "file": flac if samples == "flac" else f"bank:{m['item']['file']}",
                "root": root,
                "keys": [low_key, high_key],
                "velocities": [low_velocity, high_velocity],
            }
            if gain_db:
                zone["gain"] = gain_db
            if m["tune"]:
                zone["tune"] = m["tune"]
            if samples != "flac":
                zone["start"] = cut
                zone["end"] = cut + frames
                zone["endFade"] = end_fade
                legacy[flac] = {"file": zone["file"], "offset": cut, "frames": frames, "endFade": end_fade}
            if m["loop"] is not None:
                start, end = m["loop"][0] - cut, m["loop"][1] - cut
                crossfade = int(0.08 * m["converted"]["rate"])
                if crossfade < start < end <= frames:
                    zone["loop"] = [start + offset, end + offset]
                    zone["loopCrossfade"] = crossfade
            zones.append(zone)
    sampler = {"format": "cydaw-sampler", "version": 1, "name": spec["names"]["en"], "zones": zones,
               "settings": spec["settings"]}
    map_path = folder / f"{spec['folder']}.cysampler"
    map_path.write_text(json.dumps(sampler, indent=1, ensure_ascii=False), encoding="utf-8")
    size = (sum(p.stat().st_size for p in folder.glob("*.flac")) if samples == "flac"
            else sum(m["item"]["size"] for m in measured))
    report = {
        "key": spec["key"],
        "notes": len(roots),
        "zones": len(zones),
        "octaveShift": octave,
        "meanDeviationCents": round(mean_deviation, 1),
        "largestDeviationCents": round(max((abs(m["deviation"]) for m in measured), default=0.0), 1),
        "loops": sum(1 for z in zones if "loop" in z),
        "gainDb": gain_db,
        "megabytes": round(size / 1e6, 1),
    }
    return {
        "key": spec["key"],
        "names": spec["names"],
        "category": spec["category"],
        "map": f"Instruments/{spec['folder']}/{spec['folder']}.cysampler",
        "settings": spec["settings"],
        "envelope": spec["envelope"],
        "source": "VSCO 2 CE" if chosen[0]["bank"] == "vsco" else "VCSL",
        "bank": chosen[0]["bank"],
        "licence": "CC0-1.0",
        "range": [note_name(roots[0]), note_name(roots[-1])],
        "gainDb": gain_db,
        "report": report,
    }


def build_derived(spec: dict, base_entry: dict, out: Path) -> dict:
    base_map = json.loads((out / base_entry["map"]).read_text(encoding="utf-8"))
    zones = base_map["zones"]
    if spec.get("softest"):
        by_root: dict[int, list[dict]] = {}
        for zone in zones:
            by_root.setdefault(zone["root"], []).append(zone)
        zones = []
        for root in sorted(by_root):
            layers = sorted(by_root[root], key=lambda zone: zone["velocities"][0])[: spec["softest"]]
            for rank, zone in enumerate(layers):
                zone = dict(zone)
                zone["velocities"] = [rank * 127 // len(layers) + 1, (rank + 1) * 127 // len(layers)]
                zones.append(zone)
    if spec.get("gain"):
        zones = [dict(zone, gain=round(float(zone.get("gain", 0.0)) + spec["gain"], 1)) for zone in zones]
    if spec.get("detune"):
        doubled = []
        for zone in zones:
            for side in (-1.0, 1.0):
                twin = dict(zone)
                twin["tune"] = round(float(zone.get("tune", 0.0)) + side * spec["detune"] / 2.0, 1)
                twin["gain"] = round(float(zone.get("gain", 0.0)) - 3.0, 1)
                twin["pan"] = round(side * 0.2, 2)
                doubled.append(twin)
        zones = doubled
    folder = out / "Instruments" / spec["folder"]
    folder.mkdir(parents=True, exist_ok=True)
    sampler = {"format": "cydaw-sampler", "version": 1, "name": spec["names"]["en"], "zones": zones,
               "settings": spec["settings"]}
    (folder / f"{spec['folder']}.cysampler").write_text(json.dumps(sampler, indent=1, ensure_ascii=False), encoding="utf-8")
    entry = {
        "key": spec["key"],
        "names": spec["names"],
        "category": spec["category"],
        "map": f"Instruments/{spec['folder']}/{spec['folder']}.cysampler",
        "settings": spec["settings"],
        "envelope": spec["envelope"],
        "source": base_entry["source"],
        "bank": base_entry.get("bank", ""),
        "licence": "CC0-1.0",
        "range": base_entry["range"],
        "derivedFrom": spec["base"],
        "report": {"key": spec["key"], "zones": len(zones), "derivedFrom": spec["base"]},
    }
    if spec.get("filter"):
        entry["filter"] = spec["filter"]
    return entry


LICENCE_HEAD = """CyDAW factory instruments
=========================

The sampler instruments (Instruments/) and the sound models made from them (Sound Models/) are
built from recordings released under Creative Commons Zero (CC0 1.0, public domain) or the
Unlicense: no rights reserved; use, change and share them, commercially too, without credit.
The credits below are given with thanks.

CyDAW does not ship the recordings: it downloads them from each bank's repository (Options >
Sound banks), at the commit banks.json names, with the bank's own licence file. It plays each
note as recorded (from where its sound starts to where it has faded), tuned and mapped to keys
and velocities; an SFZ bank is played as its own mapping opens (its controllers at their
starting values).

The electronic drums (CyDAW Electro) are not recorded: CyDAW makes them on the computer it runs
on, from the recipes BankLists/cydaw-electro.json lists, with its own drum and Kick engines. They
and their kits are CyDAW's own, dedicated to the public domain (CC0 1.0).
"""


def write_licence(out: Path, catalogue: dict) -> None:
    """LICENCE.txt: each bank an instrument plays, its author, licence, source and instruments."""
    registry = json.loads((out / "banks.json").read_text(encoding="utf-8")) if (out / "banks.json").exists() else {}
    banks = {bank["id"]: bank for bank in registry.get("banks", [])}
    used: dict[str, list[str]] = {}
    for entry in catalogue["instruments"]:
        used.setdefault(entry.get("bank") or ("vsco" if entry.get("source") == "VSCO 2 CE" else "vcsl"), []).append(
            entry["names"]["en"])
    lines = [LICENCE_HEAD]
    for bank_id, names in used.items():
        bank = banks.get(bank_id, {"title": bank_id, "author": "", "licence": "", "homepage": ""})
        lines.append(f"  - {bank['title']}" + (f", {bank['author']}" if bank.get("author") else ""))
        lines.append(f"    {bank.get('licence', '')}  {bank.get('homepage', '')}")
        lines.append(textwrap.fill(", ".join(names), width=100, initial_indent="    ", subsequent_indent="    "))
    (out / "LICENCE.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")



def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--sources", type=Path, default=default_destination())
    parser.add_argument("--out", type=Path, default=ROOT / "content" / "factory")
    parser.add_argument("--only", default="")
    parser.add_argument("--samples", choices=("bank", "flac"), default="bank")
    args = parser.parse_args()
    legacy_path = args.out / "Instruments" / "legacy.json"
    legacy = json.loads(legacy_path.read_text(encoding="utf-8")) if legacy_path.exists() else {}
    manifest = json.loads((args.sources / "manifest.json").read_text(encoding="utf-8"))
    files = manifest["files"]
    only = {key for key in args.only.split(",") if key}
    args.out.mkdir(parents=True, exist_ok=True)
    catalogue_path = args.out / "catalogue.json"
    catalogue = json.loads(catalogue_path.read_text(encoding="utf-8")) if catalogue_path.exists() else {}
    entries = {entry["key"]: entry for entry in catalogue.get("instruments", [])}
    # Instruments made elsewhere (build_sfz_instruments.py) stay, after these.
    known = {spec["key"] for spec in INSTRUMENTS} | {spec["key"] for spec in DERIVED}
    others = [entry for entry in catalogue.get("instruments", []) if entry["key"] not in known]
    for spec in INSTRUMENTS:
        if only and spec["key"] not in only:
            continue
        entry = build_instrument(spec, files, args.sources, args.out, args.tool, args.samples, legacy)
        # Its sound model, made by build_factory_models.py, stays listed.
        for kept in ("model", "modelSettings"):
            if kept in entries.get(spec["key"], {}):
                entry[kept] = entries[spec["key"]][kept]
        entries[spec["key"]] = entry
        print(json.dumps(entry["report"]), flush=True)
    for spec in DERIVED:
        if (not only or spec["key"] in only or spec["base"] in only) and spec["base"] in entries:
            entries[spec["key"]] = build_derived(spec, entries[spec["base"]], args.out)
            print(json.dumps(entries[spec["key"]]["report"]), flush=True)
    # Each derived instrument after the one it comes from.
    ordered = []
    for spec in INSTRUMENTS:
        if spec["key"] in entries:
            ordered.append(entries[spec["key"]])
            ordered.extend(entries[d["key"]] for d in DERIVED if d["base"] == spec["key"] and d["key"] in entries)
    ordered.extend(others)
    catalogue = {"format": "cydaw-factory-catalogue", "version": 1, "categories": CATEGORIES,
                 "instruments": ordered}
    # One note at a time for the winds, brass, bowed solo strings, the voice.
    factory_voicing.apply(catalogue)
    catalogue_path.write_text(json.dumps(catalogue, indent=1, ensure_ascii=False), encoding="utf-8")
    write_licence(args.out, catalogue)
    if args.samples == "bank":
        legacy_path.write_text(json.dumps(legacy, indent=1, ensure_ascii=False, sort_keys=True), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
