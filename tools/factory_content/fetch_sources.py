"""Download the CC0 recordings the factory instruments are made from.

    python tools/factory_content/fetch_sources.py [--dest DIR] [--list]

The notes come one by one from the Versilian Studios repositories on GitHub (VCSL and VSCO 2 Community
Edition, both CC0 1.0: public domain, redistributable without credit). Only sustained notes are taken,
first round-robin, about one sampled pitch in every SELECTION step, every velocity layer listed below.
Files already present with the right size are skipped, so an interrupted run resumes. The default
destination is %LOCALAPPDATA%/CyDAW/sources (outside the project; it can be deleted once the factory
content is built).

Writes <dest>/manifest.json: every file with its bank, instrument, note, layer, size and source URL.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

BANKS = {
    "vcsl": {"repo": "sgossner/VCSL", "branch": "master", "licence": "CC0-1.0",
             "title": "Versilian Community Sample Library (VCSL)", "author": "Versilian Studios LLC"},
    "vsco": {"repo": "sgossner/VSCO-2-CE", "branch": "master", "licence": "CC0-1.0",
             "title": "Versilian Studios Chamber Orchestra 2 Community Edition", "author": "Versilian Studios LLC"},
}

# key, bank, folder, file filter, velocity-layer pattern (group 1 = the layer), layers kept (None = all), step
SELECTION = [
    ("grand-steinway", "vcsl", "Chordophones/Zithers/Grand Piano, Steinway B/Sus/", r"_rr1", r"_vl(\d)", {"2", "3", "4"}, 3),
    ("grand-kawai", "vcsl", "Chordophones/Zithers/Grand Piano, Kawai/Sustains/", r"_rr1", r"_v(\d)", {"1", "2", "3", "4"}, 3),
    ("upright-yamaha", "vcsl", "Chordophones/Zithers/Upright Piano, Yamaha/Sustains/", r"_rr1", r"_vl(\d)", {"1", "2", "3"}, 3),
    ("upright-knight", "vcsl", "Chordophones/Zithers/Upright Piano, Knight/Sustains/", r"_rr1", r"_vl(\d)", None, 3),
    ("fm-piano", "vcsl", "Electrophones/TX81Z/FM Piano/", r"", r"_vl(\d)", {"1", "2", "3"}, 6),
    ("harpsichord-flemish", "vcsl", "Chordophones/Zithers/Harpsichord, Flemish/Sustains/", r"_rr1", r"Sustains/(\w+)/", None, 3),
    ("harpsichord-french", "vcsl", "Chordophones/Zithers/Harpsichord, French/Sustains/", r"_rr1", None, None, 3),
    ("harpsichord-italian", "vcsl", "Chordophones/Zithers/Harpsichord, Italian/Sustains/", r"", None, None, 3),
    ("harpsichord-english", "vcsl", "Chordophones/Zithers/Harpsichord, English/Sustains/", r"_rr1", r"Sustains/(\w+)/", None, 3),
    ("harp-concert", "vcsl", "Chordophones/Composite Chordophones/Concert Harp/", r"1\.wav", r"_(f|mf|mp|p)1", None, 2),
    ("harp-folk", "vcsl", "Chordophones/Composite Chordophones/Folk Harp/", r"_RR1", r"_v(\d)", None, 2),
    ("violin-arco", "vsco", "Strings/Solo Violin/Arco Vib/", r"", r"_(p|f)\.wav", None, 1),
    ("violin-pizz", "vsco", "Strings/Solo Violin/Pizz/", r"_RR1", r"_(p|f)_", None, 2),
    # Orchestra (VSCO 2 CE): sections and solo bass, brass, woodwinds; every note sampled.
    ("cello-section", "vsco", "Strings/Cello Section/susvib/", r"_1\.wav", r"_v(\d)_", None, 1),
    ("viola-section", "vsco", "Strings/Viola Section/susvib/", r"_1\.wav", r"_v(\d)_", None, 1),
    ("violin-section", "vsco", "Strings/Violin Section/susVib/", r"", r"_v(\d)", None, 1),
    ("contrabass-arco", "vsco", "Strings/Solo Contrabass/SusVib/", r"_rr1", r"_v(\d)_", None, 1),
    ("cello-section-pizz", "vsco", "Strings/Cello Section/pizzT/", r"_RR1", r"_v(\d)_", None, 1),
    ("violin-section-pizz", "vsco", "Strings/Violin Section/Pizz/", r"_rr1", r"_v(\d)_", None, 1),
    ("contrabass-pizz", "vsco", "Strings/Solo Contrabass/Pizz/", r"_rr1", r"_v(\d)_", None, 1),
    ("horn", "vsco", "Brass/F Horn/sus/", r"_1\.wav", r"_v(\d)_", None, 1),
    ("trumpet", "vsco", "Brass/Trumpet/sus/", r"_rr1", r"_v(\d)_", None, 1),
    ("trombone", "vsco", "Brass/Tenor Trombone/sus/", r"_1\.wav", r"_v(\d)_", None, 1),
    ("tuba", "vsco", "Brass/Tuba/sus/", r"_rr1_Mid", r"_v(\d)_", None, 1),
    ("flute", "vsco", "Woodwinds/Flute/susNV/", r"_1\.wav", r"_v(\d)_", None, 1),
    ("oboe", "vsco", "Woodwinds/Oboe/Sus/", r"_Main", r"_v(\d)_", None, 1),
    ("clarinet", "vsco", "Woodwinds/Clarinet/susLong/", r"_rr1_sum", r"_v(\d)_", None, 1),
    ("bassoon", "vsco", "Woodwinds/Bassoon/sus/", r"_1\.wav", r"_v(\d)_", None, 1),
    # VCSL: organs, winds, tuned percussion, plucked and electric.
    ("pipe-organ", "vcsl", "Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/", r"", None, None, 1),
    ("pipe-organ-quiet", "vcsl", "Aerophones/Edge-blown Aerophones/Pipe Organ/Quiet/", r"_rr1", None, None, 1),
    ("renaissance-organ", "vcsl", "Aerophones/Edge-blown Aerophones/Renaissance Organ/8'/", r"_rr1", None, None, 1),
    ("recorder-alto", "vcsl", "Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/", r"_rr1", None, None, 1),
    ("recorder-tenor", "vcsl", "Aerophones/Edge-blown Aerophones/Baroque Tenor Recorder/Sustain/", r"_rr1", None, None, 1),
    ("ocarina", "vcsl", "Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/", r"", None, None, 1),
    ("harmonica", "vcsl", "Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/", r"", None, None, 1),
    ("tenor-sax", "vcsl", "Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/", r"_rr1", r"_vl(\d)_", None, 1),
    ("marimba", "vcsl", "Idiophones/Struck Idiophones/Marimba/", r"_01\.wav", r"_(loud|med|soft)_01", None, 1),
    ("vibraphone", "vcsl", "Idiophones/Struck Idiophones/Vibraphone/Soft Mallets/", r"_rr1", r"_v(\d)_", None, 1),
    ("xylophone", "vcsl", "Idiophones/Struck Idiophones/Xylophone/Medium Mallets/", r"_01_far", r"_(pp|ff)_01", None, 1),
    ("glockenspiel", "vcsl", "Idiophones/Struck Idiophones/Glockenspiel/", r"_01\.wav", r"glock_(\w+?)_[A-G]", None, 1),
    ("tubular-bells", "vcsl", "Idiophones/Struck Idiophones/Tubular Bells 2/", r"_1\.wav", r"_v(\d)_", None, 1),
    ("hand-chimes", "vcsl", "Idiophones/Struck Idiophones/Hand Chimes/", r"_r01_", None, None, 1),
    ("dan-tranh", "vcsl", "Chordophones/Zithers/Dan Tranh/Normal/", r"_1\.wav", r"_(p|mp|mf|f|ff)_1", None, 1),
    ("bowed-psaltery", "vcsl", "Chordophones/Zithers/Psaltery, Bowed and Plucked/LongBow/", r"_rr", None, None, 1),
    ("clavisynth", "vcsl", "Electrophones/TX81Z/Clavisynth/", r"", r"_vl(\d)", None, 3),
]

NOTE = re.compile(r"(?<![A-Za-z])([A-G])(#?)(-?\d)(?=[_.])")
PITCH_CLASS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note_name_number(name: str) -> int | None:
    """The note written in a file name, in the bank's own octave numbering (C4 = 60 when octave 4 is middle C)."""
    match = NOTE.search(name)
    if not match:
        return None
    return 12 * (int(match.group(3)) + 1) + PITCH_CLASS[match.group(1)] + (1 if match.group(2) else 0)


