"""Kick and the CyDAW Electro bank, in the software. Silent, offline.

    python tests/v2_electro_check.py --exe "<CyDAW 2.exe>" [--port 47839]

The banks, downloads, cache and presets folders are the test's own (CYDAW_LOCATIONS_FILE).

* CyDAW Electro is a generated bank: made here, every file of exactly its listed size, the WAVs
  16-bit mono 48 kHz peaking at -1 dBFS, its licence as listed, nothing left in the downloads
  folder; a file made again is the same, byte for byte.
* Its kits load in the sampler and play (the kick louder than the closed hat).
* A Kick track: its settings by key, what it plays as an exported plug-in, its factory presets
  (one loaded changes its settings), its settings kept by a saved project opened again.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47839
ROOT = Path(__file__).resolve().parents[1]


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


def peak_of(path: Path) -> tuple[int, int, int, float]:
    with wave.open(str(path), "rb") as reader:
        channels, width, rate, frames = reader.getnchannels(), reader.getsampwidth(), reader.getframerate(), reader.getnframes()
        data = reader.readframes(frames)
    samples = struct.unpack(f"<{len(data) // 2}h", data)
    peak = max(abs(x) for x in samples) if samples else 0
    return channels, width, rate, peak / 32767.0


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-electro-check-"))
    banks = scratch / "Banks"
    downloads = scratch / "Downloads"
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "banks": str(banks), "downloads": str(downloads), "cache": str(scratch / "Cache"),
        "presets": str(scratch / "Presets")}}), encoding="utf-8")
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
        status = need({"cmd": "status"}, "status")
        if not status.get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")

        # ---- The generated bank ----
        listed = need({"cmd": "banks"}, "banks")
        electro = next((bank for bank in listed["banks"] if bank["id"] == "cydaw-electro"), None)
        check(electro is not None and electro.get("generator") == "cydaw-drums", "CyDAW Electro is a generated bank")
        if electro is None:
            return 1
        entries = json.loads((ROOT / "content" / "factory" / "BankLists" / "cydaw-electro.json").read_text(encoding="utf-8"))["files"]
        started = time.time()
        need({"cmd": "banks", "action": "download", "bank": "cydaw-electro", "wait": 120}, "generate")
        elapsed = time.time() - started
        folder = banks / "cydaw-electro"
        sizes_ok = all((folder / path).is_file() and (folder / path).stat().st_size == size for path, size, *_ in entries)
        check(sizes_ok, f"its {len(entries)} files made, each of its listed size ({elapsed:.1f} s)")
        wavs = [folder / path for path, *_ in entries if path.endswith(".wav")]
        formats = [peak_of(path) for path in wavs if path.is_file()]
        check(all(f[:3] == (1, 2, 48_000) for f in formats) and len(formats) == len(wavs), "16-bit mono 48 kHz WAVs")
        peaks = [f[3] for f in formats]
        check(all(abs(p - 10 ** (-1 / 20)) < 0.01 for p in peaks),
              f"each peaks at -1 dBFS ({min(peaks):.3f}..{max(peaks):.3f})")
        licence = next(entry for entry in entries if entry[0] == "LICENCE.txt")
        check((folder / "LICENCE.txt").read_text(encoding="utf-8") == licence[3]["text"], "its licence as listed")
        check((folder / "bank.json").is_file() and json.loads((folder / "bank.json").read_text(encoding="utf-8")).get("generator")
              == "cydaw-drums", "its record says it was generated")
        left = [path for path in downloads.rglob("*") if path.is_file()] if downloads.exists() else []
        check(not left, "nothing left in the downloads folder")
        counted = need({"cmd": "banks", "count": True, "bank": "cydaw-electro"}, "count")["banks"][0]
        check(counted.get("here") == electro["files"], f"the bank is complete ({counted.get('here')}/{electro['files']})")
        # The same recipe, the same file.
        again = folder / "Kicks" / "909 Punch.wav"
        before = again.read_bytes()
        again.unlink()
        need({"cmd": "banks", "action": "download", "bank": "cydaw-electro", "wait": 60}, "generate again")
        check(again.is_file() and again.read_bytes() == before, "a file made again is the same, byte for byte")

        # ---- Its kits ----
        instruments = {item["key"]: item for item in need({"cmd": "factory_instruments"}, "instruments")["instruments"]}
        kits = [key for key in instruments if key.startswith("electro-")]
        check(len(kits) == 10, f"ten electronic kits ({len(kits)})")
        for key in ("electro-808", "electro-techno", "electro-kicks"):
            added = need({"cmd": "add_factory_instrument", "key": key}, key)
            state = need({"cmd": "sampler_status", "track": added["track"], "wait": 60}, key + " status")
            check(state.get("ready") == state.get("zones") and state.get("failed") == 0,
                  f"{key}: every sound found ({state.get('ready')}/{state.get('zones')})")
            kick = need({"cmd": "instrument_export_check", "track": added["track"], "note": 36, "velocity": 1.0,
                         "seconds": 0.6}, key + " kick")
            check(kick.get("rms", 0) > 0.02 and kick.get("peakDb", 0) < 0.5, f"{key}: C2 plays (rms {kick.get('rms', 0):.3f})")
            if key != "electro-kicks":
                hat = need({"cmd": "instrument_export_check", "track": added["track"], "note": 42, "velocity": 1.0,
                            "seconds": 0.6}, key + " hat")
                check(-60 < hat.get("peakDb", -200) < kick.get("peakDb", 0) - 3,
                      f"{key}: the closed hat sits under the kick ({hat.get('peakDb', -200):.1f} dB)")

        # The hats choke: the closed one cuts the open one ringing.
        kit = need({"cmd": "add_factory_instrument", "key": "electro-909"}, "909")["track"]
        need({"cmd": "sampler_status", "track": kit, "wait": 60}, "909 status")
        ringing = need({"cmd": "instrument_export_check", "track": kit, "notes": [[46, 1.0, 0.0]], "seconds": 0.6}, "open hat")
        choked = need({"cmd": "instrument_export_check", "track": kit, "notes": [[46, 1.0, 0.0], [42, 1.0, 0.2]], "seconds": 0.6},
                      "open then closed hat")
        # The windows from 0.35 to 0.5 s: the open hat still rings alone; the closed one is long over.
        late = lambda answer: max(answer["windowsDb"][7:10])  # noqa: E731
        check(late(choked) < late(ringing) - 15.0,
              f"the closed hat chokes the open one ({late(ringing):.1f} dB ringing, {late(choked):.1f} dB choked)")

        # A kit whose sound is gone: made again by itself, nothing asked.
        gone = folder / "Snares" / "808 Snare.wav"
        gone.unlink()
        added = need({"cmd": "add_factory_instrument", "key": "electro-808"}, "808 again")
        state = need({"cmd": "sampler_status", "track": added["track"], "wait": 60}, "808 again status")
        for _ in range(100):
            if gone.is_file():
                break
            time.sleep(0.1)
        check(gone.is_file() and gone.read_bytes()[:4] == b"RIFF", "a kit's missing sound is made again when the kit is added")

        # ---- Kick ----
        track = need({"cmd": "add_midi_track", "name": "Kick", "instrument": "kick-instrument"}, "kick track")["name"]
        values = need({"cmd": "kick", "track": track}, "kick")
        check(values["values"]["pitchEnd"] == 52.0 and abs(values["peakDb"] + 1.4) < 0.5, "Kick's defaults")
        changed = need({"cmd": "kick", "track": track, "set": {"pitchEnd": 60, "clickType": "Blip", "driveType": "Tube",
                                                              "drive": 6, "nothing": 1}}, "kick set")
        check(changed["values"]["pitchEnd"] == 60.0 and changed["values"]["clickType"] == 3.0
              and changed["values"]["driveType"] == 3.0, "its settings by key, a choice by name")
        check(any("nothing" in problem for problem in changed.get("problems", [])), "an unknown key is said")
        heard = need({"cmd": "instrument_export_check", "track": track, "note": 36, "velocity": 1.0, "seconds": 1.0}, "kick plays")
        check(heard.get("waveform") == 9 and heard.get("rms", 0) > 0.02 and heard.get("tailRms", 1) < 1e-4,
              f"as a plug-in it plays a kick that ends (rms {heard.get('rms', 0):.3f}, tail {heard.get('tailRms', 1):.1e})")
        presets = need({"cmd": "presets", "track": track, "nodeId": values["nodeId"]}, "presets")
        factory = [item["name"] for item in presets.get("presets", []) if item.get("factory")]
        check(len(factory) >= 16 and "909 Punch" in factory, f"its factory presets ({len(factory)})")
        need({"cmd": "presets", "track": track, "nodeId": values["nodeId"], "action": "load", "name": "909 Punch"}, "load")
        loaded = need({"cmd": "kick", "track": track}, "kick after preset")
        check(loaded["values"]["pitchStart"] == 380.0 and abs(loaded["peakDb"] + 1.0) < 0.2, "a preset loaded sets it")
        need({"cmd": "kick", "track": track, "set": {"decay": 777}}, "kick decay")
        project = scratch / "Kick.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        # Opened in the background: asked again until it is.
        reopened = {}
        for _ in range(150):
            answer = request({"cmd": "kick", "track": track})
            if answer.get("ok"):
                reopened = answer["result"]
                break
            time.sleep(0.2)
        check(reopened.get("values", {}).get("decay") == 777.0 and reopened["values"]["pitchStart"] == 380.0,
              "a saved project keeps its settings")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print("FAILED: " + "; ".join(failures) if failures else "All CyDAW Electro and Kick checks passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
