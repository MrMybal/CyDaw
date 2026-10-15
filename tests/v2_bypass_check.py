"""Cards switched off (bypassed), silently (master at -60 dB, speakers off).

    python tests/v2_bypass_check.py --exe "<CyDAW 2.exe>" [--port 47849]

A synth plays a held chord through a three-band equaliser cut by 24 dB:

* the equaliser switched off: the chord comes back up by that much;
* the instrument switched off: the track falls silent; on again, it plays;
* saved and opened again, the equaliser is still off;
* a reverb added after it, then moved before it: the signal goes through the
  reverb first.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

failures: list[str] = []
port = 47849


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


def peak(name: str, seconds: float, settle: float = 0.35) -> float:
    """The loudest the track's meter shows over that time, after a moment to settle (the meter's peak falls back
    slowly: a sound that stops needs a longer one)."""
    time.sleep(settle)
    loudest = -120.0
    end = time.time() + seconds
    while time.time() < end:
        for meter in need({"cmd": "status"}, "status")["meters"]:
            if meter["name"] == name:
                loudest = max(loudest, meter["peakDb"])
        time.sleep(0.04)
    return loudest


def card(track: str, kind: str) -> int:
    for node in need({"cmd": "get_track_graph", "track": track}, "graph")["nodes"]:
        if node["type"] == kind:
            return int(node["id"])
    raise RuntimeError(f"no {kind} card on {track}")


def chain(track: str) -> list[str]:
    """The cards the signal goes through, from the instrument's voice to the output."""
    graph = need({"cmd": "get_track_graph", "track": track}, "graph")
    types = {node["id"]: node["type"] for node in graph["nodes"]}
    audio = [cable for cable in graph["connections"] if cable["kind"] == 0]
    at = next(node for node, kind in types.items() if kind == "voice-filter")
    path = [types[at]]
    for _ in range(32):
        onward = [cable["destination"] for cable in audio if cable["source"] == at]
        if not onward:
            break
        at = onward[0]
        path.append(types[at])
    return path


def scenario(scratch: Path) -> None:
    if not need({"cmd": "status"}, "status").get("speakersSilenced"):
        raise RuntimeError("the session is not silent")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new project")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "set_tempo", "bpm": 120}, "tempo")
    need({"cmd": "add_midi_track", "name": "Synth", "instrument": "ambre-instrument"}, "synth")
    need({"cmd": "add_midi_clip", "track": "Synth", "name": "Chord", "startBeats": 0, "lengthBeats": 16, "looping": True}, "clip")
    need({"cmd": "add_notes", "track": "Synth", "clip": "Chord",
          "notes": [{"note": n, "start": 0, "length": 16, "velocity": 0.8} for n in (48, 55, 60, 64)]}, "notes")
    need({"cmd": "insert_device", "track": "Synth", "key": "eq-three"}, "equaliser")
    equaliser = card("Synth", "eq-three")
    for band in ("low", "mid", "high"):
        need({"cmd": "set_node_parameter", "track": "Synth", "nodeId": equaliser, "parameter": f"eq3.{band}", "value": -24}, band)
    instrument = card("Synth", "ambre-synth")

    need({"cmd": "transport", "action": "play", "position": 0}, "play")
    cut = peak("Synth", 0.8)
    need({"cmd": "set_node", "track": "Synth", "nodeId": equaliser, "bypassed": True}, "equaliser off")
    through = peak("Synth", 0.8)
    check(cut > -80.0 and through > cut + 10.0,
          f"the equaliser switched off: the chord comes back up ({cut:.1f} dB cut, {through:.1f} dB through)")
    described = need({"cmd": "describe_node", "track": "Synth", "nodeId": equaliser}, "describe")
    check(described.get("bypassed") is True, "the card says it is switched off")

    need({"cmd": "set_node", "track": "Synth", "nodeId": instrument, "bypassed": True}, "instrument off")
    silent = peak("Synth", 0.5, settle=2.5)
    check(silent < -90.0, f"the instrument switched off: the track falls silent ({silent:.1f} dB)")
    need({"cmd": "set_node", "track": "Synth", "nodeId": instrument, "bypassed": False}, "instrument on")
    again = peak("Synth", 0.6)
    check(abs(again - through) < 6.0, f"the instrument on again: it plays ({again:.1f} dB)")
    need({"cmd": "transport", "action": "stop"}, "stop")

    project = scratch / "bypass-check.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    for _ in range(100):
        if need({"cmd": "status"}, "status").get("project"):
            break
        time.sleep(0.1)
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    reopened = need({"cmd": "describe_node", "track": "Synth", "nodeId": card("Synth", "eq-three")}, "describe")
    check(reopened.get("bypassed") is True, "saved and opened again, the equaliser is still off")
    need({"cmd": "transport", "action": "play", "position": 0}, "play")
    kept = peak("Synth", 0.8)
    need({"cmd": "transport", "action": "stop"}, "stop")
    check(kept > cut + 10.0, f"and the chord still goes past it ({kept:.1f} dB)")

    # The order of the chain: a reverb after the equaliser, moved before it.
    need({"cmd": "insert_device", "track": "Synth", "key": "halo"}, "reverb")
    reverb = card("Synth", "halo")
    equaliser = card("Synth", "eq-three")
    check(chain("Synth")[-3:-1] == ["eq-three", "halo"], f"the reverb goes after the equaliser ({chain('Synth')})")
    need({"cmd": "move_device", "track": "Synth", "nodeId": reverb, "before": equaliser}, "move")
    order = chain("Synth")
    check(order.index("halo") < order.index("eq-three"), f"moved before it, the signal goes through the reverb first ({order})")
    refused = request({"cmd": "move_device", "track": "Synth", "nodeId": card("Synth", "ambre-synth"), "before": 0})
    check(not refused.get("ok"), "the instrument stays first: it cannot be moved")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-bypass-"))
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1")
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
        scenario(scratch)
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL", error, flush=True)
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
