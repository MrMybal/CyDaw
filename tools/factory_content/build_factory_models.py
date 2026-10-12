"""The factory instruments as sound models: each recorded note analysed by the companion.

    python tools/factory_content/build_factory_models.py [--sources DIR] [--out content/factory]
           [--only key,key] [--jobs N]

Run after build_factory_content.py and build_sfz_instruments.py (the catalogue gives each
instrument's notes and octave; an SFZ instrument's, its map: every velocity layer of each note,
first round robin, one microphone, its key's noises left out). For every instrument, the notes it
is made of (every velocity layer, each at the middle of the velocities the sampler gives it) are
analysed in stereo into harmonics, noise and the recording's head with the note known (the
analysis' octave follows it); each zone is tuned as the sampler is (every note to equal temperament, or a piano by
its mean only), placed at its layer's velocity and, for a bank whose layers were recorded at the
same peak, given its level back. The zones are written compact (key frames) into
<out>/Sound Models/<folder>.cysound, and the catalogue entry learns its model and the sound-model
card's settings ("model", "modelSettings").
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import math
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "companion"))
sys.path.insert(0, str(HERE))

from build_factory_content import INSTRUMENTS, layer_rank  # noqa: E402
from fetch_banks import default_destination  # noqa: E402

# How each instrument plays as a model: the sound-model card's settings, and whether its layers
# were recorded at one peak (their dynamics then come back as levels).
MODEL_SETTINGS = {
    "grand-steinway": ({"velocity-db": 12, "release": 0.35}, 28.0),
    "grand-kawai": ({"velocity-db": 12, "release": 0.35}, 0.0),
    "upright-yamaha": ({"velocity-db": 12, "release": 0.3}, 0.0),
    "upright-knight": ({"velocity-db": 12, "release": 0.3}, 0.0),
    "fm-piano": ({"velocity-db": 12, "release": 0.25}, 0.0),
    "harp-concert": ({"velocity-db": 12, "release": 2.0}, 0.0),
    "harp-folk": ({"velocity-db": 12, "release": 1.8}, 0.0),
    "violin-arco": ({"velocity-db": 10}, 0.0),
    "violin-pizz": ({"velocity-db": 12, "release": 0.4}, 0.0),
}
HARPSICHORD = ({"velocity-db": 3, "release": 0.25, "velocity-brightness": 0}, 0.0)


def model_settings(spec: dict) -> tuple[dict, float]:
    """Those above, else the instrument's own: its release, its velocity range (none for an organ)."""
    if spec["key"] in MODEL_SETTINGS:
        return MODEL_SETTINGS[spec["key"]]
    if spec["key"].startswith("harpsichord"):
        return HARPSICHORD
    release = round(spec["envelope"]["release"] / 1000.0, 2)
    velocity_db = spec["settings"].get("velocity-db", 12) if spec.get("loops") else 12
    return {"velocity-db": velocity_db, "release": release}, 0.0


