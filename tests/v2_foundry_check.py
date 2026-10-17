"""The Foundry in the software: sound files edited. Silent, nothing played through the speakers.

    python tests/v2_foundry_check.py --exe "<CyDAW 2.exe>" [--port 47855]

A 1 kHz tone (3 s, stereo, 48 kHz, 24 bits, -6 dBFS) is made for it.

* Opened: its length, rate, channels, bits; its statistics (peak, RMS).
* A gain on the left only, undone; normalised; a fade in; cut, pasted, undone; silence inserted;
  stretched; a new rate; mixed to mono.
* Markers and a region kept through a WAV saved and opened again.
* A track's effects (the utility's "Trim -6 dB") rendered into the sound.
* Several tracks: a sound imported on a second track, a clip moved, a cut over both, a split, a solo,
  the mix exported, the session saved and opened again.
* Sent to the studio: a new audio track; played and stopped.

Exit code 0 when all hold.
"""
from __future__ import annotations

import argparse
import json
import math
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47855


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


def foundry(what: str, **fields) -> dict:
    return need(dict({"cmd": "foundry"}, **fields), what)


def check(condition: bool, message: str) -> None:
    print(("ok   " if condition else "FAIL ") + message, flush=True)
    if not condition:
        failures.append(message)


def tab(state: dict) -> dict:
    return state["tabs"][state["current"]]


