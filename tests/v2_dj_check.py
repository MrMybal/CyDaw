"""The Mix DJ mode in the software. Silent (the master muted at -60 dB; the meters read are the DJ mix's own).

    python tests/v2_dj_check.py --exe "<CyDAW 2.exe>" [--port 47854]

Two dance tracks are made for it (128 BPM in A minor, 122 BPM in D minor), in a folder of their own; the
collection, the locations and the mixes are the test's own (CYDAW_DJ_COLLECTION_FILE, CYDAW_LOCATIONS_FILE).

* The collection reads the folder and analyses both tracks: tempo, key.
* On the decks: decoded, analysed (tempo within 0.05 BPM, the grid on the kicks within 15 ms, the key).
* A plays; B synced follows its tempo, beats aligned; a tempo change on A carries B along.
* The EQ's kills take the deck out; the fader and the crossfader too.
* A loop holds the deck; a hot cue brings it back; the cue button stops on the cue point.
* An effect keeps the mix bounded; the mix is recorded to a file that holds sound, under the limiter's ceiling.
* Quitting keeps the collection (tempo, cues) in its file.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import random
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47854


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


def dj(what: str, **fields) -> dict:
    return need(dict({"cmd": "dj"}, **fields), what)


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def make_track(path: Path, bpm: float, root_hz: float, offset: float, seconds: float = 30.0, rate: int = 44_100, seed: int = 1) -> None:
    """A kick on every beat from `offset`, a hat between, a bass and a minor chord held."""
    rng = random.Random(seed)
    frames = int(seconds * rate)
    left = [0.0] * frames
    right = [0.0] * frames
    beat = 60.0 / bpm
    at = offset
    while at < seconds:
        start = int(at * rate)
        for i in range(int(0.18 * rate)):
            if start + i >= frames:
                break
            s = i / rate
            value = 0.75 * math.exp(-s * 18.0) * math.sin(2 * math.pi * (50.0 * s + 90.0 / 40.0 * (1 - math.exp(-s * 40.0))))
            left[start + i] += value
            right[start + i] += value
        hat = int((at + beat * 0.5) * rate)
        for i in range(int(0.04 * rate)):
            if hat + i >= frames:
                break
            value = (rng.random() * 2 - 1) * 0.18 * math.exp(-i / rate * 90.0)
            left[hat + i] += value
            right[hat + i] += value
        at += beat
    for i in range(frames):
        s = i / rate
        bass = 0.12 * math.sin(2 * math.pi * root_hz * s)
        chord = sum(0.035 * math.sin(2 * math.pi * root_hz * ratio * s) for ratio in (2.0, 2.4, 3.0, 4.0))
        left[i] += bass + chord
        right[i] += bass + chord
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(b"".join(struct.pack("<hh", int(max(-1.0, min(1.0, l)) * 32000), int(max(-1.0, min(1.0, r)) * 32000))
                                 for l, r in zip(left, right)))


def deck(state: dict, letter: str) -> dict:
    return next(item for item in state["decks"] if item["deck"] == letter)


def wait_for(what: str, predicate, seconds: float = 30.0) -> dict:
    deadline = time.time() + seconds
    state = dj("status")
    while time.time() < deadline:
        state = dj("status")
        if predicate(state):
            return state
        time.sleep(0.2)
    raise RuntimeError("timed out: " + what)


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-dj-check-"))
    music = scratch / "Music"
    music.mkdir()
    first = music / "Test Artist - Deep 128.wav"
    second = music / "Other Artist - Rolling 122.wav"
    make_track(first, 128.0, 55.0, 0.25, seed=1)
    make_track(second, 122.0, 73.42, 0.4, seed=2)
    collection = scratch / "DjCollection.json"
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "mixes": str(scratch / "Mixes"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations), CYDAW_DJ_COLLECTION_FILE=str(collection))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent", "--window-size=1500x900"],
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
        need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
        check(need({"cmd": "app_mode", "mode": "dj"}, "mode").get("mode") == "dj", "the window is in the DJ mode")

        # ---- The collection ----
        dj("folder", action="folders", add=str(music))
        state = wait_for("the folder read", lambda s: s["trackCount"] >= 2 and not s["scanning"])
        check(state["trackCount"] == 2, f"the collection holds the folder's two tracks ({state['trackCount']})")
        dj("analyse", action="analyse")
        state = wait_for("the analysis", lambda s: s["analysing"] == 0 and all(t["analysed"] for t in dj("tracks", action="tracks")["tracks"]), 90.0)
        tracks = {Path(t["file"]).name: t for t in dj("tracks", action="tracks")["tracks"]}
        check(abs(tracks[first.name]["bpm"] - 128.0) < 0.05 and tracks[first.name]["camelot"] == "8A",
              f"the collection's analysis: 128 BPM, 8A ({tracks[first.name]['bpm']}, {tracks[first.name]['camelot']})")
        check(tracks[first.name]["title"] == "Deep 128" and tracks[first.name]["artist"] == "Test Artist", "a track's title and artist")

        # ---- On the decks ----
        dj("load A", action="load", deck="A", file=str(first))
        dj("load B", action="load", deck="B", file=str(second))
        state = wait_for("the decks ready", lambda s: deck(s, "A")["analysed"] and deck(s, "B")["analysed"])
        a, b = deck(state, "A"), deck(state, "B")
        check(abs(a["bpm"] - 128.0) < 0.05 and abs(b["bpm"] - 122.0) < 0.05, f"the decks' tempos ({a['bpm']}, {b['bpm']})")
        for item, offset, bpm in ((a, 0.25, 128.0), (b, 0.4, 122.0)):
            beat = 60.0 / bpm
            off = (item["firstBeat"] - offset + beat / 2) % beat - beat / 2
            check(abs(off) < 0.015, f"deck {item['deck']}'s grid on its kicks ({off * 1000:.1f} ms)")
        check(a.get("key") == "Am" and b.get("key") == "Dm", f"the decks' keys ({a.get('key')}, {b.get('key')})")
        check(abs(a["seconds"] - 30.0) < 0.01, "a deck's length")

        # ---- Playing, syncing ----
        dj("play A", action="play", deck="A")
        time.sleep(1.2)
        state = dj("status")
        a = deck(state, "A")
        check(a["playing"] and 0.8 < a["position"] < 2.0, f"deck A plays ({a['position']:.2f} s)")
        check(a["peakDb"] > -30.0 and state["masterPeakDb"] > -30.0, f"deck A sounds in the mix ({a['peakDb']:.1f} dB)")
        check(a["master"], "the deck playing leads")
        dj("sync B", action="sync", deck="B", on=True)
        dj("play B", action="play", deck="B")
        time.sleep(2.5)
        b = deck(dj("status"), "B")
        check(abs(b["speed"] - 128.0 / 122.0) < 0.003, f"deck B synced plays at A's tempo (x{b['speed']:.4f})")
        check(abs(b["phase"]) < 0.02, f"deck B's beats on A's ({b['phase']:.4f} of a beat)")
        dj("tempo A", action="tempo", deck="A", percent=2.0)
        time.sleep(1.5)
        state = dj("status")
        a, b = deck(state, "A"), deck(state, "B")
        check(abs(a["playedBpm"] - 130.56) < 0.05, f"deck A at +2 % ({a['playedBpm']:.2f} BPM)")
        check(abs(b["playedBpm"] - a["playedBpm"]) < 0.08, f"deck B follows ({b['playedBpm']:.2f} BPM)")
        dj("tempo A", action="tempo", deck="A", ratio=1.0)
        dj("keylock A off", action="keylock", deck="A", on=False)
        before = deck(dj("status"), "A")["position"]
        time.sleep(0.6)
        check(deck(dj("status"), "A")["position"] > before + 0.4, "deck A plays on without its key locked")
        dj("keylock A on", action="keylock", deck="A", on=True)

        # ---- The mixer ----
        dj("pause B", action="play", deck="B", on=False)
        dj("kills", action="channel", deck="A", killLow=True, killMid=True, killHigh=True)
        time.sleep(2.2)
        check(deck(dj("status"), "A")["peakDb"] < -40.0, f"the three kills take deck A out ({deck(dj('status'), 'A')['peakDb']:.1f} dB)")
        dj("kills off", action="channel", deck="A", killLow=False, killMid=False, killHigh=False)
        time.sleep(0.6)
        check(deck(dj("status"), "A")["peakDb"] > -30.0, "deck A back")
        dj("crossfader right", action="mixer", crossfader=1.0)
        time.sleep(2.2)
        check(deck(dj("status"), "A")["peakDb"] < -40.0, "the crossfader at the right takes the left deck out")
        dj("crossfader middle", action="mixer", crossfader=0.5)
        dj("fader down", action="channel", deck="A", fader=0.0)
        time.sleep(2.2)
        check(deck(dj("status"), "A")["peakDb"] < -40.0, "the fader down takes the deck out")
        dj("fader up", action="channel", deck="A", fader=1.0)

        # ---- Loops, cues ----
        dj("loop", action="loop", deck="A", beats=4)
        time.sleep(3.0)
        a = deck(dj("status"), "A")
        check(a["looping"] and a["loopStart"] - 0.01 <= a["position"] <= a["loopEnd"] + 0.05,
              f"a loop of 4 beats holds the deck ({a['loopStart']:.2f} <= {a['position']:.2f} <= {a['loopEnd']:.2f})")
        check(abs((a["loopEnd"] - a["loopStart"]) - 4 * 60.0 / 128.0) < 0.002, "the loop is 4 beats long")
        dj("loop off", action="loop", deck="A", beats=0)
        dj("hot cue", action="hotcue", deck="A", index=0, **{"do": "set"})
        time.sleep(0.2)  # the audio thread sets it at its next block
        cue = deck(dj("status"), "A")["hotCues"][0]
        check(cue >= 0.0, f"hot cue 1 set ({cue:.2f} s)")
        dj("jump", action="beatjump", deck="A", beats=16)
        time.sleep(0.3)
        dj("hot cue jump", action="hotcue", deck="A", index=0)
        time.sleep(0.2)
        a = deck(dj("status"), "A")
        check(abs(a["position"] - cue) < 0.5, f"hot cue 1 brings the deck back ({a['position']:.2f} s, cue {cue:.2f} s)")
        dj("pause A", action="play", deck="A", on=False)
        dj("seek", action="seek", deck="A", seconds=8.0)
        time.sleep(0.2)
        dj("cue down", action="cue", deck="A", down=True)
        time.sleep(0.6)
        dj("cue up", action="cue", deck="A", down=False)
        time.sleep(0.2)
        a = deck(dj("status"), "A")
        check(not a["playing"] and abs(a["position"] - a["cuePoint"]) < 0.01 and abs(a["cuePoint"] - 8.0) < 0.3,
              f"the cue button: held it plays, let go it is back on the cue point ({a['position']:.2f}, cue {a['cuePoint']:.2f})")

        # ---- An effect, the mix recorded ----
        dj("echo", action="channel", deck="A", effect={"kind": "echo", "on": True, "mix": 0.6, "first": 0.5, "second": 0.6})
        dj("play A", action="play", deck="A")
        recorded = scratch / "Mixes" / "check mix.wav"
        dj("record", action="record", on=True, path=str(recorded))
        time.sleep(2.5)
        state = dj("status")
        check(state["recording"] and state["recordedSeconds"] > 1.5, f"the mix records ({state.get('recordedSeconds', 0):.1f} s)")
        check(-40.0 < state["masterPeakDb"] < 12.0, f"an echo on deck A keeps the mix bounded ({state['masterPeakDb']:.1f} dB)")
        dj("record off", action="record", on=False)
        dj("stop", action="play", deck="A", on=False)
        time.sleep(0.5)
        with wave.open(str(recorded), "rb") as file:
            frames = file.getnframes()
            rate = file.getframerate()
            raw = file.readframes(frames)
        width = 3
        loudest = max(abs(int.from_bytes(raw[i:i + 3] + (b"\xff" if raw[i + 2] & 0x80 else b"\x00"), "little", signed=True))
                      for i in range(0, len(raw) - 3, width * 2 * 50)) / 8_388_608.0
        check(frames / rate > 1.5 and loudest > 0.05, f"the recorded mix holds sound ({frames / rate:.1f} s, peak {loudest:.2f})")
        check(loudest <= 0.97, f"the master's limiter keeps the mix under -0.3 dBFS (peak {loudest:.3f})")

        # ---- Kept ----
        need({"cmd": "quit"}, "quit")
        process.wait(timeout=60)
        saved = json.loads(collection.read_text(encoding="utf-8"))
        kept = {Path(t["path"]).name: t for t in saved["tracks"]}
        check(abs(kept[first.name]["bpm"] - 128.0) < 0.05 and kept[first.name]["hotCues"][0] >= 0.0 and kept[first.name]["plays"] >= 1,
              "the collection keeps the tempo, the hot cue and the plays")
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