def tree(bank: str, cache: Path) -> list[dict]:
    """The bank's file list (GitHub tree API, cached next to the downloads)."""
    path = cache / f"{bank}-tree.json"
    if not path.exists():
        info = BANKS[bank]
        url = f"https://api.github.com/repos/{info['repo']}/git/trees/{info['branch']}?recursive=1"
        with urllib.request.urlopen(url, timeout=60) as response:
            path.write_bytes(response.read())
    return json.loads(path.read_text(encoding="utf-8"))["tree"]


def select(cache: Path) -> list[dict]:
    files = []
    for key, bank, folder, keep, layer_pattern, layers, step in SELECTION:
        entries = [e for e in tree(bank, cache) if e["type"] == "blob" and e["path"].startswith(folder)
                   and e["path"].lower().endswith(".wav") and re.search(keep, e["path"])]
        by_note: dict[int, list[tuple[str, dict]]] = {}
        for entry in entries:
            name = entry["path"][len(folder):]
            note = note_name_number(name)
            if note is None:
                continue
            layer = "-"
            if layer_pattern:
                found = re.search(layer_pattern, entry["path"])
                layer = found.group(1) if found else "-"
            if layers is not None and layer not in layers:
                continue
            by_note.setdefault(note, []).append((layer, entry))
        last = None
        for note in sorted(by_note):
            if last is not None and note - last < step:
                continue
            last = note
            for layer, entry in by_note[note]:
                info = BANKS[bank]
                url = (f"https://raw.githubusercontent.com/{info['repo']}/{info['branch']}/"
                       + urllib.parse.quote(entry["path"]))
                files.append({"instrument": key, "bank": bank, "path": entry["path"], "nameNote": note,
                              "layer": layer, "size": entry["size"], "url": url})
    return files


