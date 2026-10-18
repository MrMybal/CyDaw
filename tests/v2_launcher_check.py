"""The launcher and arming, silently (master muted).

    python tests/v2_launcher_check.py --exe "<CyDAW 2.exe>" [--port 47827]

* Picking a track arms nothing; several tracks are armed by hand.
* A track picked while another plays a clip leaves that clip's held notes
  sounding (they were cut: the keyboard's all-notes-off reached them).
* A scene launched: each track plays its clip there; a track whose slot is
  empty stops, unless that slot's stop was taken off; the stop is saved with
  the project.
* The transport stopped while clips play: they wait, and play starts them
  again from their start; stop all forgets them.
* Hybrid tracks: an instrument track takes an audio clip and an audio track a
  MIDI clip (a companion track the lists never show holds them, routed into
  the track); one clip plays at a time on a track; saved and opened; gone
  with its track.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import math
import struct
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47827


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


def silent() -> None:
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master muted")


def midi_tracks() -> dict:
    return {track["name"]: track for track in need({"cmd": "status"}, "status").get("midiTracks", [])}


def peak(name: str, seconds: float) -> float:
    loudest = -120.0
    end = time.time() + seconds
    while time.time() < end:
        for meter in need({"cmd": "status"}, "status")["meters"]:
            if meter["name"] == name:
                loudest = max(loudest, meter["peakDb"])
        time.sleep(0.04)
    return loudest


def scenes(result: dict) -> dict:
    return {entry["track"]: entry["scene"] for entry in result.get("tracks", [])}


def scenario(scratch: Path) -> None:
    silent()
    need({"cmd": "new_project"}, "new")
    silent()
    need({"cmd": "set_tempo", "bpm": 120}, "tempo")
    for name in ("A", "B"):
        need({"cmd": "add_midi_track", "name": name}, "track " + name)
        # An instrument that holds its notes (a sustained synth).
        need({"cmd": "insert_device", "track": name, "key": "synth-keys"}, "instrument " + name)
    # A: a chord held the whole bar in scene 1, a note in scene 2; B: scene 1 only.
    need({"cmd": "add_midi_clip", "track": "A", "name": "A1", "scene": 0, "lengthBeats": 4, "looping": True}, "clip")
    need({"cmd": "add_notes", "track": "A", "clip": "A1",
          "notes": [{"note": n, "start": 0, "length": 4, "velocity": 0.8} for n in (57, 60, 64)]}, "notes")
    need({"cmd": "add_midi_clip", "track": "A", "name": "A2", "scene": 1, "lengthBeats": 4, "looping": True}, "clip")
    need({"cmd": "add_notes", "track": "A", "clip": "A2", "notes": [{"note": 62, "start": 0, "length": 4}]}, "notes")
    need({"cmd": "add_midi_clip", "track": "B", "name": "B1", "scene": 0, "lengthBeats": 4, "looping": True}, "clip")
    need({"cmd": "add_notes", "track": "B", "clip": "B1", "notes": [{"note": 48, "start": 0, "length": 4}]}, "notes")

    # Picking a track arms nothing; arms are the person's, several at once.
    for name in ("A", "B"):
        need({"cmd": "arm", "track": name, "armed": False}, "disarm")
    need({"cmd": "get_track_graph", "track": "A"}, "pick A")
    time.sleep(0.3)
    need({"cmd": "get_track_graph", "track": "B"}, "pick B")
    time.sleep(0.3)
    tracks = midi_tracks()
    check(not tracks["A"]["armed"] and not tracks["B"]["armed"],
          f"picking tracks arms none of them (A {tracks['A']['armed']}, B {tracks['B']['armed']})")
    need({"cmd": "arm", "track": "A", "armed": True}, "arm A")
    need({"cmd": "arm", "track": "B", "armed": True}, "arm B")
    need({"cmd": "get_track_graph", "track": "A"}, "pick A")
    time.sleep(0.3)
    tracks = midi_tracks()
    check(tracks["A"]["armed"] and tracks["B"]["armed"], "two tracks stay armed together, whichever is picked")
    for name in ("A", "B"):
        need({"cmd": "arm", "track": name, "armed": False}, "disarm")

    # A's chord held while B is picked: it goes on sounding.
    silent()
    launched = need({"cmd": "launch_scene", "scene": 0}, "scene 1")
    check(scenes(launched).get("A") == 0 and scenes(launched).get("B") == 0, f"scene 1 plays both clips ({scenes(launched)})")
    time.sleep(0.4)
    before = peak("A", 0.3)
    need({"cmd": "get_track_graph", "track": "B"}, "pick B")
    need({"cmd": "get_track_graph", "track": "A"}, "pick A")
    need({"cmd": "get_track_graph", "track": "B"}, "pick B")
    time.sleep(0.4)
    after = peak("A", 0.5)
    check(before > -40.0 and after > before - 6.0,
          f"A's held chord goes on when another track is picked ({before:.1f} dB before, {after:.1f} dB after)")

    # Scene 2: A plays its clip there, B's slot is empty: B stops.
    launched = need({"cmd": "launch_scene", "scene": 1}, "scene 2")
    check(scenes(launched).get("A") == 1 and scenes(launched).get("B") == -1,
          f"an empty slot stops its track when its scene launches ({scenes(launched)})")
    # Its stop taken off: B goes on through scene 2.
    stop = need({"cmd": "slot_stop", "track": "B", "scene": 1, "stops": False}, "stop off")
    check(stop.get("stops") is False and stop.get("hasClip") is False, "an empty slot's stop is taken off")
    need({"cmd": "launch_scene", "scene": 0}, "scene 1 again")
    launched = need({"cmd": "launch_scene", "scene": 1}, "scene 2 again")
    check(scenes(launched).get("A") == 1 and scenes(launched).get("B") == 0,
          f"without its stop, the track goes on through that scene ({scenes(launched)})")
    need({"cmd": "stop_launcher"}, "stop all")
    need({"cmd": "transport", "action": "stop"}, "transport stop")

    # The transport stopped while clips play, then played: each starts
    # again from its start (it does not go on from where it was).
    need({"cmd": "launch_scene", "scene": 0}, "scene 1 from stopped")
    time.sleep(0.9)
    need({"cmd": "transport", "action": "stop"}, "transport stop")
    waiting = need({"cmd": "launch_scene", "query": True}, "read")
    pending = {entry["track"]: entry["pending"] for entry in waiting.get("tracks", [])}
    check(not waiting.get("playing") and pending.get("A") == 0 and pending.get("B") == 0,
          f"stopped, the clips that played wait for play ({pending})")
    need({"cmd": "transport", "action": "play"}, "transport play")
    time.sleep(0.3)
    again = need({"cmd": "launch_scene", "query": True}, "read")
    positions = {entry["track"]: entry["position"] for entry in again.get("tracks", [])}
    check(scenes(again).get("A") == 0 and scenes(again).get("B") == 0
          and all(0.0 <= positions.get(name, -1.0) < 0.75 for name in ("A", "B")),
          f"played again, they start again from their start ({scenes(again)}, {positions})")
    need({"cmd": "transport", "action": "stop"}, "transport stop")
    need({"cmd": "stop_launcher"}, "stop all")
    forgotten = need({"cmd": "launch_scene", "query": True}, "read")
    check(all(entry["pending"] < 0 for entry in forgotten.get("tracks", [])),
          "stop all: nothing waits for play any more")

    # Saved and opened: the stop stays off, the others on.
    project = scratch / "launcher-check.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    silent()
    off = need({"cmd": "slot_stop", "track": "B", "scene": 1}, "read")
    on = need({"cmd": "slot_stop", "track": "B", "scene": 2}, "read")
    check(off.get("stops") is False and on.get("stops") is True, "the slots' stops are saved with the project")


def tone(path: Path, seconds: float = 4.0) -> None:
    """A 440 Hz sine at -12 dBFS, stereo 48 kHz."""
    rate = 48_000
    with wave.open(str(path), "wb") as file:
        file.setnchannels(2)
        file.setsampwidth(2)
        file.setframerate(rate)
        frames = bytearray()
        for index in range(int(rate * seconds)):
            value = int(0.25 * 32767 * math.sin(2.0 * math.pi * 440.0 * index / rate))
            frames += struct.pack("<hh", value, value)
        file.writeframes(bytes(frames))


def order() -> list:
    return need({"cmd": "status"}, "status").get("order", [])


def hybrid_scenario(scratch: Path) -> None:
    need({"cmd": "new_project"}, "new")
    silent()
    need({"cmd": "set_tempo", "bpm": 120}, "tempo")
    sound = scratch / "tone.wav"
    tone(sound)
    file = str(sound).replace(os.sep, "/")
    need({"cmd": "add_midi_track", "name": "H"}, "track H")
    need({"cmd": "insert_device", "track": "H", "key": "synth-keys"}, "instrument H")
    # An audio clip in an instrument track's slot: the track becomes hybrid,
    # its companion is not listed.
    need({"cmd": "add_audio_clip", "track": "H", "file": file, "scene": 1}, "audio clip on H")
    need({"cmd": "add_midi_clip", "track": "H", "name": "H0", "scene": 0, "lengthBeats": 4, "looping": True}, "clip H0")
    shown = order()
    check([entry.get("hybrid") for entry in shown if entry["name"] == "H"] == ["audio"]
          and sum(1 for track in need({"cmd": "status"}, "status").get("audioTracks", []) if track.get("companionOf") == "H") == 1,
          f"an instrument track with an audio clip is one hybrid track, its companion not shown ({shown})")
    check(need({"cmd": "slot_stop", "track": "H", "scene": 1}, "slot").get("hasClip") is True,
          "its slot holds the audio clip")
    # The audio clip plays through the track.
    launched = need({"cmd": "launch_clip", "track": "H", "scene": 1}, "launch audio")
    check(launched.get("launched") is True, "the audio clip of the instrument track launches")
    level = peak("H", 3.0)
    check(level > -30.0, f"the audio clip sounds through the instrument track ({level:.1f} dB)")
    # Its MIDI clip launched: the audio clip stops (one clip at a time).
    need({"cmd": "launch_clip", "track": "H", "scene": 0}, "launch MIDI")
    time.sleep(2.6)
    playing = scenes(need({"cmd": "launch_scene", "query": True}, "read"))
    quiet = peak("H", 0.6)
    check(playing.get("H") == 0 and quiet < -50.0,
          f"launching its MIDI clip stops its audio clip (scene {playing.get('H')}, {quiet:.1f} dB)")
    need({"cmd": "stop_launcher"}, "stop all")
    need({"cmd": "transport", "action": "stop"}, "stop")
    # An instrument track without an instrument: its audio clips still sound.
    need({"cmd": "add_midi_track", "name": "Bare"}, "track Bare")
    need({"cmd": "add_audio_clip", "track": "Bare", "file": file, "scene": 0}, "audio clip on Bare")
    need({"cmd": "launch_clip", "track": "Bare", "scene": 0}, "launch Bare")
    bare = peak("Bare", 2.5)
    check(bare > -30.0, f"an instrument track without an instrument lets its audio clips through ({bare:.1f} dB)")
    need({"cmd": "stop_launcher"}, "stop all")
    need({"cmd": "transport", "action": "stop"}, "stop")
    # « + audio »: an empty audio clip, a bar long.
    need({"cmd": "add_audio_clip", "track": "Bare", "scene": 2}, "empty audio clip")
    check(need({"cmd": "slot_stop", "track": "Bare", "scene": 2}, "slot").get("hasClip") is True,
          "an empty audio clip is made in an instrument track's slot")
    # A MIDI clip on an audio track: an instrument plays it there.
    need({"cmd": "add_audio_track", "file": file}, "audio track")
    need({"cmd": "add_midi_clip", "track": "tone", "name": "W2", "scene": 2, "lengthBeats": 4, "looping": True}, "clip W2")
    companions = [track for track in midi_tracks().values() if track.get("companionOf") == "tone"]
    check(len(companions) == 1 and companions[0].get("devices"),
          "a MIDI clip on an audio track gets it a companion instrument")
    notes = request({"cmd": "list_notes", "track": "tone@midi", "clip": "W2"})
    check(bool(notes.get("ok")), f"\"<name>@midi\" names the audio track's MIDI clips ({notes.get('error', '')})")
    # Saved and opened: the same hybrid tracks.
    before = order()
    project = scratch / "hybrid-check.cydaw"
    need({"cmd": "save_project", "path": str(project).replace(os.sep, "/")}, "save")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "open_project", "path": str(project).replace(os.sep, "/")}, "open")
    silent()
    after = order()
    check(after == before, f"hybrid tracks are saved with the project ({after})")
    check(need({"cmd": "slot_stop", "track": "H", "scene": 1}, "slot").get("hasClip") is True,
          "the instrument track's audio clip is still in its slot")
    # A hybrid track removed: its companion goes with it.
    need({"cmd": "remove_track", "track": "H"}, "remove H")
    status = need({"cmd": "status"}, "status")
    left = [track for track in status.get("audioTracks", []) if track.get("companionOf") == "H"]
    names = [entry["name"] for entry in status.get("order", [])]
    check(not left and "H" not in names and "tone" in names, f"a hybrid track removed takes its companion with it ({names})")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=47827)
    arguments = parser.parse_args()
    port = arguments.port
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1")
    process = subprocess.Popen([str(Path(arguments.exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1500x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-launcher-"))
    try:
        for _ in range(200):
            try:
                request({"cmd": "ping"})
                break
            except OSError:
                time.sleep(0.2)
        scenario(scratch)
        hybrid_scenario(scratch)
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        try:
            request({"cmd": "quit"})
        except OSError:
            pass
        try:
            process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print("failures:", len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
