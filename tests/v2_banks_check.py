"""Sound banks downloaded in the software, and the folders it keeps things in. Silent, offline.

    python tests/v2_banks_check.py --exe "<CyDAW 2.exe>" [--port 47829]

A local HTTP server plays GitHub: it serves a small bank (two notes, one with a space and a "#"
in its path, and its licence), listed in a registry of the test's own (CYDAW_BANKS_REGISTRY);
the banks, downloads and cache folders are the test's too (CYDAW_LOCATIONS_FILE).

* A sampler map naming "bank:" files: its samples are missing, counted with their size.
* Downloaded: the files land in the banks folder (the licence with them, nothing left in the
  downloads folder), the bank is complete, and the sampler, loaded again, plays them.
* A file of the banks folder is kept in a map as "bank:…".
* A map made when the factory instruments were FLAC files plays the banks' recordings instead,
  at the frames the FLACs were cut from.
* The sound models' folder changed: its models move along, nothing else; back to a folder of the
  test's own without moving.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import functools
import http.server
import json
import math
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47829
ROOT = Path(__file__).resolve().parents[1]


def request(command: dict) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(60)
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


def write_note(path: Path, hz: float, seconds: float = 1.0, rate: int = 44100) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = int(seconds * rate)
    data = bytearray()
    for frame in range(frames):
        value = 0.4 * math.sin(2 * math.pi * hz * frame / rate) * min(1.0, (frames - frame) / 2000)
        data += struct.pack("<hh", int(value * 32767), int(value * 32767))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(data))


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args) -> None:  # noqa: D401 - silence
        pass


def serve(folder: Path) -> tuple[http.server.ThreadingHTTPServer, int]:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(folder)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, server.server_address[1]


def launch(exe: str, environment: dict) -> subprocess.Popen:
    process = subprocess.Popen([str(Path(exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1500x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(200):
        try:
            request({"cmd": "ping"})
            break
        except OSError:
            time.sleep(0.2)
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master muted")
    return process


def close(process: subprocess.Popen) -> None:
    try:
        request({"cmd": "quit"})
    except OSError:
        pass
    try:
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()


def wait_for(condition, seconds: float = 20.0) -> bool:
    deadline = time.time() + seconds
    while time.time() < deadline:
        if condition():
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port

    scratch = Path(tempfile.mkdtemp(prefix="cydaw-banks-"))
    try:
        # The bank, as its repository has it.
        remote = scratch / "remote"
        write_note(remote / "Samples" / "C4.wav", 261.63)
        write_note(remote / "Samples" / "G 4#1.wav", 392.0)
        (remote / "LICENSE").write_text("CC0 1.0 Universal (test)\n", encoding="utf-8")
        server, http_port = serve(remote)
        files = [[path.relative_to(remote).as_posix(), path.stat().st_size] for path in sorted(remote.rglob("*")) if path.is_file()]
        registry = scratch / "registry"
        (registry / "BankLists").mkdir(parents=True)
        (registry / "BankLists" / "test-bank.json").write_text(json.dumps({"files": files}), encoding="utf-8")
        (registry / "banks.json").write_text(json.dumps({"format": "cydaw-banks", "version": 1, "banks": [{
            "id": "test-bank", "title": "Test bank", "author": "CyDAW tests", "licence": "CC0-1.0",
            "baseUrl": f"http://127.0.0.1:{http_port}/", "list": "BankLists/test-bank.json",
            "count": len(files), "bytes": sum(size for _, size in files)}]}), encoding="utf-8")
        # The folders, the test's.
        banks = scratch / "Banks"
        downloads = scratch / "Downloads"
        locations = scratch / "Locations.json"
        locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
            "banks": str(banks), "downloads": str(downloads), "cache": str(scratch / "Cache"),
            "soundModels": str(scratch / "ModelsA")}}), encoding="utf-8")
        environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations),
                           CYDAW_BANKS_REGISTRY=str(registry / "banks.json"))
        environment.pop("CYDAW_BANKS_DIR", None)
        process = launch(args.exe, environment)
        try:
            listed = need({"cmd": "locations"}, "locations")
            folders = {item["key"]: item["folder"] for item in listed["locations"]}
            check(Path(folders["banks"]) == banks and Path(folders["downloads"]) == downloads,
                  "the banks and downloads folders are the ones chosen")
            check(Path(listed["sampleCache"]) == scratch / "Cache" / "samples", "the sample index follows the cache folder")

            # A map playing the bank, before it is here.
            mapping = scratch / "Test.cysampler"
            mapping.write_text(json.dumps({"format": "cydaw-sampler", "version": 1, "name": "Bank test", "zones": [
                {"file": "bank:test-bank/Samples/C4.wav", "root": 60, "keys": [0, 64], "endFade": 400},
                {"file": "bank:test-bank/Samples/G 4#1.wav", "root": 67, "keys": [65, 127]}]}), encoding="utf-8")
            loaded = need({"cmd": "load_sampler", "file": str(mapping)}, "load the map")
            status = need({"cmd": "sampler_status", "track": loaded["track"]}, "sampler status")
            note_bytes = sum(size for path, size in files if path.endswith(".wav"))
            check(status["missingFiles"] == 2 and status["missingBytes"] == note_bytes,
                  f"its two samples are missing, {note_bytes} bytes ({status['missingFiles']}, {status['missingBytes']})")
            check(status["ready"] == 0, "nothing plays yet")
            before = need({"cmd": "banks", "count": True}, "banks")["banks"][0]
            check(before["here"] == 0, "nothing of the bank is here")

            # Downloaded.
            done = need({"cmd": "banks", "action": "download", "bank": "test-bank", "wait": 30}, "download")
            check(not done["download"]["running"], "the download ended")
            check(all((banks / "test-bank" / path).exists() for path, _ in files),
                  "every file is in the banks folder, the licence with them")
            check((banks / "test-bank" / "bank.json").exists(), "the bank says where it came from")
            check(not any(downloads.rglob("*.part")), "nothing is left in the downloads folder")
            after = need({"cmd": "banks", "count": True}, "banks")["banks"][0]
            check(after["here"] == len(files) and after["hereBytes"] == after["bytes"], "the bank is complete")
            check(wait_for(lambda: need({"cmd": "sampler_status", "track": loaded["track"], "wait": 2},
                                        "status")["ready"] == 2),
                  "the sampler, loaded again, plays them")
            status = need({"cmd": "sampler_status", "track": loaded["track"]}, "sampler status")
            check(status["missingFiles"] == 0, "nothing is missing any more")

            # A file of the banks folder is stored as "bank:".
            made = need({"cmd": "new_sampler", "files": [str(banks / "test-bank" / "Samples" / "C4.wav")]}, "new sampler")
            zones = need({"cmd": "sampler_map", "track": made["track"]}, "its map")["map"]["zones"]
            check(zones and zones[0]["file"] == "bank:test-bank/Samples/C4.wav",
                  f"a bank's file is kept as bank: ({zones[0]['file'] if zones else None})")

            # A map of the FLAC days, on the recordings now.
            legacy_path = ROOT / "content" / "factory" / "Instruments" / "legacy.json"
            legacy = json.loads(legacy_path.read_text(encoding="utf-8")) if legacy_path.exists() else {}
            if legacy:
                flac, entry = next((key, value) for key, value in legacy.items() if "Violin Arco" in key)
                old = scratch / "Old.cysampler"
                old.write_text(json.dumps({"format": "cydaw-sampler", "version": 1, "name": "Old violin", "zones": [
                    {"file": flac, "root": 67, "start": 10, "loop": [20000, 40000], "loopCrossfade": 100}]}),
                    encoding="utf-8")
                upgraded = need({"cmd": "load_sampler", "file": str(old)}, "old map")
                zone = need({"cmd": "sampler_map", "track": upgraded["track"]}, "its map")["map"]["zones"][0]
                check(zone["file"] == entry["file"] and zone["start"] == entry["offset"] + 10
                      and zone["end"] == entry["offset"] + entry["frames"]
                      and zone["loop"] == [entry["offset"] + 20000, entry["offset"] + 40000]
                      and zone.get("endFade") == entry["endFade"],
                      "a FLAC-era factory zone plays the recording, at the frames it was cut from")
            else:
                check(False, "Factory/Instruments/legacy.json is there")

            # The models' folder moved, its models with it.
            models_a, models_b = scratch / "ModelsA", scratch / "ModelsB"
            models_a.mkdir(exist_ok=True)
            (models_a / "Kept.cysound").write_text("{}", encoding="utf-8")
            (models_a / "notes.txt").write_text("not CyDAW's", encoding="utf-8")
            need({"cmd": "locations", "set": "soundModels", "folder": str(models_b), "take": True}, "move the models")
            check(wait_for(lambda: not need({"cmd": "locations"}, "locations")["move"]["active"]), "the move ends")
            folders = {item["key"]: item["folder"] for item in need({"cmd": "locations"}, "locations")["locations"]}
            check(Path(folders["soundModels"]) == models_b, "the models are kept in the new folder")
            check((models_b / "Kept.cysound").exists() and not (models_a / "Kept.cysound").exists(),
                  "the model moved")
            check((models_a / "notes.txt").exists() and not (models_b / "notes.txt").exists(),
                  "what is not CyDAW's stayed where it was")
            saved = json.loads(locations.read_text(encoding="utf-8"))["folders"]
            check(Path(saved.get("soundModels", "")) == models_b, "the choice is saved")
            need({"cmd": "locations", "set": "soundModels", "folder": str(models_a), "take": False}, "back without moving")
            folders = {item["key"]: item["folder"] for item in need({"cmd": "locations"}, "locations")["locations"]}
            check(Path(folders["soundModels"]) == models_a and (models_b / "Kept.cysound").exists(),
                  "changed without moving, the model stays")
        finally:
            close(process)
            server.shutdown()
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL " + str(error), flush=True)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print("banks check: %d failure(s)" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
