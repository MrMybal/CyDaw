"""A project to hear the factory sound models before and after a rebuild, side by side.

    python tools/templates/build_model_comparison.py [--old REV] [--out FILE] [--exe "<CyDAW 2.exe>"]

For every factory instrument whose model changed since REV (a git revision; default: the commit
before the last rebuild of the models), two instrument tracks next to each other: "<name>
(ancien)", the model as it was at REV, and "<name> (nouveau)", the model in content/factory now,
each on the factory instrument's own card (its settings), so only the model differs. Both play the
same phrase (as in "12-tous-les-instruments"): in the arrangement four bars each instrument, the
phrase on the old model (bars 1-2) then on the new (3-4); in the launcher's first scene, the same
phrase on each, looping, to play one then the other.

The old models are read from git (LFS) into <out folder>/Anciens modeles. The project holds both
models of every instrument (a few hundred megabytes): it is written outside the repository (by
default D:/CyDAW/Comparaison des modeles). The session is silent and hidden; nothing plays.
"""
import argparse
import os
import pathlib
import subprocess
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import build_v2_examples as ex  # noqa: E402

MODELS = "content/factory/Sound Models"
ex.PORT = 47832


def git(*arguments, data=None):
    return subprocess.run(["git", *arguments], cwd=ROOT, input=data, capture_output=True, check=True).stdout


def old_models(revision: str, folder: pathlib.Path) -> dict[str, pathlib.Path]:
    """The models that changed since `revision`, as they were then (out of LFS): by file name."""
    changed = git("diff", "--name-only", revision, "--", MODELS).decode("utf-8").splitlines()
    folder.mkdir(parents=True, exist_ok=True)
    found = {}
    for path in changed:
        if not path.endswith(".cysound"):
            continue
        try:
            pointer = git("show", f"{revision}:{path}")
        except subprocess.CalledProcessError:
            continue  # a model that did not exist then
        target = folder / pathlib.Path(path).name
        target.write_bytes(git("lfs", "smudge", "--", path, data=pointer))
        found[pathlib.Path(path).name] = target
    return found


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--old", default="")
    parser.add_argument("--out", type=pathlib.Path,
                        default=pathlib.Path("D:/CyDAW/Comparaison des modeles/Comparaison des modeles.cydaw"))
    parser.add_argument("--exe", default=str(ex.DEFAULT_EXE))
    arguments = parser.parse_args()
    # Default: the commit before the one that rebuilt the factory models.
    revision = arguments.old or git("log", "-1", "--format=%H", "--grep=modèles de son d'usine refaits").decode().strip() + "^"
    old = old_models(revision, arguments.out.parent / "Anciens modeles")
    print(f"{len(old)} models changed since {revision}", flush=True)

    environment = dict(os.environ, CYDAW_NO_RECOVERY="1")
    process = subprocess.Popen([arguments.exe, "--control-port=%d" % ex.PORT, "--silent", "--window-size=1400x900"],
                               cwd=os.path.dirname(arguments.exe), env=environment)
    try:
        for _ in range(150):
            try:
                ex.request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        ex.new_piece()
        ex.request({"cmd": "set_tempo", "bpm": 90})
        instruments = ex.request({"cmd": "factory_instruments"}).get("instruments", [])
        at = 0
        for item in instruments:
            model = pathlib.Path(item.get("model") or "").name
            if model not in old:
                continue
            key, name = item["key"], item["name"]
            family = ex.family_of(key, item.get("category", ""))
            low, high = ex.instrument_range(item.get("map", ""))
            phrase = ex.fit_range(ex.family_phrase(family, 0, None), low, high, family)
            colour = ex.FAMILY_COLOURS[family]
            pair = []
            for label, file in (("ancien", old[model]), ("nouveau", None)):
                track = ex.request({"cmd": "add_factory_instrument", "key": key, "model": True})["track"]
                if file is not None:
                    ex.request({"cmd": "load_sound_model", "file": str(file).replace(os.sep, "/"), "track": track})
                renamed = f"{name} ({label})"
                ex.request({"cmd": "set_track", "track": track, "name": renamed})
                pair.append(renamed)
            # The arrangement: the old model's phrase, then the new's.
            for index, track in enumerate(pair):
                start = at + 8 * index
                ex.clip(track, "Ancien" if index == 0 else "Nouveau", start, 8,
                        [(note, when + start, length, velocity) for note, when, length, velocity in phrase], colour)
            # The launcher: the same phrase on each, in the first scene.
            for track in pair:
                ex.request({"cmd": "add_midi_clip", "track": track, "name": "Phrase", "scene": 0, "lengthBeats": 8,
                            "looping": True, "colour": colour})
                ex.request({"cmd": "add_notes", "track": track, "clip": "Phrase", "notes": ex.notes_of(phrase, 0)})
            print(f"{key:28s} {family:8s} {low:3d}-{high:3d}", flush=True)
            at += 16
        arguments.out.parent.mkdir(parents=True, exist_ok=True)
        ex.request({"cmd": "set_master_gain", "gainDb": 0.0})
        ex.request({"cmd": "save_project", "path": str(arguments.out).replace(os.sep, "/")})
        ex.request({"cmd": "set_master_gain", "gainDb": -60})
        print("saved", arguments.out, flush=True)
    finally:
        try:
            ex.request({"cmd": "quit"})
            process.wait(timeout=60)
        except Exception:  # noqa: BLE001
            process.kill()
    return 0


if __name__ == "__main__":
    sys.exit(main())
