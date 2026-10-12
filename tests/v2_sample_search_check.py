"""The sample library with the banks, its categories, and the optional semantic search. Silent, offline.

    python tests/v2_sample_search_check.py --exe "<CyDAW 2.exe>" [--port 47841]
        [--clap-model DIR --clap-python PYTHON]

Everything is the test's own: its banks (a registry of its own, CYDAW_BANKS_REGISTRY), its folders
(CYDAW_LOCATIONS_FILE), the library's settings (CYDAW_SAMPLES_SETTINGS), the models' catalogue
(CYDAW_MODELS_CATALOGUE, its files served by a local server).

* The banks' sounds are in the library with their source; a file a factory instrument plays (the alto
  recorder's) takes the catalogue's category and instrument, and is one note (no tempo); a drum bank's
  files are named by their names, a folder of the person's by its own; leaving the banks out and taking
  them back; the counts by category, family and source; searching by family, source, instrument.
* A model whose licence asks to be accepted is refused without it; with it, it is downloaded, checked
  (SHA-256), and its receipt says the licence was accepted by the person. A file that is not the one
  expected is refused, nothing of it installed.
* With a real CLAP (--clap-model, the folder of laion/clap-htsat-unfused, and --clap-python, a Python
  with torch and transformers): served by the local server and installed as from Hugging Face, the
  library indexed, "kick drum" finds the kick first, French is put into English, the likeness of the
  kick puts the kick-like sounds first, and the categories the names gave never change.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import functools
import hashlib
import http.server
import json
import math
import os
import random
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
port = 47841
ROOT = Path(__file__).resolve().parents[1]
RATE = 44100


def request(command: dict, timeout: float = 120.0) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(timeout)
        connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
        data = b""
        while not data.endswith(b"\n"):
            chunk = connection.recv(1 << 20)
            if not chunk:
                break
            data += chunk
    return json.loads(data.decode("utf-8"))


def need(command: dict, what: str, timeout: float = 120.0) -> dict:
    answer = request(command, timeout)
    if not answer.get("ok"):
        raise RuntimeError(f"{what}: {answer.get('error')}")
    return answer.get("result", {})


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def wait_for(condition, seconds: float = 30.0, step: float = 0.3) -> bool:
    deadline = time.time() + seconds
    while time.time() < deadline:
        if condition():
            return True
        time.sleep(step)
    return False


def write_wav(path: Path, samples: list[float], channels: int = 1) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray()
    for value in samples:
        clipped = max(-1.0, min(1.0, value))
        for _ in range(channels):
            data += struct.pack("<h", int(clipped * 32767))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(channels)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(bytes(data))


def kick(seconds: float = 0.6) -> list[float]:
    out, phase = [], 0.0
    for n in range(int(seconds * RATE)):
        t = n / RATE
        phase += 2 * math.pi * (45 + 160 * math.exp(-t * 28)) / RATE
        out.append(0.9 * math.sin(phase) * math.exp(-t * 7))
    return out


def snare(seconds: float = 0.4) -> list[float]:
    rng = random.Random(3)
    return [(0.5 * rng.uniform(-1, 1) + 0.4 * math.sin(2 * math.pi * 190 * n / RATE)) * math.exp(-n / RATE * 18)
            for n in range(int(seconds * RATE))]


def hat(seconds: float = 0.15) -> list[float]:
    rng = random.Random(5)
    previous, out = 0.0, []
    for n in range(int(seconds * RATE)):
        value = rng.uniform(-1, 1)
        out.append(0.6 * (value - previous) * math.exp(-n / RATE * 45))
        previous = value
    return out


def tone(hz: float, seconds: float = 1.5) -> list[float]:
    return [0.4 * sum(math.sin(2 * math.pi * hz * k * n / RATE) / k for k in (1, 2, 3)) * min(1.0, n / 400) * math.exp(-n / RATE * 0.8)
            for n in range(int(seconds * RATE))]


def loop(bpm: float = 120.0, bars: int = 1) -> list[float]:
    beat = 60.0 / bpm
    out = [0.0] * int(bars * 4 * beat * RATE)
    for index in range(bars * 4):
        start = int(index * beat * RATE)
        for offset, value in enumerate(kick(0.3) if index % 2 == 0 else snare(0.25)):
            if start + offset < len(out):
                out[start + offset] += value
    return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args) -> None:  # noqa: D401 - silence
        pass


def serve(folder: Path) -> tuple[http.server.ThreadingHTTPServer, int]:
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(Quiet, directory=str(folder)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, server.server_address[1]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def launch(exe: str, environment: dict) -> subprocess.Popen:
    process = subprocess.Popen([str(Path(exe).resolve()), f"--control-port={port}", "--silent", "--window-size=1500x900"],
                               env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
        process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        process.kill()


def records_by_name(status_samples: list[dict]) -> dict[str, dict]:
    return {item["name"]: item for item in status_samples}


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    parser.add_argument("--clap-model", default=os.environ.get("CYDAW_TEST_CLAP_MODEL", ""))
    parser.add_argument("--clap-python", default=os.environ.get("CYDAW_TEST_CLAP_PYTHON", ""))
    args = parser.parse_args()
    port = args.port

    scratch = Path(tempfile.mkdtemp(prefix="cydaw-samplesearch-"))
    process = None
    try:
        # ---- The banks: a drum bank, and the alto recorder's note where VCSL keeps it.
        banks = scratch / "Banks"
        recorder = "Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_F3_rr1_Main.wav"
        write_wav(banks / "vcsl" / recorder, tone(174.6))
        write_wav(banks / "test-drums" / "Kicks" / "Deep Kick.wav", kick())
        write_wav(banks / "test-drums" / "Snares" / "Snare Tight.wav", snare())
        write_wav(banks / "test-drums" / "Hats" / "Closed Hat.wav", hat())
        # Nothing in its name nor its folder says what it is: a kick.
        write_wav(banks / "test-drums" / "Misc" / "Thing 01.wav", kick(0.5))
        registry = scratch / "registry"
        (registry / "BankLists").mkdir(parents=True)
        entries = []
        for bank, title in (("vcsl", "VCSL"), ("test-drums", "Test drums")):
            files = [[p.relative_to(banks / bank).as_posix(), p.stat().st_size] for p in sorted((banks / bank).rglob("*.wav"))]
            (registry / "BankLists" / f"{bank}.json").write_text(json.dumps({"files": files}), encoding="utf-8")
            entries.append({"id": bank, "title": title, "author": "CyDAW tests", "licence": "CC0-1.0",
                            "baseUrl": "http://127.0.0.1:9/", "list": f"BankLists/{bank}.json", "count": len(files),
                            "bytes": sum(size for _, size in files)})
        (registry / "banks.json").write_text(json.dumps({"format": "cydaw-banks", "version": 1, "banks": entries}),
                                             encoding="utf-8")
        # A folder of the person's.
        mine = scratch / "My Samples"
        write_wav(mine / "Loops" / "Drum Loop 120bpm.wav", loop(), channels=2)

        # ---- The models: a test model whose licence asks to be accepted, a broken one, and CLAP when given.
        remote = scratch / "remote"
        (remote / "fake").mkdir(parents=True)
        (remote / "fake" / "config.json").write_text('{"model": "fake"}', encoding="utf-8")
        (remote / "fake" / "weights.bin").write_bytes(os.urandom(300_000))
        (remote / "broken").mkdir(parents=True)
        (remote / "broken" / "weights.bin").write_bytes(b"not the expected file")
        clap_dir = Path(args.clap_model) if args.clap_model else None
        real = clap_dir is not None and (clap_dir / "pytorch_model.bin").is_file() and Path(args.clap_python or "-").is_file()
        if real:
            (remote / "clap").mkdir()
            for file in clap_dir.iterdir():
                if file.is_file() and not file.name.endswith(".json.installed.json") and not file.name.endswith(".installed.json") \
                        and file.name != "receipt.json":
                    # A link where it can, a copy otherwise: the server reads it either way.
                    target = remote / "clap" / file.name
                    try:
                        os.link(file, target)
                    except OSError:
                        shutil.copyfile(file, target)
        server, http_port = serve(remote)
        base = f"http://127.0.0.1:{http_port}/"

        def file_entry(path: Path, name: str) -> dict:
            return {"path": name, "size": path.stat().st_size, "sha256": sha256(path)}

        licence = {"name": "Test-Licence", "url": base + "fake/LICENSE", "page": base, "requiresAcceptance": True,
                   "note": {"fr": "Licence de test", "en": "Test licence"}}
        models = [
            {"id": "fake", "version": "1", "title": "Fake model", "provider": "tests", "kind": "clap", "default": False,
             "description": {"fr": "test", "en": "test"}, "homepage": base, "licence": licence,
             "source": {"host": "127.0.0.1", "revision": "test", "baseUrl": base + "fake/"},
             "files": [file_entry(remote / "fake" / name, name) for name in ("config.json", "weights.bin")],
             "dimension": 512, "sampleRate": 48000, "windowSeconds": 10, "similarity": [0.05, 0.5]},
            {"id": "broken", "version": "1", "title": "Broken model", "provider": "tests", "kind": "clap",
             "description": {"fr": "test", "en": "test"}, "homepage": base,
             "licence": {"name": "Test", "url": base, "requiresAcceptance": False},
             "source": {"host": "127.0.0.1", "revision": "test", "baseUrl": base + "broken/"},
             "files": [{"path": "weights.bin", "size": len(b"not the expected file"), "sha256": "0" * 64}],
             "dimension": 512},
        ]
        if real:
            catalogue = json.loads((ROOT / "content" / "factory" / "models.json").read_text(encoding="utf-8"))
            clap = next(model for model in catalogue["models"] if model["id"] == "laion-clap")
            clap = dict(clap, default=True, source=dict(clap["source"], baseUrl=base + "clap/"))
            models.append(clap)
        catalogue_file = scratch / "models.json"
        catalogue_file.write_text(json.dumps({"format": "cydaw-ai-models", "version": 1, "models": models}), encoding="utf-8")

        locations = scratch / "Locations.json"
        locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
            "banks": str(banks), "downloads": str(scratch / "Downloads"), "cache": str(scratch / "Cache"),
            "aiModels": str(scratch / "Models")}}), encoding="utf-8")
        environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations),
                           CYDAW_BANKS_REGISTRY=str(registry / "banks.json"),
                           CYDAW_SAMPLES_SETTINGS=str(scratch / "samples.settings"),
                           CYDAW_MODELS_CATALOGUE=str(catalogue_file))
        environment.pop("CYDAW_BANKS_DIR", None)
        if real:
            environment["CYDAW_COMPANION_PYTHON"] = str(Path(args.clap_python))
        else:
            # A Python without the model's packages: the engine says so.
            environment["CYDAW_COMPANION_PYTHON"] = sys.executable
        process = launch(args.exe, environment)

        # ---- The library, the banks in it.
        def status() -> dict:
            return need({"cmd": "samples_status"}, "samples status")

        check(wait_for(lambda: status()["loaded"], 30), "the library is read")
        need({"cmd": "sample_folders", "add": str(mine)}, "add a folder")
        check(wait_for(lambda: not status()["scanning"] and status()["samples"] == 6, 60),
              f"the banks and the folder are scanned (6 sounds: {status()['samples']})")
        found = records_by_name(need({"cmd": "search_samples", "limit": 50}, "list")["samples"])
        note = found.get("AltRecorder_Sus_F3_rr1_Main", {})
        check(note.get("category") == "woodwinds" and note.get("instrument", "") != "" and note.get("source") == "vcsl"
              and note.get("classifier") == "bank" and note.get("bpm", 1) == 0,
              f"the recorder's note: woodwinds, its instrument, from VCSL, no tempo ({note.get('category')}, "
              f"{note.get('instrument')}, {note.get('classifier')}, {note.get('bpm')})")
        check(found.get("Deep Kick", {}).get("category") == "kick" and found["Deep Kick"].get("source") == "test-drums"
              and found["Deep Kick"].get("family") == "drums", "the kick: named by its name, from its bank, a drum")
        check(found.get("Snare Tight", {}).get("category") == "snare", "the snare: named by its name")
        check(found.get("Closed Hat", {}).get("category") == "hat", "the hat: named by its name")
        check(found.get("Thing 01", {}).get("category", "x") == "", "a sound nothing names has no category")
        loop_record = found.get("Drum Loop 120bpm", {})
        check(loop_record.get("category") == "drums" and loop_record.get("source") == "" and abs(loop_record.get("bpm", 0) - 120) < 0.5,
              f"the person's loop: drums, from their folder, 120 BPM ({loop_record.get('category')}, {loop_record.get('bpm')})")
        counts = status()
        check(counts["categoryCounts"].get("kick") == 1 and counts["categoryCounts"].get("-") == 1
              and counts["familyCounts"].get("drums") == 4 and counts["sourceCounts"].get("test-drums") == 4
              and counts["sourceCounts"].get("folders") == 1, f"the counts by category, family, source ({counts['categoryCounts']}, "
              f"{counts['familyCounts']}, {counts['sourceCounts']})")
        check(need({"cmd": "search_samples", "family": "drums"}, "family")["matched"] == 4, "searched by family")
        check(need({"cmd": "search_samples", "source": "vcsl"}, "source")["matched"] == 1, "searched by bank")
        check(need({"cmd": "search_samples", "instrument": note.get("instrument", "?")[:6]}, "instrument")["matched"] == 1,
              "searched by instrument")
        need({"cmd": "sample_folders", "includeBanks": False}, "banks out")
        check(wait_for(lambda: status()["samples"] == 1, 20), "without the banks: the folder's sound alone")
        need({"cmd": "sample_folders", "includeBanks": True}, "banks in")
        check(wait_for(lambda: not status()["scanning"] and status()["samples"] == 6, 60), "the banks back")
        # ---- The library's big window: the multi-column filter, its counts, the order, its tabs.
        def window(**command) -> dict:
            return need(dict(command, cmd="library_window"), "library window")

        check(wait_for(lambda: window(show=True, tab="samples", query="")["total"] == 6, 20),
              "the big window shows the six sounds")
        both = window(filters={"category": ["kick", "snare"]})
        check(both["rows"] == 2 and set(both["names"]) == {"Deep Kick", "Snare Tight"},
              f"two categories chosen in one column: both kept ({both['names']})")
        counts = both["facets"]["category"]
        check(counts.get("kick") == 1 and counts.get("hat") == 1 and counts.get("woodwinds") == 1,
              f"a column's counts are what choosing there would bring, its own choice aside ({counts})")
        check(both["facets"]["source"].get("bank:test-drums") == 2 and "bank:vcsl" not in both["facets"]["source"],
              f"the other columns count what is left ({both['facets']['source']})")
        crossed = window(filters={"category": ["kick", "snare"], "type": ["one-shot"], "source": ["bank:test-drums"]})
        check(crossed["rows"] == 2, f"three columns at once ({crossed['rows']})")
        words = window(query="loop", filters={})
        check(words["rows"] == 1 and words["names"] == ["Drum Loop 120bpm"], f"words over names and categories ({words['names']})")
        longest = window(query="", filters={}, sort="length", ascending=False)
        check(longest["names"][:1] == ["Drum Loop 120bpm"], f"sorted by length, longest first ({longest['names'][:3]})")
        check(window(tab="effects")["tab"] == "effects" and window(tab="instruments")["tab"] == "instruments",
              "the instruments' and effects' tabs open")
        out = window(tab="samples", detached=True)
        check(out["detached"], "the window goes out of the studio's")
        check(not window(detached=False)["detached"], "and back in")
        perf = need({"cmd": "library_perf", "items": 50000}, "library perf", 120)
        slowest = max(query["ms"] for query in perf["queries"].values())
        print(f"     50 000 sounds: built in {perf['buildMs']:.0f} ms (its own thread), slowest query {slowest:.1f} ms", flush=True)
        check(perf["items"] == 50000 and slowest < 60.0, f"a query over 50 000 sounds stays under 60 ms ({slowest:.1f} ms)")
        window(show=False)

        # The person's category stays theirs.
        need({"cmd": "set_sample", "path": str(banks / "test-drums" / "Hats" / "Closed Hat.wav"), "category": "perc"}, "set category")

        # ---- The models: licences, downloads, checks.
        listed = need({"cmd": "ai_models"}, "models")
        fake = next(model for model in listed["models"] if model["id"] == "fake")
        check(fake["requiresAcceptance"] and fake["licence"] == "Test-Licence" and not fake["installed"],
              "a model's licence is listed, it asks to be accepted, nothing is installed")
        refused = request({"cmd": "ai_models", "install": "fake"})
        check(not refused.get("ok") and "licen" in refused.get("error", "").lower(),
              f"without its licence accepted it is refused ({refused.get('error')})")
        # The test is the person here: its own test licence.
        need({"cmd": "ai_models", "install": "fake", "acceptLicence": True}, "install fake")
        check(wait_for(lambda: not need({"cmd": "ai_models"}, "models")["install"]["running"], 30), "the download ends")
        listed = need({"cmd": "ai_models"}, "models")
        fake = next(model for model in listed["models"] if model["id"] == "fake")
        receipt = fake.get("receipt") or {}
        check(fake["installed"] and receipt.get("licence", {}).get("acceptedByPerson") is True
              and len(receipt.get("files", [])) == 2,
              "installed, its receipt says the licence was accepted by the person and lists its files")
        check(not any((scratch / "Downloads").rglob("*.part")), "nothing left in the downloads folder")
        need({"cmd": "ai_models", "install": "broken"}, "install broken")
        check(wait_for(lambda: not need({"cmd": "ai_models"}, "models")["install"]["running"], 30), "the broken download ends")
        listed = need({"cmd": "ai_models"}, "models")
        broken = next(model for model in listed["models"] if model["id"] == "broken")
        check(not broken["installed"] and "SHA-256" in listed["install"]["error"],
              f"a file that is not the one expected is refused ({listed['install']['error']})")
        check(not (scratch / "Models" / "broken" / "1" / "weights.bin").exists(), "nothing of it is kept")

        if not real:
            # The fake model cannot run: the search says why, and CyDAW goes on.
            need({"cmd": "ai_models", "use": "fake", "checkEngine": True}, "use fake")
            check(wait_for(lambda: need({"cmd": "semantic_index"}, "index")["engine"]["checked"], 60),
                  "the engine is checked")
            engine = need({"cmd": "semantic_index"}, "index")["engine"]
            if not engine["ready"]:
                check(engine["error"] != "", f"a Python without torch or transformers is said not ready ({engine['error']})")
            else:
                # This Python has them: the fake model itself cannot load, and says so.
                need({"cmd": "semantic_index", "start": True}, "index the fake")
                check(wait_for(lambda: not need({"cmd": "semantic_index"}, "index")["running"], 180, 0.5), "the attempt ends")
                index = need({"cmd": "semantic_index"}, "index")
                check(index["error"] != "" and index["indexed"] == 0,
                      f"a model that cannot load says why ({index['error'][:120]})")
            check(need({"cmd": "ping"}, "ping") is not None, "CyDAW answers")
            print("skip the real CLAP part (no --clap-model / --clap-python)", flush=True)
        else:
            # ---- A real CLAP, installed as from Hugging Face.
            need({"cmd": "ai_models", "install": "laion-clap"}, "install clap")
            check(wait_for(lambda: not need({"cmd": "ai_models"}, "models")["install"]["running"], 300, 1.0),
                  "CLAP downloaded and checked")
            listed = need({"cmd": "ai_models", "use": "laion-clap", "checkEngine": True}, "use clap")
            clap = next(model for model in listed["models"] if model["id"] == "laion-clap")
            check(clap["installed"], f"CLAP is installed ({listed['install']['error']})")
            check(wait_for(lambda: need({"cmd": "semantic_index"}, "index")["engine"]["checked"], 120),
                  "the engine is checked")
            engine = need({"cmd": "semantic_index"}, "index")["engine"]
            check(engine["ready"], f"the engine is ready: torch {engine['torch']}, transformers {engine['transformers']}")
            need({"cmd": "semantic_index", "start": True}, "index")
            check(wait_for(lambda: not need({"cmd": "semantic_index"}, "index")["running"], 300, 1.0), "indexing ends")
            # The count is made again each second.
            wait_for(lambda: need({"cmd": "semantic_index"}, "index")["indexed"] == 6, 15, 0.5)
            index = need({"cmd": "semantic_index"}, "index")
            check(index["error"] == "" and index["indexed"] == 6 and index["total"] == 6,
                  f"every sound indexed ({index['indexed']}/{index['total']}, {index['error'] or index['message']})")
            check((scratch / "Cache" / "samples" / "semantic" / "laion-clap_htsat-unfused.cyvec").is_file(),
                  "the vectors are kept in the cache")
            answer = need({"cmd": "search_samples", "semantic": "kick drum", "limit": 6}, "semantic search", 300)
            names = [item["name"] for item in answer["samples"]]
            print("     kick drum ->", [(item["name"], round(item["score"])) for item in answer["samples"]], flush=True)
            check(names[:1] == ["Deep Kick"] or names[:2] == ["Thing 01", "Deep Kick"],
                  f"\"kick drum\" finds the kick first ({names[:3]})")
            french = need({"cmd": "search_samples", "semantic": "grosse caisse", "limit": 3}, "french", 120)
            check(french["english"] == "kick drum", f"French is put into English ({french['english']})")
            hats = need({"cmd": "search_samples", "semantic": "closed hi-hat", "limit": 6}, "hat", 120)
            print("     closed hi-hat ->", [(item["name"], round(item["score"])) for item in hats["samples"]], flush=True)
            check([item["name"] for item in hats["samples"]].index("Closed Hat") < 3, "\"closed hi-hat\" finds the hat near the top")
            like = need({"cmd": "search_samples", "similar": str(banks / "test-drums" / "Kicks" / "Deep Kick.wav"), "limit": 5},
                        "similar", 120)
            like_names = [item["name"] for item in like["samples"]]
            print("     like Deep Kick ->", [(item["name"], round(item["score"])) for item in like["samples"]], flush=True)
            check("Deep Kick" not in like_names and like_names[:1] == ["Thing 01"],
                  f"the sound most like the kick is the other kick, itself left out ({like_names[:3]})")
            # Categories: CLAP names only what nothing named.
            need({"cmd": "semantic_index", "classify": True}, "classify")
            check(wait_for(lambda: not need({"cmd": "semantic_index"}, "index")["running"], 120, 0.5), "classification ends")
            time.sleep(0.5)
            after = records_by_name(need({"cmd": "search_samples", "limit": 50}, "list")["samples"])
            print("     Thing 01 ->", after["Thing 01"].get("category"), after["Thing 01"].get("classifier"), flush=True)
            check(after["Deep Kick"]["category"] == "kick" and after["Snare Tight"]["category"] == "snare"
                  and after["AltRecorder_Sus_F3_rr1_Main"]["category"] == "woodwinds",
                  "the names' and the bank's categories stay")
            check(after["Closed Hat"]["category"] == "perc", "the person's category stays")
            check(after["Thing 01"]["category"] in ("", "kick", "tom", "perc", "drums")
                  and (after["Thing 01"]["category"] == "" or after["Thing 01"]["classifier"] == "clap"),
                  "the unnamed sound gets a drum category from CLAP, or none when unsure")
            still = need({"cmd": "ping"}, "ping")
            check(still is not None, "CyDAW answers")
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(f"{type(error).__name__}: {error}")
        print(f"FAIL {type(error).__name__}: {error}", flush=True)
    finally:
        if process is not None:
            close(process)
        shutil.rmtree(scratch, ignore_errors=True)
    print("all ok" if not failures else f"{len(failures)} failure(s)", flush=True)
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
