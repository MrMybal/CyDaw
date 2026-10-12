"""Command line of the companion. Every line it prints is one JSON object:

    {"progress": 0.4, "message": "..."}     while it works
    {"done": true, ...}                     the result
    {"error": "..."}                        what went wrong (exit code 1)

Commands:
    info                                    Python, numpy, torch, CUDA and the GPU
    analyse FILE... --out M.cysound         recorded notes -> a sound model (one zone each)
        [--name N] [--note MIDI] [--velocity 0..1] [--loop on|off] [--no-attack]
        [--refine] [--device auto|cpu|cuda] [--steps N]
    render M.cysound --out F.wav            the model played (the reference player)
        [--note MIDI] [--velocity 0..1] [--seconds S] [--held S] [--zone I]
    evaluate M.cysound FILE...              each zone against its source
    setup --env FOLDER [--torch none|cpu|cuda] [--clap]
                                            its own environment (venv, numpy, PyTorch, and
                                            transformers for the semantic search); runs
                                            with any Python, before numpy is there
    clap-check [--model DIR]                what the semantic search needs, and the model
                                            loaded once when given
    clap-worker --model DIR [--device auto|cpu|cuda] [--idle S]
                                            the semantic search's model (clap_worker.py)
"""
from __future__ import annotations

import argparse
import json
import platform
import sys
import time
from pathlib import Path

from . import __version__


def emit(**value) -> None:
    print(json.dumps(value, ensure_ascii=False), flush=True)


def progress(fraction: float, message: str) -> None:
    emit(progress=round(float(fraction), 3), message=message)


def command_info(_: argparse.Namespace) -> int:
    value = {"done": True, "companion": __version__, "python": platform.python_version(),
             "executable": sys.executable, "numpy": None, "torch": None, "cuda": False, "gpu": None}
    try:
        import numpy  # noqa: PLC0415
        value["numpy"] = numpy.__version__
    except Exception:  # noqa: BLE001 - reported as missing
        pass
    try:
        import torch  # noqa: PLC0415
        value["torch"] = torch.__version__
        value["cuda"] = bool(torch.cuda.is_available())
        if value["cuda"]:
            value["gpu"] = torch.cuda.get_device_name(0)
            value["gpuMemoryGb"] = round(torch.cuda.get_device_properties(0).total_memory / 2 ** 30, 1)
    except Exception:  # noqa: BLE001 - torch is optional
        pass
    emit(**value)
    return 0


def command_setup(arguments: argparse.Namespace) -> int:
    from .setup_env import setup  # noqa: PLC0415
    return setup(Path(arguments.env), arguments.torch, arguments.clap)


def command_clap_check(arguments: argparse.Namespace) -> int:
    from .clap_worker import check  # noqa: PLC0415
    return check(Path(arguments.model) if arguments.model else None)


def command_clap_worker(arguments: argparse.Namespace) -> int:
    from .clap_worker import serve  # noqa: PLC0415
    return serve(Path(arguments.model), arguments.device, arguments.idle)


def command_analyse(arguments: argparse.Namespace) -> int:
    import numpy as np  # noqa: PLC0415
    from .analysis import AnalysisOptions, analyse  # noqa: PLC0415
    from .audio_io import mono, read_wav  # noqa: PLC0415
    from .loss import band_error_db, level_difference_db, spectral_loss  # noqa: PLC0415
    from .model import SoundModel  # noqa: PLC0415
    from .synth import render  # noqa: PLC0415

    files = [Path(f) for f in arguments.files]
    zones = []
    reports = []
    started = time.time()
    for index, file in enumerate(files):
        progress(index / max(1, len(files)) * 0.8, f"analyse {file.name}")
        audio, rate = read_wav(file)
        options = AnalysisOptions(note_hint=arguments.note, velocity=arguments.velocity,
                                  keep_attack=not arguments.no_attack,
                                  loop=None if arguments.loop is None else arguments.loop == "on")
        zone, report = analyse(audio, rate, options)
        if arguments.refine:
            from .refine import refine  # noqa: PLC0415
            progress((index + 0.5) / max(1, len(files)) * 0.8, f"refine {file.name}")
            report["refine"] = refine(zone, audio, rate, device=arguments.device, steps=arguments.steps,
                                      progress=lambda f, m: progress((index + 0.5 + 0.5 * f) / len(files) * 0.8, m))
        # How close the model plays to the note it came from.
        source = mono(audio)
        if rate != 48000:
            from .audio_io import resample  # noqa: PLC0415
            source = resample(source, rate, 48000)
        peak = np.max(np.abs(source))
        loud = np.nonzero(np.abs(source) > peak * 10 ** (-50 / 20))[0]
        source = source[max(0, loud[0] - int(0.002 * 48000)):]
        seconds = report["seconds"]
        held = zone.release * zone.frame_seconds if zone.release is not None else None
        played = render(zone, zone.root_note + report["cents"] / 100.0, zone.velocity, 48000, seconds, held)
        report["spectralLoss"] = round(spectral_loss(source[:len(played)], played), 4)
        report["levelDb"] = round(level_difference_db(source[:len(played)], played), 2)
        report["bandErrorDb"] = round(band_error_db(source[:len(played)], played), 2)
        report["file"] = str(file)
        zones.append(zone)
        reports.append(report)
    name = arguments.name or (files[0].stem if len(files) == 1 else files[0].parent.name)
    musical = reports[0].get("musical", {}) if reports else {}
    model = SoundModel(name=name, zones=zones, source=[str(f) for f in files], musical=musical,
                       analysis={"companion": __version__, "seconds": round(time.time() - started, 2),
                                 "zones": [{k: v for k, v in r.items() if k != "musical"} for r in reports]})
    progress(0.95, "write the model")
    model.save(arguments.out)
    emit(done=True, model=str(arguments.out), name=name, zones=reports)
    return 0


