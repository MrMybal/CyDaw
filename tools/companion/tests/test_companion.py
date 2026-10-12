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
    print("failures:", len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
