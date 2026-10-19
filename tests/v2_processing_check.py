"""The Traitements mode (processings): audio and MIDI ones, made, tried and applied. Silent.

    python tests/v2_processing_check.py --exe "<CyDAW 2.exe>" [--port 47871]

Sounds are made for it (a tone with noise, hum and clicks; a short burst; noise that never ends).

* The factory's processings listed (audio and MIDI), each with its settings.
* Remove Noise lowers the background noise and keeps the tone; Remove Hum finds 50 Hz and notches it.
* Tape Echo lets its repeats ring past the end of the sound (the sound longer), and stops when they fade;
  still fading at the most allowed, cut there; a processing that makes noise without end ("endless") is
  cut soon after its level stops falling.
* A tail not allowed: the sound keeps its length.
* Code checked: JSFX and note scripts, a mistake reported with its line.
* A processing of one's own saved (a JSFX step and a gain), listed among "mine", then deleted.
* On clips: an audio processing applied (a new version of the clip, longer with its tail), undone;
  Humanize and Strum on a MIDI clip (the notes moved, as many), undone; Note Echo adds notes.
* The mode's window shown, its apply window opened on a clip.

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
port = 47871
rate = 48_000


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


def processing(what: str, **fields) -> dict:
    return need(dict({"cmd": "processing"}, **fields), what)


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def write_wav(path: Path, samples: list[float]) -> None:
    data = bytearray()
    for value in samples:
        packed = struct.pack("<i", int(max(-1.0, min(1.0, value)) * 8_388_607))[:3]
        data += packed + packed
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(3)
        out.setframerate(rate)
        out.writeframes(bytes(data))


def read_wav(path: Path) -> list[float]:
    """The left channel."""
    with wave.open(str(path), "rb") as source:
        channels, width, frames = source.getnchannels(), source.getsampwidth(), source.getnframes()
        raw = source.readframes(frames)
    step = channels * width
    out = []
    for i in range(0, len(raw), step):
        if width == 3:
            value = int.from_bytes(raw[i:i + 3], "little", signed=True) / 8_388_608.0
        elif width == 2:
            value = int.from_bytes(raw[i:i + 2], "little", signed=True) / 32_768.0
        else:
            value = struct.unpack("<f", raw[i:i + 4])[0]
        out.append(value)
    return out


def rms_db(samples: list[float]) -> float:
    return 10 * math.log10(sum(s * s for s in samples) / max(1, len(samples)) + 1e-24)


def tone_level_db(samples: list[float], hz: float) -> float:
    """The level of one frequency (a Goertzel over the whole sound)."""
    k = 2 * math.cos(2 * math.pi * hz / rate)
    s1 = s2 = 0.0
    for x in samples:
        s1, s2 = x + k * s1 - s2, s1
    power = s1 * s1 + s2 * s2 - k * s1 * s2
    return 10 * math.log10(power / (len(samples) ** 2 / 4) + 1e-24)


def make_sounds(folder: Path) -> dict[str, Path]:
    random.seed(7)
    noisy = []
    for i in range(rate * 4):
        t = i / rate
        # The tone from 0.75 s to 3.25 s: the noise alone around it.
        envelope = math.sin(math.pi * (t - 0.75) / 2.5) ** 2 if 0.75 < t < 3.25 else 0.0
        noisy.append(0.4 * envelope * math.sin(2 * math.pi * 440 * t) + 0.03 * random.gauss(0, 1)
                     + 0.05 * math.sin(2 * math.pi * 50 * t))
    for at in (rate + 123, rate * 3 + 999):
        noisy[at] += 0.8
    burst = [0.6 * math.exp(-8 * i / rate) * math.sin(2 * math.pi * 330 * i / rate) if i < rate // 2 else 0.0
             for i in range(rate)]
    files = {"noisy": folder / "noisy.wav", "burst": folder / "burst.wav"}
    write_wav(files["noisy"], noisy)
    write_wav(files["burst"], burst)
    return files


# A processing of one's own: a JSFX step that adds noise for ever, a gain after it.
NOISE_MAKER = """desc:Noise maker
slider1:0.1<0,0.5,0.01>Level