def build(spec: dict, files: list[dict], sources: Path, octave: int, gain_db: float) -> tuple[dict, dict]:
    import numpy as np  # noqa: PLC0415
    from cydaw_companion.analysis import AnalysisOptions, analyse  # noqa: PLC0415
    from cydaw_companion.audio_io import read_wav  # noqa: PLC0415
    from cydaw_companion.model import SoundModel  # noqa: PLC0415
    from cydaw_companion.pitch import midi_to_hz  # noqa: PLC0415

    started = time.time()
    wanted = spec.get("layers")
    chosen = [f for f in files if f["instrument"] == spec["source"] and (wanted is None or f["layer"] in wanted)]
    by_note: dict[int, list[dict]] = {}
    for item in chosen:
        by_note.setdefault(item["nameNote"] + octave, []).append(item)
    settings, normalised_db = model_settings(spec)
    zones = []
    reports = []
    for root in sorted(by_note):
        layers = sorted(by_note[root], key=lambda item: layer_rank(item["layer"]))
        for rank, item in enumerate(layers):
            # The middle of the velocities the sampler gives this layer.
            velocity = 0.8 if len(layers) == 1 else (rank + 0.5) / len(layers)
            audio, rate = read_wav(sources / item["file"])
            zone, report = analyse(audio, rate, AnalysisOptions(note_hint=float(root), velocity=velocity,
                                                                 loop=bool(spec.get("loops"))))
            zone.root_note = root
            zone.velocity = velocity
            deviation = 1200.0 * math.log2(zone.f0 / midi_to_hz(root)) if zone.f0 > 0 else 0.0
            reports.append({"root": root, "velocity": velocity, "deviation": deviation, "seconds": report["seconds"],
                            "harmonics": report["harmonics"]})
            zones.append(zone)
            # The sampler's level (its loudest layer near the same peak).
            zone.gain *= 10 ** (gain_db / 20.0)
            if normalised_db:
                zone.gain *= 10 ** (normalised_db * (velocity - 0.9) / 20.0)
    # Tuning: each note to equal temperament, or (a piano) the mean only.
    deviations = [r["deviation"] for r in reports if abs(r["deviation"]) < 60.0]
    mean = float(np.median(deviations)) if deviations else 0.0
    for zone, report in zip(zones, reports):
        correction = report["deviation"] if spec["tuning"] == "note" and abs(report["deviation"]) < 60.0 else mean
        zone.f0 /= 2.0 ** (correction / 1200.0)
    name = spec["names"]["en"]
    model = SoundModel(name=name, zones=zones,
                       source=[f"{'VSCO 2 CE' if item['bank'] == 'vsco' else 'VCSL'}: {Path(item['path']).name}"
                               for item in chosen],
                       musical={}, analysis={"factory": spec["key"], "seconds": round(time.time() - started, 1)})
    return model.to_json(), {"key": spec["key"], "zones": len(zones), "meanDeviationCents": round(mean, 1),
                             "settings": settings, "seconds": round(time.time() - started, 1)}


# SFZ instruments whose notes are held (their models loop).
SUSTAINED_SFZ = {"cello-solo", "double-bass-rubner", "baritone-sax", "alto-sax", "tuba-war", "erhu",
                 "caveman-cosmonaut", "cowsynth", "string-cyborgs", "voice-a"}
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_number(name: str) -> int:
    pitch = name.rstrip("-0123456789")
    return NOTE_NAMES.index(pitch) + 12 * (int(name[len(pitch):]) + 1)


