"""Run existing protocol checks against isolated native desktop sessions."""
import argparse
import json
import os
from pathlib import Path
import socket
import shutil
import subprocess
import tempfile
import time

p = argparse.ArgumentParser()
p.add_argument("--exe", type=Path, required=True)
p.add_argument("--port", type=int, required=True)
p.add_argument("--peer-port", type=int)
p.add_argument("script", type=Path)
p.add_argument("arguments", nargs=argparse.REMAINDER)
a = p.parse_args()

def request(port, command):
    with socket.create_connection(("127.0.0.1", port), timeout=2) as s:
        s.settimeout(5)
        s.sendall((json.dumps({"cmd": command}) + "\n").encode())
        return json.loads(s.makefile("rb").readline())

with tempfile.TemporaryDirectory(prefix="cydaw-native-session-") as folder:
    sessions = []
    try:
        for port in [a.port] + ([a.peer_port] if a.peer_port else []):
            profile = Path(folder) / str(port)
            profile.mkdir()
            # Paired benchmarks restore the same audio setup in nested profiles.
            # These are workspace QA preferences, never the user's real files.
            if bench_profile := os.environ.get("CYDAW_BENCH_PROFILE"):
                destination = profile/"Roaming"/"CyDAW"
                destination.mkdir(parents=True, exist_ok=True)
                for suffix in ("layout", "processing"):
                    source = Path(bench_profile)/"CyDAW"/f"CyDAW.{suffix}.settings"
                    if source.is_file():
                        shutil.copy2(source, destination/source.name)
            locations = profile / "Locations.json"
            locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
                k: str(profile/k) for k in ("cache", "presets", "banks", "mixes", "foundry", "devices", "pluginExports")}}), encoding="utf-8")
            env = dict(os.environ, APPDATA=str(profile/"Roaming"), LOCALAPPDATA=str(profile/"Local"),
                CYDAW_NO_RECOVERY="1", CYDAW_CONTROL_TOKEN="", CYDAW_LOCATIONS_FILE=str(locations),
                CYDAW_BENCH_PROFILE=str(profile/"Roaming") if os.environ.get("CYDAW_BENCH_PROFILE") else "",
                CYDAW_DJ_COLLECTION_FILE=str(profile/"dj.json"), CYDAW_DJ_PROCESSING_FILE=str(profile/"dj-processing.json"))
            log = open(profile/"application.log", "w", encoding="utf-8")
            app = subprocess.Popen([str(a.exe.resolve()), "--silent", f"--control-port={port}", "--window-size=1800x1100"], env=env, stdout=log, stderr=subprocess.STDOUT)
            sessions.append((port, app, log))
            end = time.monotonic()+60
            while True:
                if app.poll() is not None:
                    raise RuntimeError(f"native application exited {app.returncode}")
                try:
                    if request(port, "ping").get("ok"):
                        break
                except (OSError, ValueError):
                    pass
                if time.monotonic() >= end:
                    raise TimeoutError("native control port not ready")
                time.sleep(.1)
        result = subprocess.run([os.sys.executable, str(a.script), *a.arguments], timeout=300).returncode
    finally:
        for port, app, log in sessions:
            try:
                request(port, "quit")
                app.wait(timeout=15)
            except (OSError, ValueError, subprocess.TimeoutExpired):
                app.kill()
                app.wait(timeout=10)
            log.close()
    raise SystemExit(result)