def fetch(url: str, target: Path, size: int) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    for attempt in range(5):
        try:
            with urllib.request.urlopen(url, timeout=120) as response, open(partial, "wb") as out:
                while chunk := response.read(1 << 20):
                    out.write(chunk)
            if partial.stat().st_size != size:
                raise OSError(f"size {partial.stat().st_size} != {size}")
            os.replace(partial, target)
            return
        except OSError as error:
            print(f"  retry {attempt + 1} {target.name}: {error}", flush=True)
            time.sleep(2 + 3 * attempt)
    raise SystemExit(f"could not download {url}")


def main() -> int:
    parser = argparse.ArgumentParser()
    default = Path(os.environ.get("LOCALAPPDATA", Path.home())) / "CyDAW" / "sources"
    parser.add_argument("--dest", type=Path, default=default)
    parser.add_argument("--list", action="store_true", help="only print the selection")
    args = parser.parse_args()
    args.dest.mkdir(parents=True, exist_ok=True)
    files = select(args.dest)
    total = sum(f["size"] for f in files)
    print(f"{len(files)} files, {total / 1e6:.0f} MB -> {args.dest}", flush=True)
    if args.list:
        for f in files:
            print(f"{f['instrument']:22s} {f['layer']:>4s} {f['size'] / 1e6:6.1f} MB  {f['path']}")
        return 0
    done = 0
    for index, f in enumerate(files):
        target = args.dest / f["bank"] / f["path"]
        if not (target.exists() and target.stat().st_size == f["size"]):
            fetch(f["url"], target, f["size"])
        done += f["size"]
        f["file"] = str(target.relative_to(args.dest)).replace("\\", "/")
        if index % 10 == 0 or index == len(files) - 1:
            print(f"{index + 1}/{len(files)} {done / 1e6:.0f}/{total / 1e6:.0f} MB", flush=True)
    manifest = {"banks": BANKS, "files": files}
    (args.dest / "manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    print("done", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
