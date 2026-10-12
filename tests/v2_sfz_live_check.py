"""An SFZ instrument played as it moves, in the software. Silent, offline.

    python tests/v2_sfz_live_check.py --exe "<CyDAW 2.exe>" [--port 47847]

A small SFZ is written beside sine notes (two microphones of a note, its release, a second
articulation), then read by CyDAW:

* its controls (named controllers) and articulations (keyswitches) are on the card, the zones its
  settings do not reach yet are not decoded until they do;
* a controller set on the card levels its zones; one at zero takes its microphone out; MIDI moves
  it while a note plays;
* the articulation is chosen on the card, or by its key while playing;
* a key let go plays the release zone, lower the longer it was held;
* a nine-second recording keeps only its head in memory and plays through, read from the disk;
* a project saved and opened keeps the card's controls;
* with the banks there, Batterie's kit raises an overhead microphone that was not playing.

Every note is heard through the exported plug-in's path, in memory: nothing is played.
Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47847
SINE_RMS = 1 / math.sqrt(2)


def write_note(path: Path, frequency: float, level: float, seconds: float, rate: int = 48_000) -> None:
    frames = int(seconds * rate)
    data = bytearray()
    for index in range(frames):
        value = level * math.sin(2 * math.pi * frequency * index / rate)
        data += struct.pack("<hh", int(round(value * 32767)), int(round(value * 32767)))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(data))


def request(command: dict) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(180)
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
        if banks and (Path(banks) / "big-rusty-drums").is_dir():
            return banks
    return None


SFZ = """// CyDAW's live SFZ check
<control>
label_cc70=Close mic
label_cc71=Room mic
label_cc20=Unused
set_cc70=127
<global> sw_lokey=c1 sw_hikey=d1 sw_default=c1 lokey=a2 hikey=a5 pitch_keycenter=a3
<group> sw_last=c1 sw_label=Sustain
<region> sample=close.wav amplitude_oncc70=100
<region> sample=room.wav amplitude_oncc71=100 locc71=1
<region> sample=release.wav trigger=release rt_decay=6
<group> sw_last=c#1 sw_label=Staccato
<region> sample=staccato.wav
"""


def scenario(scratch: Path, banks: str | None) -> None:
    if not need({"cmd": "status"}, "status").get("speakersSilenced"):
        raise RuntimeError("the session is not silent")
    need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
    notes = scratch / "notes"
    notes.mkdir()
    write_note(notes / "close.wav", 220.0, 0.5, 3.0)
    write_note(notes / "room.wav", 220.0, 0.25, 3.0)
    write_note(notes / "release.wav", 880.0, 0.5, 1.2)
    write_note(notes / "staccato.wav", 220.0, 0.125, 3.0)
    write_note(notes / "long.wav", 220.0, 0.5, 9.0)
    sfz = notes / "live.sfz"
    sfz.write_text(SFZ, encoding="utf-8")
    (notes / "long.sfz").write_text("<region> sample=long.wav pitch_keycenter=a3\n", encoding="utf-8")
    track = need({"cmd": "load_sampler", "file": str(sfz).replace(os.sep, "/")}, "load")["track"]
    need({"cmd": "sampler_status", "track": track, "wait": 30}, "decode")

    def controls(**fields) -> dict:
        return need(dict({"cmd": "sampler_controls", "track": track}, **fields), "controls")

    def play(*notes_played, seconds=1.0, controllers=None) -> dict:
        command = {"cmd": "instrument_export_check", "track": track, "seconds": seconds,
                   "notes": [list(note) for note in notes_played]}
        if controllers:
            command["controllers"] = controllers
        return need(command, "play")

    # ---- What the card shows ----
    first = controls()
    labels = [item["label"] for item in first.get("controls", [])]
    keys = [item["key"] for item in first.get("articulations", [])]
    named = {item["key"]: item.get("label") for item in first.get("articulations", [])}
    check(labels == ["Close mic", "Room mic"], f"the controls its zones read, by their names ({labels})")
    check(keys == [24, 25, 26] and named.get(24) == "Sustain" and named.get(25) == "Staccato"
          and first.get("articulation") == 24, f"its articulations, the default chosen ({keys}, {named})")
    check(first.get("releaseZones") == 1 and first.get("deferred") == 2,
          f"a release zone; the room microphone and the other articulation not decoded yet "
          f"(release {first.get('releaseZones')}, deferred {first.get('deferred')})")

    # ---- Controllers ----
    whole = play((57, 1.0, 0.0)).get("rms", 0.0)
    check(abs(whole - 0.5 * SINE_RMS) < 0.5 * SINE_RMS * 0.05, f"it opens with its close microphone alone ({whole:.4f})")
    controls(set={"70": 63.5})
    half = play((57, 1.0, 0.0)).get("rms", 0.0)
    check(abs(half - 0.25 * SINE_RMS) < 0.25 * SINE_RMS * 0.05, f"a controller set on the card levels its zones ({half:.4f})")
    raised = controls(set={"71": 127})
    need({"cmd": "sampler_status", "track": track, "wait": 30}, "decode the room")
    both = play((57, 1.0, 0.0)).get("rms", 0.0)
    check(raised.get("deferred") <= 1 and abs(both - 0.5 * SINE_RMS) < 0.5 * SINE_RMS * 0.05,
          f"a microphone raised from zero is decoded and plays ({both:.4f}, deferred {raised.get('deferred')})")
    controls(set={"70": -1, "71": -1})
    moved = play((57, 1.0, 0.0), seconds=1.0, controllers=[[70, 0, 0.5]])
    windows = moved.get("windowsDb", [])
    check(len(windows) >= 20 and windows[5] > -12 and windows[15] < -60,
          f"MIDI moves a controller while the note plays ({[round(x) for x in windows[3:17]]})")
    restored = controls()
    check(all(not item["set"] for item in restored["controls"]), "a control set below zero goes back to the instrument's start")

    # ---- Articulations ----
    controls(set={"articulation": 25})
    staccato = play((57, 1.0, 0.0)).get("rms", 0.0)
    check(abs(staccato - 0.125 * SINE_RMS) < 0.125 * SINE_RMS * 0.08, f"the card chooses the articulation ({staccato:.4f})")
    controls(set={"articulation": -1})
    switched = play((25, 0.8, 0.0), (57, 1.0, 0.05)).get("windowsDb", [])
    level = 10 ** (switched[10] / 20) if len(switched) > 10 else 0.0
    check(abs(level - 0.125 * SINE_RMS) < 0.125 * SINE_RMS * 0.15,
          f"a keyswitch played chooses it too, and sounds nothing itself ({level:.4f})")

    # ---- The release ----
    # Let go after a second: the release (1.2 s) plays from there, the note's
    # envelope closes under it.
    released = play((57, 1.0, 0.0, 1.0), seconds=2.6).get("windowsDb", [])
    # Held a second: 6 dB lower than the file (0.5, 880 Hz).
    expected = 20 * math.log10(0.5 * SINE_RMS * 10 ** (-6 / 20))
    after = released[30:42] if len(released) > 50 else []
    check(bool(after) and all(abs(value - expected) < 1.5 for value in after),
          f"a key let go plays its release, lower the longer it was held ({[round(x, 1) for x in after]} for {expected:.1f})")
    check(len(released) > 50 and released[46] < -100 and released[-1] < -100,
          f"the release plays once through, then the voice ends ({[round(x) for x in released[42:48]]})")

    # ---- A long recording read from the disk ----
    long_track = need({"cmd": "load_sampler", "file": str(notes / "long.sfz").replace(os.sep, "/")}, "long")["track"]
    status = need({"cmd": "sampler_status", "track": long_track, "wait": 30}, "long status")
    check(status.get("streamedZones") == 1 and status.get("streamedHeadMegabytes", 99) < 0.3,
          f"a nine-second recording keeps only its head in memory ({status.get('streamedHeadMegabytes', 0):.2f} MB)")
    held = need({"cmd": "instrument_export_check", "track": long_track, "notes": [[57, 1.0, 0.0, 7.5]], "seconds": 7.4},
                "long note").get("windowsDb", [])
    steady = held[4:140]
    expected = 20 * math.log10(0.5 * SINE_RMS)
    after_status = need({"cmd": "sampler_status", "track": long_track}, "long status after")
    check(len(steady) > 100 and all(abs(value - expected) < 0.2 for value in steady)
          and after_status.get("streamUnderruns") == status.get("streamUnderruns"),
          f"it plays through, read from the disk, without a gap ({min(steady, default=0):.2f} .. {max(steady, default=0):.2f} dB"
          f" for {expected:.2f}; missed {after_status.get('streamUnderruns')})")

    # ---- Saved and opened ----
    controls(set={"70": 90, "articulation": 25})
    project = scratch / "Live.cydaw"
    need({"cmd": "save_project", "path": str(project)}, "save")
    need({"cmd": "new_project"}, "new")
    need({"cmd": "open_project", "path": str(project)}, "open")
    kept = {}
    for _ in range(150):
        answer = request({"cmd": "sampler_controls", "track": track})
        if answer.get("ok"):
            kept = answer["result"]
            break
        time.sleep(0.2)
    close = next((item for item in kept.get("controls", []) if item["cc"] == 70), {})
    check(abs(close.get("value", 0) - 90) < 1e-3 and kept.get("articulation") == 25,
          f"a saved project keeps the card's controls ({close.get('value')}, {kept.get('articulation')})")
    need({"cmd": "get_track_graph", "track": track}, "select")
    need({"cmd": "show_dialog", "name": "sampler-controls"}, "the controls' tab")

    # ---- Batterie's microphones ----
    if banks is None:
        print("skip: the drum banks are not on this computer")
        return
    drums = need({"cmd": "add_midi_track", "name": "Batterie", "instrument": "batterie-instrument"}, "batterie")["name"]
    need({"cmd": "sampler_status", "track": drums, "wait": 60}, "kit")
    kit = need({"cmd": "sampler_controls", "track": drums}, "kit controls")
    names = [item["label"] for item in kit.get("controls", [])]
    check("Kick mic" in names and "Kick OH" in names and kit.get("deferred", 0) > 0,
          f"the kit's microphones are on its card ({len(names)} controls, {kit.get('deferred')} zones waiting)")
    dry = need({"cmd": "instrument_export_check", "track": drums, "note": 36, "velocity": 0.9, "seconds": 1.5}, "kick")
    opened = need({"cmd": "sampler_controls", "track": drums, "set": {"71": 127}}, "kick overheads")
    need({"cmd": "sampler_status", "track": drums, "wait": 60}, "overheads decoded")
    wet = need({"cmd": "instrument_export_check", "track": drums, "note": 36, "velocity": 0.9, "seconds": 1.5}, "kick again")
    check(opened.get("deferred", 0) < kit.get("deferred", 0) and wet.get("rms", 0) > dry.get("rms", 0) * 1.5,
          f"the kick's overheads raised sound with it ({dry.get('rms', 0):.4f} -> {wet.get('rms', 0):.4f})")
    need({"cmd": "get_track_graph", "track": drums}, "select drums")
    need({"cmd": "show_dialog", "name": "batterie-mics"}, "the mics' tab")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    banks = banks_folder()
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-sfz-live-"))
    locations = scratch / "Locations.json"
    folders = {"presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}
    if banks:
        folders["banks"] = banks
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": folders}), encoding="utf-8")
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
        scenario(scratch, banks)
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
