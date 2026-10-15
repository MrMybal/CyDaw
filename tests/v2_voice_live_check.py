"""What a synth's voice plays, shown as it plays. Silent (master at -60 dB, speakers off).

    python tests/v2_voice_live_check.py --exe "<CyDAW 2.exe>" [--port 47851]

An Ambre holds a note while its LFO 2 sweeps the filter:

* the track reports its newest voice playing, its cutoff where the LFO puts
  it (moving from one look to the next), its LFO moving, its envelope up;
* stopped and silent, it reports no voice.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import socket
import subprocess
import time
from pathlib import Path

failures: list[str] = []
port = 47851


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


def scenario() -> None:
    if not need({"cmd": "status"}, "status").get("speakersSilenced"):
        raise RuntimeError("the session is not silent")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "new_project"}, "new project")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    need({"cmd": "set_tempo", "bpm": 120}, "tempo")
    need({"cmd": "add_midi_track", "name": "Synth", "instrument": "ambre-instrument"}, "synth")
    keys = [p["key"] for p in need({"cmd": "list_parameters", "track": "Synth"}, "parameters")["parameters"]]
    for suffix, value in (("lfo2.cutoff", 0.6), ("lfo2.rate", 1.5)):
        need({"cmd": "set_parameter", "track": "Synth", "key": next(k for k in keys if k.endswith(suffix)), "value": value}, suffix)
    need({"cmd": "add_midi_clip", "track": "Synth", "name": "Note", "startBeats": 0, "lengthBeats": 8, "looping": False}, "clip")
    need({"cmd": "add_notes", "track": "Synth", "clip": "Note", "notes": [{"note": 48, "start": 0, "length": 8, "velocity": 0.8}]},
         "note")

    quiet = need({"cmd": "voice_live", "track": "Synth"}, "live")
    check(quiet.get("playing") is False, "nothing plays: no voice to show")
    need({"cmd": "transport", "action": "play", "position": 0}, "play")
    time.sleep(0.4)
    looks = []
    for _ in range(8):
        looks.append(need({"cmd": "voice_live", "track": "Synth"}, "live"))
        time.sleep(0.15)
    need({"cmd": "transport", "action": "stop"}, "stop")
    playing = [look for look in looks if look.get("playing")]
    check(len(playing) == len(looks), f"while the note plays, its voice is shown ({len(playing)} of {len(looks)} looks)")
    cutoffs = [look.get("modulated", {}).get("filter.cutoff") for look in playing]
    cutoffs = [value for value in cutoffs if isinstance(value, (int, float))]
    check(len(cutoffs) == len(playing) and len(cutoffs) > 0, f"the filter's cutoff is shown where the LFO puts it ({len(cutoffs)} looks)")
    if cutoffs:
        check(max(cutoffs) > min(cutoffs) * 1.2, f"it moves from one look to the next ({min(cutoffs):.0f} to {max(cutoffs):.0f} Hz)")
    lfos = [look["lfos"][1] for look in playing if isinstance(look.get("lfos", [None, None])[1], (int, float))]
    check(len(lfos) > 1 and max(lfos) - min(lfos) > 0.3, "its LFO 2 moves")
    envelopes = [look["envelopes"][0] for look in playing if isinstance(look.get("envelopes", [None])[0], (int, float))]
    check(len(envelopes) > 0 and min(envelopes) > 0.1, "its envelope is up while the key is down")

    end = time.time() + 5.0
    silent = False
    while time.time() < end and not silent:
        silent = need({"cmd": "voice_live", "track": "Synth"}, "live").get("playing") is False
        time.sleep(0.2)
    check(silent, "stopped and faded out: no voice is shown")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
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
        scenario()
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=20)
        except Exception:  # noqa: BLE001
            process.kill()
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
