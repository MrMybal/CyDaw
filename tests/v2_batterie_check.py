"""Batterie in the software: an acoustic kit played piece by piece. Silent, offline.

    python tests/v2_batterie_check.py --exe "<CyDAW 2.exe>" [--port 47846]

The kits are free banks CyDAW downloads (Options > Banques de sons): the test reads where they are
from the person's Locations file and says it skips when they are not there. The presets folder is
the test's own. A hit is heard through the exported plug-in's path, in memory; the mix through a
file: nothing is played.

* A Batterie track comes with its first kit; another kit loads by its name.
* Each piece sounds; a muted piece is silent; a piece's level moves it by its decibels.
* A groove becomes a MIDI clip on the track, on keys the kit plays, and the mix sounds in its bars.
* Its factory presets are there; one of another kit loads that kit.
* Its window opens; a saved project opened again keeps its kit and its mix.

Exit code 0 when all hold (or when the banks are not there).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import socket
import subprocess
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47846


def request(command: dict) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(120)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(1 << 20)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode("utf-8"))


def need(command: dict, what: str) -> dict:
    answer = request(command)
    if not answer.get("ok"):
        raise RuntimeError(f"{what}: {answer.get('error')}")
    return answer.get("result", {})


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def banks_folder() -> str | None:
    path = Path(os.environ.get("APPDATA", "")) / "CyDAW" / "Locations.json"
    if path.is_file():
        banks = json.loads(path.read_text(encoding="utf-8")).get("folders", {}).get("banks")
        if banks and (Path(banks) / "big-rusty-drums").is_dir() and (Path(banks) / "virtuosity-drums").is_dir():
            return banks
    return None


def read_wav(path: Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as file:
        rate = file.getframerate()
        width = file.getsampwidth()
        channels = file.getnchannels()
        raw = file.readframes(file.getnframes())
    step = width * channels
    out = []
    for at in range(0, len(raw) - step + 1, step):
        frame = raw[at:at + width]
        if width == 3:
            out.append(int.from_bytes(frame + (b"\xff" if frame[2] & 0x80 else b"\x00"), "little", signed=True) / 8_388_608.0)
        elif width == 4:
            out.append(int.from_bytes(frame, "little", signed=True) / 2_147_483_648.0)
        else:
            out.append(int.from_bytes(frame, "little", signed=True) / 32_768.0)
    return out, rate


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    banks = banks_folder()
    if banks is None:
        print("skip: the drum banks are not on this computer (Options > Banques de sons downloads them)")
        return 0
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-batterie-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache"), "banks": banks}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1400x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(300):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        if not need({"cmd": "status"}, "status").get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
        need({"cmd": "new_project"}, "new project")
        track = need({"cmd": "add_midi_track", "name": "Batterie", "instrument": "batterie-instrument"}, "track")["name"]

        def batterie(**fields) -> dict:
            return need(dict({"cmd": "batterie", "track": track}, **fields), "batterie")

        def hit(note: int) -> dict:
            return need({"cmd": "instrument_export_check", "track": track, "note": note, "velocity": 0.9, "seconds": 1.5}, "hit")

        # ---- Its kit ----
        first = batterie()
        check(first.get("kitInstrument") == "drums-rusty" and first.get("zones", 0) > 500,
              f"a Batterie track comes with its first kit ({first.get('kit')}, {first.get('zones')} zones)")
        check(len(first.get("kits", [])) == 6 and "rock" in first.get("grooves", []), "it lists its kits and its grooves")
        node = first["nodeId"]

        # ---- Its pieces ----
        heard = {note: hit(note) for note in (36, 38, 42, 49)}
        check(all(item.get("rms", 0.0) > 1.0e-4 and item.get("peakDb", 0.0) < 0.0 for item in heard.values()),
              "the kick, the snare, the hi-hat and the crash sound: "
              + ", ".join(f"{note} {item.get('peakDb', -200):.1f} dB" for note, item in heard.items()))
        batterie(set={"snare.mute": 1, "kick.level": -12})
        muted = hit(38)
        lower = hit(36)
        check(muted.get("rms", 1.0) == 0.0, "a muted piece is silent")
        ratio = lower.get("rms", 0.0) / max(1e-12, heard[36].get("rms", 0.0))
        check(abs(20.0 * math.log10(max(ratio, 1e-9)) + 12.0) < 0.5, f"a piece's level moves it by its decibels ({20.0 * math.log10(max(ratio, 1e-9)):.2f} dB)")
        batterie(init=True)
        problems = batterie(set={"nothing.here": 1}).get("problems", [])
        check(any("nothing.here" in problem for problem in problems), "an unknown setting is reported")

        # ---- Another kit ----
        jazz = batterie(kit="Jazz")
        check(jazz.get("kitInstrument") == "drums-jazz" and jazz.get("zones", 0) > 100 and jazz["values"]["kit.kit"] == 1,
              f"another kit loads by its name ({jazz.get('kit')}, {jazz.get('zones')} zones)")
        check(hit(51).get("rms", 0.0) > 1.0e-4, "the jazz kit's ride sounds")

        # ---- A groove ----
        groove = need({"cmd": "groove", "track": track, "name": "funk", "bars": 2, "fill": "fill-toms", "startBeats": 0}, "groove")
        notes = need({"cmd": "list_notes", "track": track}, "notes")
        clips = notes.get("clips", [])
        keys = sorted({note["note"] for clip in clips for note in clip.get("notes", [])})
        check(groove.get("hits", 0) > 20 and clips and len(clips[0].get("notes", [])) > 20,
              f"a groove becomes a MIDI clip ({groove.get('hits')} hits)")
        check(all(35 <= key <= 84 for key in keys), f"on keys the kit plays ({keys})")
        need({"cmd": "set_master_gain", "gainDb": 0.0, "muted": False}, "master up")
        mix_path = scratch / "groove.wav"
        need({"cmd": "export_mix", "path": str(mix_path)}, "export")
        for _ in range(900):
            time.sleep(0.1)
            if not need({"cmd": "status"}, "status").get("exporting"):
                break
        need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master down")
        samples, rate = read_wav(mix_path)
        # 120 BPM: two bars are four seconds; every beat has a hit.
        beats = [max((abs(x) for x in samples[int(b * 0.5 * rate):int((b + 1) * 0.5 * rate)]), default=0.0) for b in range(8)]
        check(all(peak > 0.003 for peak in beats), "the mix sounds on every beat of the groove: "
              + " ".join(f"{20 * math.log10(max(peak, 1e-9)):.0f}" for peak in beats))

        # ---- Presets ----
        listed = need({"cmd": "presets", "track": track, "nodeId": node}, "presets")
        factory = [item["name"] for item in listed.get("presets", []) if item.get("factory")]
        check(len(factory) >= 10 and "Balais intimes" in factory, f"Batterie's factory presets ({len(factory)})")
        if "Balais intimes" in factory:
            need({"cmd": "presets", "track": track, "nodeId": node, "action": "load", "name": "Balais intimes"}, "load")
            loaded = batterie()
            check(loaded.get("kitInstrument") == "drums-unruly-brushes" and abs(loaded["values"]["kick.level"] + 3.0) < 1e-4,
                  f"a preset of another kit loads that kit and its mix ({loaded.get('kit')})")

        # ---- Its window, a project saved and opened ----
        need({"cmd": "get_track_graph", "track": track}, "select")
        need({"cmd": "show_dialog", "name": "batterie"}, "window")
        batterie(kit="Swirly", set={"tom2.tune": -2.5, "hihat.decay": 0.4})
        project = scratch / "Batterie.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        kept = {}
        for _ in range(150):
            answer = request({"cmd": "batterie", "track": track})
            if answer.get("ok"):
                kept = answer["result"]
                break
            time.sleep(0.2)
        values = kept.get("values", {})
        check(kept.get("kitInstrument") == "drums-swirly" and abs(values.get("tom2.tune", 0) + 2.5) < 1e-4
              and abs(values.get("hihat.decay", 0) - 0.4) < 1e-4 and kept.get("zones", 0) > 100,
              "a saved project keeps Batterie's kit and mix")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=20)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
