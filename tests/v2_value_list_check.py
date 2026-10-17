"""A card's list of values in the software. Silent (the master muted at -60 dB).

    python tests/v2_value_list_check.py --exe "<CyDAW 2.exe>" [--port 47857]

* Opened on a synth's card, it holds every value the card has, none touched.
* A value set as a knob sets it goes to the top; the next one touched goes
  above it.
* Its search keeps the values whose name or key holds the words.
* A snapshot of the strip with the list open (for the eye).

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

failures: list[str] = []
port = 47857


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


def scenario(scratch: Path) -> None:
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "add_midi_track", "name": "Synth", "instrument": "opale-instrument"}, "synth track")
    graph = need({"cmd": "get_track_graph", "track": "Synth"}, "graph")
    card = next(node for node in graph["nodes"] if "opale" in node["type"])
    need({"cmd": "set_view", "mode": 3}, "devices")
    listed = need({"cmd": "value_list", "track": "Synth", "nodeId": card["id"]}, "open")["values"]
    check(len(listed) == len(card["parameters"]) and len(listed) > 20 and not any(v["touched"] for v in listed),
          f"the list holds every value of the card, none touched yet ({len(listed)} of {len(card['parameters'])})")
    bindings = [v["binding"] for v in listed]
    key = next(t["target"] for t in need({"cmd": "controllers", "action": "targets", "filter": "/volume"}, "targets")["targets"]
               if t["label"].startswith("Synth")).split("/")[1]
    first, second = bindings[10], bindings[3]
    need({"cmd": "controllers", "action": "apply", "target": f"param/{key}/{first}", "value": 0.7}, "touch one")
    listed = need({"cmd": "value_list", "track": "Synth", "nodeId": card["id"]}, "list")["values"]
    check(listed[0]["binding"] == first and listed[0]["touched"] and not listed[1]["touched"],
          f"a value set as a knob sets it goes to the top ({listed[0]['binding']})")
    need({"cmd": "controllers", "action": "apply", "target": f"param/{key}/{second}", "value": 0.2}, "touch another")
    listed = need({"cmd": "value_list", "track": "Synth", "nodeId": card["id"]}, "list")["values"]
    check([v["binding"] for v in listed[:2]] == [second, first] and len(listed) == len(bindings),
          "the next one touched goes above it; the list keeps every value")
    searched = need({"cmd": "value_list", "track": "Synth", "nodeId": card["id"], "filter": "cutoff"}, "search")["values"]
    check(0 < len(searched) < len(bindings)
          and all("cutoff" in (v["label"] + v["binding"]).lower() for v in searched),
          f"its search keeps the values that hold the words ({[v['label'] for v in searched]})")
    shot = scratch / "value-list.png"
    need({"cmd": "snapshot", "path": str(shot).replace(os.sep, "/")}, "snapshot")
    check(shot.is_file() and shot.stat().st_size > 10_000, f"the strip with its list is drawn ({shot})")
    need({"cmd": "value_list", "track": "Synth", "nodeId": card["id"], "open": False}, "close")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    # Parameter names hold arrows and accents; a Windows console's code page does not.
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-value-list-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {"cache": str(scratch / "Cache")}}),
                         encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent", "--window-size=1800x1100"],
                               env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(300):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        if not need({"cmd": "status"}, "status").get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        scenario(scratch)
    except Exception as error:  # noqa: BLE001
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        if process.poll() is None:
            try:
                request({"cmd": "quit"})
                process.wait(timeout=30)
            except Exception:  # noqa: BLE001
                process.kill()
    print(f"{len(failures)} failure(s)" if failures else "all checks hold", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