def build_from_map(entry: dict, out: Path, banks: Path, tool: Path) -> tuple[dict, dict]:
    """An instrument made from an SFZ: its map's notes, the softest and loudest layer of each."""
    import numpy as np  # noqa: PLC0415
    from cydaw_companion.analysis import AnalysisOptions, analyse  # noqa: PLC0415
    from cydaw_companion.audio_io import read_wav  # noqa: PLC0415
    from cydaw_companion.model import SoundModel  # noqa: PLC0415
    from cydaw_companion.pitch import midi_to_hz  # noqa: PLC0415

    started = time.time()
    mapping = json.loads((out / entry["map"]).read_text(encoding="utf-8"))
    low, high = (note_number(name) for name in entry["range"])
    by_root: dict[int, dict[tuple, dict]] = {}
    for zone in mapping["zones"]:
        keys = zone.get("keys", [zone["root"], zone["root"]])
        # The first round robin; not a noise spread over the keyboard (a breath, a key click).
        if zone.get("sequence", [1, 1])[1] != 1 or not low <= zone["root"] <= high or keys[1] - keys[0] > 12:
            continue
        # One zone a velocity range: the first microphone.
        by_root.setdefault(zone["root"], {}).setdefault(tuple(zone.get("velocities", [1, 127])), zone)
    sustained = entry["key"] in SUSTAINED_SFZ
    settings = {"velocity-db": 12, "release": round(entry["envelope"]["release"] / 1000.0, 2)}
    zones = []
    reports = []
    sources = []
    with tempfile.TemporaryDirectory(prefix="cydaw-model-") as scratch:
        for root in sorted(by_root):
            layers = [by_root[root][velocities] for velocities in sorted(by_root[root])]
            for rank, item in enumerate(layers):
                # The middle of the velocities the map gives this layer.
                low, high = item.get("velocities", [1, 127])
                velocity = 0.8 if len(layers) == 1 else 0.5 * (low + high) / 127.0
                path = banks / item["file"][len("bank:"):]
                if path.suffix.lower() != ".wav":
                    wav = Path(scratch) / f"{root}_{rank}.wav"
                    subprocess.run([str(tool), "wav", str(path), str(wav)], check=True, capture_output=True, timeout=300)
                    path = wav
                audio, rate = read_wav(path)
                start = int(item.get("start", 0))
                end = int(item.get("end", -1))
                audio = audio[start:end if end > start else None]
                zone, report = analyse(audio, rate, AnalysisOptions(note_hint=float(root), velocity=velocity, loop=sustained))
                zone.root_note = root
                zone.velocity = velocity
                zone.gain *= 10 ** (float(item.get("gain", 0.0)) / 20.0)
                deviation = 1200.0 * math.log2(zone.f0 / midi_to_hz(root)) if zone.f0 > 0 else 0.0
                reports.append({"deviation": deviation})
                zones.append(zone)
                sources.append(f"{entry['bank']}: {Path(item['file']).name}")
    # Each note to equal temperament (its map's own tuning set aside: the model hears the note).
    deviations = [r["deviation"] for r in reports if abs(r["deviation"]) < 60.0]
    mean = float(np.median(deviations)) if deviations else 0.0
    for zone, report in zip(zones, reports):
        zone.f0 /= 2.0 ** ((report["deviation"] if abs(report["deviation"]) < 60.0 else mean) / 1200.0)
    model = SoundModel(name=entry["names"]["en"], zones=zones, source=sources, musical={},
                       analysis={"factory": entry["key"], "seconds": round(time.time() - started, 1)})
    return model.to_json(), {"key": entry["key"], "zones": len(zones), "meanDeviationCents": round(mean, 1),
                             "settings": settings, "seconds": round(time.time() - started, 1)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sources", type=Path, default=default_destination())
    parser.add_argument("--out", type=Path, default=ROOT / "content" / "factory")
    parser.add_argument("--only", default="")
    parser.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 4) - 2)))
    parser.add_argument("--tool", type=Path,
                        default=ROOT / "build" / "agent-graph" / "cydaw_sample_tool_artefacts" / "Release"
                        / "cydaw_sample_tool.exe")
    args = parser.parse_args()
    files = json.loads((args.sources / "manifest.json").read_text(encoding="utf-8"))["files"]
    catalogue_path = args.out / "catalogue.json"
    catalogue = json.loads(catalogue_path.read_text(encoding="utf-8"))
    entries = {entry["key"]: entry for entry in catalogue["instruments"]}
    only = {key for key in args.only.split(",") if key}
    folder = args.out / "Sound Models"
    folder.mkdir(parents=True, exist_ok=True)
    specs = [spec for spec in INSTRUMENTS if spec["key"] in entries and (not only or spec["key"] in only)]
    # The SFZ instruments that play notes (not the drum kits).
    mapped = [entry for entry in catalogue["instruments"] if "sfz" in entry and entry.get("melodic")
              and (not only or entry["key"] in only)]
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs) as pool:
        jobs = {pool.submit(build, spec, files, args.sources, int(entries[spec["key"]]["report"]["octaveShift"]),
                            float(entries[spec["key"]].get("gainDb", 0.0))): spec
                for spec in specs}
        for entry in mapped:
            jobs[pool.submit(build_from_map, entry, args.out, args.sources, args.tool)] = {
                "key": entry["key"], "folder": Path(entry["map"]).parent.name}
        for done in concurrent.futures.as_completed(jobs):
            spec = jobs[done]
            try:
                model, report = done.result()
            except Exception as error:  # noqa: BLE001 - one instrument's failure reported, the others go on
                print(json.dumps({"key": spec["key"], "error": str(error)[:300]}), flush=True)
                continue
            path = folder / f"{spec['folder']}.cysound"
            path.write_text(json.dumps(model, separators=(",", ":")), encoding="utf-8")
            entry = entries[spec["key"]]
            entry["model"] = f"Sound Models/{spec['folder']}.cysound"
            entry["modelSettings"] = report["settings"]
            report["megabytes"] = round(path.stat().st_size / 1e6, 2)
            print(json.dumps(report), flush=True)
            catalogue["instruments"] = [entries[key] for key in [e["key"] for e in catalogue["instruments"]]]
            catalogue_path.write_text(json.dumps(catalogue, indent=1, ensure_ascii=False), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
