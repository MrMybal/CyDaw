"""The companion end to end, on notes whose making is known (no sound, no network).

    python tools/companion/tests/test_companion.py

Each synthetic note is analysed; its model must play close to it (third-octave
band error), keep its pitch and inharmonicity, survive a save and reload, and
blend two zones by weight. Exit code 0 when all hold.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE))

from cydaw_companion.analysis import AnalysisOptions, analyse  # noqa: E402
from cydaw_companion.audio_io import read_wav, write_wav  # noqa: E402
from cydaw_companion.loss import band_error_db  # noqa: E402
from cydaw_companion.model import SoundModel  # noqa: E402
from cydaw_companion.segment import choose_notes, detect_notes  # noqa: E402
from cydaw_companion.synth import choose_zones, render, render_model  # noqa: E402
import make_test_notes as notes  # noqa: E402

failures: list[str] = []


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message)
    if not condition:
        failures.append(message)


def main() -> int:
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        made = {
            "pluck": (notes.pluck(220.0, 1.5), 3.0, None),
            "brass": (notes.brass(261.63, 1.8, 1.4), 2.0, None),
            "piano": (notes.piano(261.63, 2.5), 1.0, 0.0004),
            "flute": (notes.flute(392.0, 2.0, 1.6), 3.0, None),
        }
        zones = []
        for name, (signal, limit, inharmonicity) in made.items():
            path = folder / f"{name}.wav"
            write_wav(path, signal, 48000)
            audio, rate = read_wav(path)
            zone, report = analyse(audio, rate, AnalysisOptions())
            source = signal[np.nonzero(np.abs(signal) > np.max(np.abs(signal)) * 10 ** (-50 / 20))[0][0]:]
            held = zone.release * zone.frame_seconds if zone.release is not None else None
            played = render(zone, zone.root_note + report["cents"] / 100.0, zone.velocity, 48000, len(source) / 48000, held)
            error = band_error_db(source[:len(played)], played)
            check(error < limit, f"{name}: plays within {limit} dB of its source (band error {error:.2f} dB)")
            if inharmonicity is not None:
                check(abs(zone.inharmonicity - inharmonicity) < inharmonicity * 0.2,
                      f"{name}: inharmonicity {zone.inharmonicity:.5f} (made with {inharmonicity})")
            zones.append(zone)
        check(abs(zones[2].f0 - 261.63 * np.sqrt(1 + 0.0004)) < 0.5, f"piano: fundamental {zones[2].f0:.2f} Hz")
        check(zones[1].loop is not None and zones[0].loop is None, "brass holds a loop, the pluck decays freely")

        # Saved and read back: every frame (dB x 10 keeps 0.6 %; under -120 dB
        # is silence), then compact (key frames), which plays nearly the same.
        model = SoundModel(name="test", zones=zones[:3])
        path = folder / "test.cysound"
        model.save(path, compact=False)
        again = SoundModel.load(path)
        same = all(np.allclose(a.harmonics, b.harmonics, rtol=0.02, atol=2e-6) and a.loop == b.loop
                   for a, b in zip(model.zones, again.zones))
        check(same, "a model survives a save and a reload")
        compact_path = folder / "test-compact.cysound"
        model.save(compact_path)
        compact = SoundModel.load(compact_path)
        for a, b in zip(model.zones, compact.zones):
            first = render(a, a.root_note, a.velocity, 48000, a.frames * a.frame_seconds, None)
            second = render(b, b.root_note, b.velocity, 48000, b.frames * b.frame_seconds, None)
            error = band_error_db(first, second)
            check(b.frames == a.frames and a.loop == b.loop and error < 1.0,
                  f"a compact zone plays as the whole one ({b.frames} frames, band error {error:.2f} dB)")
        sizes = path.stat().st_size, compact_path.stat().st_size
        check(sizes[1] < sizes[0], f"compact is smaller ({sizes[0] // 1000} kB -> {sizes[1] // 1000} kB)")

        # Two zones of one instrument, an octave apart: between them, both.
        low, _ = analyse(notes.pluck(220.0, 1.2)[:, None], 48000)
        high, _ = analyse(notes.pluck(440.0, 1.2)[:, None], 48000)
        pair = SoundModel(name="pair", zones=[low, high])
        choice = choose_zones(pair, 63.0, low.velocity)
        check(len(choice) == 2, f"a note between two zones blends them ({choice})")
        between = render_model(pair, 63.0, low.velocity, 48000, 1.0)
        check(np.max(np.abs(between)) > 0.01, "the blend plays")

        # A plucked string, asked for: a string on the zone (not unless asked), played
        # close to its source, saved and read back.
        plucked = notes.pluck(220.0, 1.5)
        plain, _ = analyse(plucked[:, None], 48000)
        check(plain.string is None, "no string is fitted unless asked")
        strung, _ = analyse(plucked[:, None], 48000, AnalysisOptions(string=True))
        check(strung.string is not None, "a pluck asked as a string gets one")
        if strung.string is not None:
            onset = np.nonzero(np.abs(plucked) > np.max(np.abs(plucked)) * 10 ** (-50 / 20))[0][0]
            played = render(strung, strung.root_note, strung.velocity, 48000, 1.2, None)
            error = band_error_db(plucked[onset:onset + len(played)], played)
            check(error < 6.0, f"the string plays within 6 dB of the pluck (band error {error:.2f} dB)")
            saved = folder / "string.cysound"
            SoundModel(name="string", zones=[strung]).save(saved)
            back = SoundModel.load(saved).zones[0]
            again = render(back, back.root_note, back.velocity, 48000, 1.2, None)
            check(back.string is not None and band_error_db(played, again) < 0.5,
                  "its string survives a save and a reload")

        # Its own environment keeps every cache and temporary file in its folder (a
        # child process too), never on the system drive; the catalogue of what it can
        # install lists, for each component, its sources and every package's licence.
        import json  # noqa: PLC0415
        import os  # noqa: PLC0415
        import subprocess  # noqa: PLC0415
        import tempfile as temporary_files  # noqa: PLC0415
        from cydaw_companion.setup_env import contain  # noqa: PLC0415
        kept = {name: os.environ.get(name) for name in ("TMP", "TEMP", "TMPDIR", "PIP_CACHE_DIR", "XDG_CACHE_HOME",
                                                         "TORCH_HOME", "HF_HOME", "NUMBA_CACHE_DIR", "LIBROSA_DATA_DIR",
                                                         "MPLCONFIGDIR")}
        before = temporary_files.tempdir
        environment = folder / "environment"
        try:
            contain(environment)
            child = subprocess.run([sys.executable, "-c", "import tempfile, os; print(tempfile.gettempdir()); "
                                    "print(os.environ['TORCH_HOME']); print(os.environ['HF_HOME'])"],
                                   capture_output=True, text=True)
            places = [Path(line) for line in child.stdout.split()]
            check(Path(temporary_files.gettempdir()) == environment / "tmp" and len(places) == 3
                  and all(place.is_relative_to(environment) for place in places),
                  "the environment keeps its caches and temporary files")
        finally:
            temporary_files.tempdir = before
            for name, value in kept.items():
                if value is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = value
        catalogue = json.loads((Path(__file__).resolve().parents[1] / "cydaw_companion" / "components.json")
                               .read_text(encoding="utf-8"))
        components = {item["id"]: item for item in catalogue["components"]}
        check(set(components) == {"base", "torch-cpu", "torch-cuda", "crepe"}
              and all(item["packages"] and item["sources"] and all(package["licence"] and package["url"].startswith("https://")
                                                                   for package in item["packages"])
                      for item in components.values())
              and components["torch-cuda"]["requiresAcceptance"],
              "every component lists its sources and its packages' licences (CUDA's to accept)")

        # A bell: partials that are no harmonics, kept as ratios, saved, played back.
        prime = 440.0
        struck = notes.bell(prime, 3.0)
        bell, report = analyse(struck[:, None], 48000, AnalysisOptions(note_hint=69.0))
        check(report["partials"] == "inharmonic" and bell.ratios is not None,
              f"a bell keeps free ratios (harmonic share {report['harmonicShare']})")
        if bell.ratios is not None:
            found = [min(abs(r * bell.f0 / prime - made) / made for r in bell.ratios) for made in notes.BELL_RATIOS]
            check(max(found) < 0.005, f"each of its partials found within 0.5 % (worst {max(found) * 100:.2f} %)")
            source = struck[np.nonzero(np.abs(struck) > np.max(np.abs(struck)) * 10 ** (-50 / 20))[0][0]:]
            played = render(bell, bell.root_note + report["cents"] / 100.0, bell.velocity, 48000, len(source) / 48000)
            error = band_error_db(source[:len(played)], played)
            check(error < 2.5, f"the bell plays within 2.5 dB of its source (band error {error:.2f} dB)")
            reread = SoundModel.load(_saved(folder, bell)).zones[0]
            check(reread.ratios is not None and np.allclose(reread.ratios, bell.ratios, rtol=1e-5),
                  "its ratios survive a save and a reload")

        # Where the attack is: a note with 30 ms of breath before it.
        breath = notes.one_pole_lowpass(np.random.default_rng(3).normal(0, 1, 1440), 2000.0) * 0.02
        late = np.concatenate([breath, notes.pluck(220.0, 1.0)])
        late_zone, _ = analyse(late[:, None], 48000)
        check(0.02 < late_zone.onset < 0.045, f"the attack is found after the breath ({late_zone.onset * 1000:.1f} ms)")

        # A phrase: three notes one after another, found and given their pitches.
        gap = np.zeros(int(0.1 * 48000))
        phrase = np.concatenate([notes.pluck(220.0, 1.0), gap, notes.pluck(330.0, 1.0) * 0.5, gap, notes.pluck(440.0, 1.0)])
        found, _ = detect_notes(phrase, 48000)
        pitches = sorted(round(note.midi) for note in choose_notes(found, 48000))
        check(pitches == [57, 64, 69], f"a phrase's notes are found ({pitches})")
        layers = {round(note.midi): round(note.velocity, 2) for note in found if np.isfinite(note.midi)}
        check(layers.get(64, 1.0) < layers.get(69, 0.0), f"a softer note gets a softer velocity ({layers})")
    print("failures:", len(failures))
    return 1 if failures else 0


def _saved(folder: Path, zone) -> Path:
    path = folder / "one.cysound"
    SoundModel(name="one", zones=[zone]).save(path)
    return path


if __name__ == "__main__":
    sys.exit(main())