@sample
spl0 += (rand(2) - 1) * slider1;
spl1 += (rand(2) - 1) * slider1;
"""


def scenario(scratch: Path) -> None:
    sounds = make_sounds(scratch)
    out = scratch / "out"
    out.mkdir()

    # ---- The library ----
    audio = processing("audio list", kind="audio")["processings"]
    midi = processing("MIDI list", kind="midi")["processings"]
    names = {p["name"] for p in audio}
    check({"Remove Noise", "Remove Hum", "Remove Clicks", "Voice Clean-up", "Tape Echo", "Room Reverb"} <= names,
          f"the factory's audio processings ({len(audio)})")
    check({"Humanize", "Swing", "Strum", "Note Echo"} <= {p["name"] for p in midi}, f"the factory's MIDI ones ({len(midi)})")
    check(all(p["factory"] for p in audio + midi) and all(p["parameters"] for p in audio + midi),
          "each comes from CyDAW and has settings")
    noise = next(p for p in audio if p["name"] == "Remove Noise")
    check(any(parameter["id"] == "0/reduction" for parameter in noise["parameters"]), "Remove Noise's reduction is a setting")

    # ---- Noise and hum ----
    before = read_wav(sounds["noisy"])
    processing("noise", render="Remove Noise", file=str(sounds["noisy"]), out=str(out / "denoised.wav"))
    after = read_wav(out / "denoised.wav")
    quiet = slice(0, rate // 2)
    lowered = rms_db(before[quiet]) - rms_db(after[quiet])
    kept = tone_level_db(before, 440) - tone_level_db(after, 440)
    check(lowered > 6.0 and abs(kept) < 1.0, f"Remove Noise: the noise {lowered:.1f} dB lower, the tone within {kept:.2f} dB")
    stronger = processing("noise harder", render="Remove Noise", file=str(sounds["noisy"]), out=str(out / "denoised2.wav"),
                          values={"0/reduction": 36})
    harder = rms_db(before[quiet]) - rms_db(read_wav(out / "denoised2.wav")[quiet])
    check(harder > lowered + 1.0, f"a stronger reduction lowers it further ({harder:.1f} dB)")
    hum = processing("hum", render="Remove Hum", file=str(sounds["noisy"]), out=str(out / "dehummed.wav"))
    dehummed = read_wav(out / "dehummed.wav")
    notched = tone_level_db(before, 50) - tone_level_db(dehummed, 50)
    check(any("50" in note for note in hum["notes"]) and notched > 20.0,
          f"Remove Hum found 50 Hz and took {notched:.1f} dB of it out ({hum['notes']})")

    # ---- Tails ----
    echo = processing("echo", render="Tape Echo", file=str(sounds["burst"]), out=str(out / "echo.wav"))
    check(echo["framesAfter"] > echo["framesBefore"] + rate // 2 and not echo["endless"] and echo["tailSeconds"] < 5.0,
          f"Tape Echo rings past the end and stops when it fades ({echo['tailSeconds']:.2f} s more)")
    tail_end = read_wav(out / "echo.wav")[-rate // 50:]
    check(max(abs(s) for s in tail_end) < 1e-3, "the tail ends faded out")
    shorter = processing("echo, no tail", render="Tape Echo", file=str(sounds["burst"]), out=str(out / "echo2.wav"),
                         tail={"allow": False})
    check(shorter["framesAfter"] == shorter["framesBefore"], "no tail allowed: the sound keeps its length")
    longer = processing("echo, more feedback", render="Tape Echo", file=str(sounds["burst"]), out=str(out / "echo3.wav"),
                        values={"0/slider2": 85})
    check(longer["tailSeconds"] > echo["tailSeconds"] + 0.5, f"more feedback, a longer tail ({longer['tailSeconds']:.2f} s)")
    check(longer["endless"] and abs(longer["tailSeconds"] - 10.0) < 0.05,
          "still fading at the most allowed (10 s): cut there")

    mine = {"format": "cydaw-processing", "version": 1, "kind": "audio", "name": "Check Noise Maker",
            "description": "Noise for ever (the test's).",
            "steps": [{"type": "code", "code": NOISE_MAKER, "settings": {"slider1": 0.1}},
                      {"type": "gain", "settings": {"db": -6}}],
            "tail": {"allow": True, "thresholdDb": -60, "holdSeconds": 0.3, "maxSeconds": 3, "fadeMs": 30}}
    saved = processing("save", save=mine)
    check(Path(saved["file"]).is_file(), "a processing of one's own is saved")
    listed = [p for p in processing("list", kind="audio")["processings"] if p["name"] == "Check Noise Maker"]
    check(len(listed) == 1 and not listed[0]["factory"] and listed[0]["steps"] == ["code", "gain"],
          "it is listed among mine, its steps in order")
    endless = processing("endless", render="Check Noise Maker", file=str(sounds["burst"]), out=str(out / "endless.wav"))
    check(endless["endless"] and 0.2 < endless["tailSeconds"] < 1.5,
          f"noise without end: cut soon after it stops fading, not at the most allowed ({endless['tailSeconds']:.2f} s)")
    end = read_wav(out / "endless.wav")[-rate // 100:]
    check(max(abs(s) for s in end[-48:]) < 0.02, "its cut is faded out")

    # ---- Code checked ----
    good = processing("jsfx ok", check="desc:x\nslider1:0.5<0,1,0.01>Gain\n@sample\nspl0 *= slider1;\n", language="jsfx")
    bad = processing("jsfx bad", check="desc:x\n@sample\nspl0 = = 3;\n", language="jsfx")
    check(good["ok"] and good["sliders"] == ["Gain"] and not bad["ok"] and "3" in bad["error"],
          f"JSFX checked: its sliders, a mistake with its line ({bad['error']})")
    script = processing("notes ok", check="param1:20<0,100,1>Timing\n@note\nstart += random(1);\n", language="notes")
    broken = processing("notes bad", check="@note\nstart += ;\n", language="notes")
    check(script["ok"] and script["settings"] == ["Timing"] and not broken["ok"] and "@note" in broken["error"],
          f"note scripts checked ({broken['error']})")

    # ---- On clips ----
    project = scratch / "Processing.cydaw"
    need({"cmd": "save_project", "path": str(project)}, "first save")
    track = need({"cmd": "add_audio_track", "file": str(sounds["burst"])}, "audio track")["name"]

    def clip() -> dict:
        return need({"cmd": "list_audio_clips", "track": track}, "clips")["clips"][0]

    first = clip()
    applied = processing("apply echo", apply="Tape Echo", track=track, clip=first["id"])
    now = clip()
    check(now["source"] != first["source"] and now["duration"] > first["duration"] + 0.4,
          f"applied to the clip: a new sound, longer by its tail ({first['duration']:.2f} -> {now['duration']:.2f} s)")
    history = need({"cmd": "audio_clip_history", "track": track, "clipId": first["id"]}, "history")
    labels = [version["label"] for version in history["versions"]]
    check(len(labels) == 2 and "Tape Echo" in labels[-1], f"a version of the clip ({labels})")
    need({"cmd": "undo", "scope": "arrangement"}, "undo")
    back = clip()
    check(back["source"] == first["source"] and abs(back["duration"] - first["duration"]) < 1e-6, "undone: the clip as it was")
    processing("apply noise", apply="Remove Noise", track=track, clip=first["id"])
    check(abs(clip()["duration"] - first["duration"]) < 1e-6, "a processing without a tail keeps the clip's length")
    need({"cmd": "undo", "scope": "arrangement"}, "undo noise")

    need({"cmd": "add_midi_track", "name": "Keys"}, "MIDI track")
    added = need({"cmd": "add_midi_clip", "track": "Keys", "name": "Chords", "startBeats": 0, "lengthBeats": 8}, "MIDI clip")
    chords = [{"note": n, "start": float(bar), "length": 1.0, "velocity": 0.8} for bar in range(0, 8, 2) for n in (60, 64, 67)]
    need({"cmd": "add_notes", "track": "Keys", "clip": "Chords", "notes": chords}, "notes")

    def notes() -> list[dict]:
        return need({"cmd": "list_notes", "track": "Keys", "clip": "Chords"}, "list notes")["clips"][0]["notes"]

    original = sorted((n["start"], n["note"]) for n in notes())
    strum = processing("strum", apply="Strum", track="Keys", clip=added["clipId"], values={"param1": 40})
    strummed = notes()
    starts = sorted({round(n["start"], 4) for n in strummed if 0 <= n["start"] < 1})
    check(strum["notesAfter"] == len(chords) and len(starts) == 3 and starts[1] > starts[0],
          f"Strum: the chord's notes one after another ({starts})")
    need({"cmd": "undo", "scope": "midi"}, "undo strum")
    check(sorted((n["start"], n["note"]) for n in notes()) == original, "undone: the chords as they were")
    processing("humanize", apply="Humanize", track="Keys", clip=added["clipId"], seed=5)
    moved = notes()
    shifted = sum(1 for a, b in zip(sorted(original), sorted((n["start"], n["note"]) for n in moved)) if abs(a[0] - b[0]) > 1e-4)
    check(len(moved) == len(chords) and shifted >= len(chords) // 2, f"Humanize moves the notes a little ({shifted} moved)")
    check(all(abs(n["start"] - round(n["start"] / 2) * 2) < 0.1 for n in moved), "by a little")
    need({"cmd": "undo", "scope": "midi"}, "undo humanize")
    echo_notes = processing("note echo", apply="Note Echo", track="Keys", clip=added["clipId"], values={"param1": 2})
    check(echo_notes["notesAfter"] > len(chords), f"Note Echo adds notes ({echo_notes['notesBefore']} -> {echo_notes['notesAfter']})")
    need({"cmd": "undo", "scope": "midi"}, "undo echo")
    refused = request({"cmd": "processing", "apply": "Humanize", "track": track, "clip": first["id"]})
    check(not refused.get("ok"), "a MIDI processing is refused on an audio clip")

    # ---- The window ----
    check(need({"cmd": "app_mode", "mode": "processing"}, "mode").get("mode") == "processing", "the Traitements mode shows")
    shown = processing("select", select="Remove Noise")
    check(shown.get("selected") == "Remove Noise", f"its window selects a processing ({shown.get('selected')})")
    processing("open apply", open=True, track=track, clip=first["id"])
    time.sleep(0.5)
    state = processing("state", state=True)
    check(state.get("applyOpen") is True, "the apply window opens on a clip")

    processing("delete", delete="Check Noise Maker")
    check(not [p for p in processing("list", kind="audio")["processings"] if p["name"] == "Check Noise Maker"],
          "a processing of one's own is deleted")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-processing-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "processings": str(scratch / "Processings"), "presets": str(scratch / "Presets"),
        "cache": str(scratch / "Cache"), "foundry": str(scratch / "Foundry")}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
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
        scenario(scratch)
    except Exception as error:  # noqa: BLE001
        failures.append(str(error))
        print(f"FAIL {error}", flush=True)
    finally:
        try:
            request({"cmd": "quit"})
        except OSError:
            pass
        try:
            process.wait(timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
    print(f"{len(failures)} failure(s)" if failures else "all hold")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
