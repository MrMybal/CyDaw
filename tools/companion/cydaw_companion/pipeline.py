"""One recorded note into the zone that plays it best: what the `analyse` command and the factory
builder (tools/factory_content/build_factory_models.py) share.

1. the analysis (harmonics + noise + head; free ratios already when the partials are far from
   harmonic, a plucked string fitted when asked);
2. nearly harmonic (a marimba's bar under its overtones): the free ratios tried too, the closer of
   the two kept;
3. a plucked string kept when asked ("string"), or ("auto") when it plays within 0.5 dB of the
   partials (it transposes as a string does);
4. the refinement (PyTorch), when asked, on the partials (a string is played, not its partials).

Each step is judged by `fit`: the model rendered at the note and compared with the recording.
"""
from __future__ import annotations

from typing import Callable

import numpy as np


def fit(zone, report: dict, audio: np.ndarray, rate: int, limit_seconds: float | None = None) -> dict:
    """How close the model plays to the note it came from: the spectral loss, the level and the
    third-octave band error of its rendering against the recording (its first `limit_seconds`)."""
    from .audio_io import mono, resample  # noqa: PLC0415
    from .loss import band_error_db, level_difference_db, spectral_loss  # noqa: PLC0415
    from .synth import render  # noqa: PLC0415

    source = mono(audio)
    if rate != 48000:
        source = resample(source, rate, 48000)
    peak = np.max(np.abs(source))
    loud = np.nonzero(np.abs(source) > peak * 10 ** (-50 / 20))[0]
    source = source[max(0, loud[0] - int(0.002 * 48000)):]
    held = zone.release * zone.frame_seconds if zone.release is not None else None
    seconds = report["seconds"] if limit_seconds is None else min(report["seconds"], limit_seconds)
    played = render(zone, zone.root_note + report["cents"] / 100.0, zone.velocity, 48000, seconds, held)
    return {"spectralLoss": round(spectral_loss(source[:len(played)], played), 4),
            "levelDb": round(level_difference_db(source[:len(played)], played), 2),
            "bandErrorDb": round(band_error_db(source[:len(played)], played), 2)}


def best_zone(audio: np.ndarray, rate: int, options, kind: str = "auto", refine: bool = False, device: str = "auto",
              steps: int | None = None, progress: Callable[[float, str], None] | None = None,
              limit_seconds: float | None = None):
    """The zone for one recorded note and its report (with `kind`: "partials", "string" or "auto")."""
    from .analysis import analyse  # noqa: PLC0415

    options.string = kind != "partials"
    zone, report = analyse(audio, rate, options)
    # The partials first (the string, if any, set aside and tried after).
    string = zone.string
    zone.string = None
    report.update(fit(zone, report, audio, rate, limit_seconds))
    share = report.get("harmonicShare")
    if options.partials == "auto" and report["partials"] == "harmonic" and share is not None and share < 0.99:
        options.partials = "inharmonic"
        other, other_report = analyse(audio, rate, options)
        options.partials = "auto"
        other.string = None
        other_report.update(fit(other, other_report, audio, rate, limit_seconds))
        if other_report["bandErrorDb"] < report["bandErrorDb"]:
            other_report["tried"] = {"harmonic": report["bandErrorDb"]}
            zone, report = other, other_report
            string = None
        else:
            report["tried"] = {"inharmonic": other_report["bandErrorDb"]}
    if string is not None and kind != "partials":
        partial_fit = dict(report)
        zone.string = string
        string_fit = fit(zone, report, audio, rate, limit_seconds)
        if kind == "string" or string_fit["bandErrorDb"] <= partial_fit["bandErrorDb"] + 0.5:
            report.update(string_fit)
            report["kind"] = "string"
            report["partialsBandErrorDb"] = partial_fit["bandErrorDb"]
        else:
            zone.string = None
            report["stringBandErrorDb"] = string_fit["bandErrorDb"]
    report.setdefault("kind", "partials")
    if refine and zone.string is not None:
        report["refine"] = {"skipped": "a string is played, not its partials"}
    elif refine:
        from .refine import refine as refine_zone  # noqa: PLC0415
        report["refine"] = refine_zone(zone, audio, rate, device=device, steps=steps,
                                       progress=progress or (lambda fraction, message: None))
        report.update(fit(zone, report, audio, rate, limit_seconds))
    return zone, report