def make_tone(path: Path, seconds: float = 3.0, rate: int = 48_000) -> None:
    frames = int(seconds * rate)
    data = bytearray()
    for i in range(frames):
        value = int(0.5 * math.sin(2 * math.pi * 1_000.0 * i / rate) * 8_388_607)
        packed = struct.pack("<i", value)[:3]
        data += packed + packed
    with wave.open(str(path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(3)
        out.setframerate(rate)
        out.writeframes(bytes(data))


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-foundry-check-"))
    tone = scratch / "tone.wav"
    make_tone(tone)
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "foundry": str(scratch / "Foundry"), "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
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
        check(need({"cmd": "app_mode", "mode": "foundry"}, "mode").get("mode") == "foundry", "the window is in the Foundry")

        state = foundry("open", action="open", file=str(tone))
        t = tab(state)
        check(t["frames"] == 144_000 and t["rate"] == 48_000 and t["channels"] == 2 and t["bits"] == 24,
              f"the file opened as it is ({t['frames']} frames, {t['rate']} Hz, {t['channels']} ch, {t['bits']} bits)")
        stats = foundry("stats", action="stats")["stats"]
        check(abs(stats["peakDb"][0] + 6.02) < 0.05 and abs(stats["rmsDb"][0] + 9.03) < 0.05,
              f"its statistics ({stats['peakDb'][0]:.2f} dB peak, {stats['rmsDb'][0]:.2f} dB RMS)")

        # A gain on the left only, undone.
        foundry("select", action="select", start=1.0, end=2.0, seconds=True, channels="left")
        foundry("gain", action="process", name="gain", gainDb=6.0)
        stats = foundry("stats", action="stats")["stats"]
        check(abs(stats["peakDb"][0] + 0.0) < 0.1 and abs(stats["peakDb"][1] + 6.02) < 0.05,
              f"+6 dB on the left only ({stats['peakDb'][0]:.2f}, {stats['peakDb'][1]:.2f} dB)")
        foundry("undo", action="undo")
        stats = foundry("stats", action="stats")["stats"]
        check(abs(stats["peakDb"][0] + 6.02) < 0.05, "undone")

        # Normalised, faded in.
        foundry("select all", action="edit", op="select_all")
        foundry("normalise", action="process", name="normalise", targetDb=-1.0, rms=False)
        stats = foundry("stats", action="stats")["stats"]
        check(abs(stats["peakDb"][0] + 1.0) < 0.05, f"normalised to -1 dBFS ({stats['peakDb'][0]:.2f})")
        foundry("select", action="select", start=0.0, end=0.5, seconds=True)
        foundry("fade in", action="process", name="fade_in", curve=0)
        foundry("select", action="select", start=0, end=480)
        stats = foundry("stats", action="stats")["stats"]
        check(stats["peakDb"][0] < -30.0, f"a fade in starts from silence ({stats['peakDb'][0]:.1f} dB over the first 10 ms)")

        # Cut, pasted, undone; silence; stretch.
        foundry("select", action="select", start=1.0, end=2.0, seconds=True)
        state = foundry("cut", action="edit", op="cut")
        check(tab(state)["frames"] == 96_000 and state["clipboardFrames"] == 48_000, "a second cut out")
        foundry("cursor", action="cursor", at=0)
        state = foundry("paste", action="edit", op="paste")
        check(tab(state)["frames"] == 144_000, "pasted back at the start")
        foundry("undo", action="undo")
        state = foundry("undo", action="undo")
        check(tab(state)["frames"] == 144_000 and tab(state)["redo"] != "", "undone twice: the length as before")
        foundry("cursor", action="cursor", at=0.5, seconds=True)
        state = foundry("silence", action="process", name="insert_silence", duration=0.5)
        check(tab(state)["frames"] == 168_000, "half a second of silence inserted")
        foundry("select", action="select", start=0.0, end=1.0, seconds=True)
        state = foundry("stretch", action="process", name="stretch", speed=2.0, pitch=0.0, algorithm=1)
        check(abs(tab(state)["frames"] - 144_000) <= 2, f"a second played twice as fast: half as long ({tab(state)['frames']})")

        # Markers kept through a WAV; a new rate; mono.
        foundry("cursor", action="cursor", at=0.25, seconds=True)
        foundry("marker", action="marker", **{"do": "add"})
        foundry("select", action="select", start=1.0, end=1.5, seconds=True)
        foundry("region", action="marker", **{"do": "region"})
        foundry("rate", action="process", name="rate", rate=44_100)
        state = foundry("mono", action="process", name="channels", channels=0)
        t = tab(state)
        check(t["rate"] == 44_100 and t["channels"] == 1 and abs(t["frames"] - 132_300) <= 2,
              f"at 44.1 kHz, in mono ({t['rate']} Hz, {t['channels']} ch, {t['frames']} frames)")
        saved = scratch / "edited.wav"
        state = foundry("save", action="save", path=str(saved), bits=24)
        check(not tab(state)["dirty"] and saved.exists(), "saved")
        foundry("close", action="close")
        state = foundry("open again", action="open", file=str(saved))
        t = tab(state)
        markers = t["markers"]
        check(len(markers) == 2 and markers[0]["start"] == markers[0]["end"] and markers[1]["end"] - markers[1]["start"] == 22_050,
              f"the marker and the region kept in the WAV ({markers})")
        check(t["rate"] == 44_100 and t["channels"] == 1, "opened again as saved")

        # A track's effects.
        audio = need({"cmd": "add_audio_track", "file": str(tone)}, "audio track")["name"]
        node = [item["id"] for item in need({"cmd": "insert_device", "track": audio, "key": "utilitaire"}, "utility")["nodes"]
                if item.get("type") == "utilitaire"][-1]
        need({"cmd": "presets", "track": audio, "nodeId": node, "action": "load", "name": "Trim -6 dB"}, "Trim -6 dB")
        foundry("select all", action="edit", op="select_all")
        before = foundry("stats", action="stats")["stats"]["peakDb"][0]
        foundry("effects", action="process", name="effects", track=audio, tail=0.0)
        foundry("select", action="select", start=0.5, end=1.5, seconds=True)
        after = foundry("stats", action="stats")["stats"]["peakDb"][0]
        check(abs((after - before) + 6.0) < 0.3, f"the track's effects rendered into the sound ({before:.2f} -> {after:.2f} dB)")

        # Several tracks: a second sound on its own track, moved, cut over both, split, soloed.
        tone2 = scratch / "tone2.wav"
        make_tone(tone2, seconds=2.0)
        state = foundry("new session", action="new", rate=48_000, name="Session")
        state = foundry("import 1", action="import", file=str(tone), track=0, at=0)
        state = foundry("import 2", action="import", file=str(tone2), track=-1, at=0.5, seconds=True)
        t = tab(state)
        check(len(t["tracks"]) == 2 and t["tracks"][1]["clips"][0]["start"] == 24_000 and not t["plainFile"],
              f"two tracks, the second sound at 0.5 s ({[len(x['clips']) for x in t['tracks']]})")
        clip = t["tracks"][1]["clips"][0]["id"]
        state = foundry("move", action="clip", id=clip, start=1.0, seconds=True)
        check(tab(state)["tracks"][1]["clips"][0]["start"] == 48_000, "a clip moved to 1 s")
        state = foundry("select both", action="select", start=1.5, end=2.0, seconds=True, tracks=[0, 1])
        state = foundry("cut both", action="edit", op="cut")
        t = tab(state)
        check(sum(c["length"] for c in t["tracks"][0]["clips"]) == 144_000 - 24_000
              and sum(c["length"] for c in t["tracks"][1]["clips"]) == 96_000 - 24_000,
              "a cut over both tracks takes the stretch out of each")
        state = foundry("cursor", action="cursor", at=0.5, seconds=True, track=0)
        state = foundry("split", action="edit", op="split")
        # The cut inside the first clip left it one clip; split: two.
        check(len(tab(state)["tracks"][0]["clips"]) == 2, f"split at the cursor on the first track ({len(tab(state)['tracks'][0]['clips'])} clips)")
        foundry("solo", action="track", track=1, solo=True)
        foundry("select all", action="edit", op="select_all")
        solo = foundry("stats", action="stats")["stats"]
        foundry("solo off", action="track", track=1, solo=False)
        both = foundry("stats", action="stats")["stats"]
        check(abs(solo["peakDb"][0] + 6.02) < 0.1 and both["peakDb"][0] > solo["peakDb"][0] + 3.0,
              f"a solo track alone in the mix ({solo['peakDb'][0]:.2f} dB), both louder ({both['peakDb'][0]:.2f} dB)")
        mix = scratch / "mix.wav"
        foundry("export", action="export", path=str(mix), bits=24)
        with wave.open(str(mix), "rb") as file:
            exported = file.getnframes()
        check(exported == tab(foundry("status"))["frames"], f"the mix exported whole ({exported} frames)")
        session_file = scratch / "Session.cyfoundry"
        foundry("save session", action="save_session", path=str(session_file))
        foundry("close", action="close")
        state = foundry("open session", action="open", file=str(session_file))
        t = tab(state)
        check(len(t["tracks"]) == 2 and len(t["tracks"][0]["clips"]) == 2 and t["sessionFile"] != "",
              f"the session opened again with its tracks and clips ({[len(x['clips']) for x in t['tracks']]})")

        # To the studio; played.
        tracks = len(need({"cmd": "status"}, "status").get("audioTracks", []))
        foundry("to the studio", action="send_to_studio")
        check(len(need({"cmd": "status"}, "status").get("audioTracks", [])) == tracks + 1, "sent to the studio: a new track")
        need({"cmd": "app_mode", "mode": "foundry"}, "mode")
        foundry("play", action="play")
        time.sleep(0.5)
        state = foundry("status")
        check(state["playing"] and state["position"] > 1_000, f"it plays ({state['position']:.0f})")
        foundry("stop", action="stop")
        time.sleep(0.3)
        check(not foundry("status")["playing"], "it stops")
    except Exception as error:  # noqa: BLE001
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:  # noqa: BLE001
            process.kill()
    print(f"{len(failures)} failure(s)" if failures else "all checks hold", flush=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