def command_render(arguments: argparse.Namespace) -> int:
    import numpy as np  # noqa: PLC0415
    from .audio_io import write_wav  # noqa: PLC0415
    from .model import SoundModel  # noqa: PLC0415
    from .synth import render  # noqa: PLC0415

    model = SoundModel.load(arguments.model)
    zone = model.zones[arguments.zone]
    note = arguments.note if arguments.note is not None else zone.root_note
    velocity = arguments.velocity if arguments.velocity is not None else zone.velocity
    audio = render(zone, note, velocity, 48000, arguments.seconds, arguments.held)
    write_wav(arguments.out, audio, 48000, float32=True)
    emit(done=True, out=str(arguments.out), peakDb=round(float(20 * np.log10(max(np.max(np.abs(audio)), 1e-12))), 2))
    return 0


def command_evaluate(arguments: argparse.Namespace) -> int:
    import numpy as np  # noqa: PLC0415
    from .audio_io import mono, read_wav, resample  # noqa: PLC0415
    from .loss import band_error_db, level_difference_db, spectral_loss  # noqa: PLC0415
    from .model import SoundModel  # noqa: PLC0415
    from .synth import render  # noqa: PLC0415

    model = SoundModel.load(arguments.model)
    results = []
    for zone, file in zip(model.zones, arguments.files):
        audio, rate = read_wav(file)
        source = resample(mono(audio), rate, 48000)
        peak = np.max(np.abs(source))
        loud = np.nonzero(np.abs(source) > peak * 10 ** (-50 / 20))[0]
        source = source[max(0, loud[0] - int(0.002 * 48000)):]
        held = zone.release * zone.frame_seconds if zone.release is not None else None
        played = render(zone, zone.root_note, zone.velocity, 48000, len(source) / 48000, held)
        results.append({"file": str(file), "spectralLoss": round(spectral_loss(source, played), 4),
                        "levelDb": round(level_difference_db(source, played), 2),
                        "bandErrorDb": round(band_error_db(source, played), 2)})
    emit(done=True, results=results)
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="cydaw_companion", description="CyDAW's Python companion")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("info")
    analyse_parser = commands.add_parser("analyse")
    analyse_parser.add_argument("files", nargs="+")
    analyse_parser.add_argument("--out", required=True)
    analyse_parser.add_argument("--name")
    analyse_parser.add_argument("--note", type=float)
    analyse_parser.add_argument("--velocity", type=float)
    analyse_parser.add_argument("--loop", choices=["on", "off"])
    analyse_parser.add_argument("--no-attack", action="store_true")
    analyse_parser.add_argument("--refine", action="store_true")
    analyse_parser.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"])
    analyse_parser.add_argument("--steps", type=int, default=300)
    render_parser = commands.add_parser("render")
    render_parser.add_argument("model")
    render_parser.add_argument("--out", required=True)
    render_parser.add_argument("--note", type=float)
    render_parser.add_argument("--velocity", type=float)
    render_parser.add_argument("--seconds", type=float, default=2.0)
    render_parser.add_argument("--held", type=float)
    render_parser.add_argument("--zone", type=int, default=0)
    setup_parser = commands.add_parser("setup")
    setup_parser.add_argument("--env", required=True)
    setup_parser.add_argument("--torch", default="none", choices=["none", "cpu", "cuda"])
    setup_parser.add_argument("--clap", action="store_true")
    clap_check_parser = commands.add_parser("clap-check")
    clap_check_parser.add_argument("--model")
    clap_worker_parser = commands.add_parser("clap-worker")
    clap_worker_parser.add_argument("--model", required=True)
    clap_worker_parser.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"])
    clap_worker_parser.add_argument("--idle", type=float, default=900.0)
    evaluate_parser = commands.add_parser("evaluate")
    evaluate_parser.add_argument("model")
    evaluate_parser.add_argument("files", nargs="+")
    arguments = parser.parse_args(argv)
    handlers = {"info": command_info, "analyse": command_analyse, "render": command_render,
                "evaluate": command_evaluate, "setup": command_setup, "clap-check": command_clap_check,
                "clap-worker": command_clap_worker}
    try:
        return handlers[arguments.command](arguments)
    except Exception as error:  # noqa: BLE001 - reported to CyDAW, which shows it
        emit(error=f"{type(error).__name__}: {error}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
