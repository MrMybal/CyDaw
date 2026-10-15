"""The creative effects, the MIDI effects, the pad rack, Silex and Nuée in the software.
Silent, offline.

    python tests/v2_creative_check.py --exe "<CyDAW 2.exe>" [--port 47852]

The presets folder is the test's own (CYDAW_LOCATIONS_FILE). The audio effects are checked as an
exported plug-in gets them (effect_chain_check: a sine through the track's effects offline); the
instruments and the MIDI effects by the mix exported to a file. Nothing is played.

* Each audio effect goes on an audio track with its factory presets; its first preset plays a sine
  bounded and audible. The tuner and the stereoscope leave the sound as it is; the utility's
  "Trim -6 dB" lowers it by 6 dB.
* Each MIDI effect goes between the track's MIDI input and its instrument, in the order added, with
  its factory presets; the track's notes go through it (status: midiEffects).
* Note repeat at 1/16 makes a held note into hits; the arpeggiator plays a held chord one note at a time.
* A pad rack starts with its kit; its pads sound, a muted pad is quiet, a saved project keeps them.
* Silex plays its kick; Nuée plays a loaded sound as grains.

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
import sys
import tempfile
import time
import wave
from pathlib import Path

failures: list[str] = []
port = 47852
ROOT = Path(__file__).resolve().parents[1]

AUDIO_EFFECTS = ["vocoder", "harmonie", "justesse", "de-esser", "pompe", "empreinte", "givre", "grains", "saccade", "boucleur",
                 "decaleur", "anneau", "rotor", "resonateurs", "corps", "peigne", "utilitaire", "tuner", "stereoscope"]
MIDI_EFFECTS = ["arpege", "accords", "gamme", "echo-notes", "alea", "duree", "sequenceur", "roulement"]


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


def read_wave(path: Path) -> tuple[list[float], int]:
    """The left channel of a WAV file, -1..1."""
    with wave.open(str(path), "rb") as file:
        rate, width, channels = file.getframerate(), file.getsampwidth(), file.getnchannels()
        raw = file.readframes(file.getnframes())
    step = width * channels
    samples = []
    for at in range(0, len(raw) - step + 1, step):
        frame = raw[at:at + width]
        if width == 3:
            samples.append(int.from_bytes(frame + (b"\xff" if frame[2] & 0x80 else b"\x00"), "little", signed=True) / 8_388_608.0)
        elif width == 4:
            samples.append(struct.unpack("<f", frame)[0])
        else:
            samples.append(int.from_bytes(frame, "little", signed=True) / 32_768.0)
    return samples, rate


def rms(samples: list[float], rate: int, start: float, end: float) -> float:
    part = samples[int(start * rate):int(end * rate)]
    return math.sqrt(sum(value * value for value in part) / max(1, len(part)))


def onsets(samples: list[float], rate: int, seconds: float) -> int:
    """Sudden rises of the 5 ms envelope: a new hit over the last one's tail."""
    hop = max(1, rate // 200)
    envelope = []
    for at in range(0, min(len(samples), int(seconds * rate)), hop):
        part = samples[at:at + hop]
        envelope.append(max(abs(value) for value in part) if part else 0.0)
    peak = max(envelope) if envelope else 0.0
    hits, last = 0, -100
    for index in range(2, len(envelope)):
        if envelope[index] > 0.1 * peak and envelope[index] > 1.5 * envelope[index - 2] + 1e-4 and index - last >= 8:
            hits += 1
            last = index
    return hits


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-creative-check-"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
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
        if not need({"cmd": "status"}, "status").get("speakersSilenced"):
            raise RuntimeError("the session is not silent")
        need({"cmd": "set_master_gain", "gainDb": -60, "muted": True}, "master")
        need({"cmd": "set_tempo", "bpm": 120}, "tempo")

        def export(name: str) -> tuple[list[float], int]:
            """The mix rendered offline to a file (the master at 0 dB for it: the session stays silent)."""
            need({"cmd": "set_master_gain", "gainDb": 0.0, "muted": False}, "master for the export")
            path = scratch / f"{name}.wav"
            need({"cmd": "export_mix", "path": str(path)}, f"export {name}")
            for _ in range(1_200):
                time.sleep(0.1)
                if not need({"cmd": "status"}, "status").get("exporting"):
                    break
            need({"cmd": "set_master_gain", "gainDb": -60.0, "muted": True}, "master")
            return read_wave(path)

        def factory(track: str, node: int) -> list[str]:
            listed = need({"cmd": "presets", "track": track, "nodeId": node}, "presets")
            return [item["name"] for item in listed.get("presets", []) if item.get("factory")]

        def node_of(answer: dict, kind: str) -> int:
            return [item["id"] for item in answer["nodes"] if item.get("type") == kind][-1]

        # ---- The audio effects, one at a time on an audio track ----
        loop = sorted((ROOT / "templates" / "audio").glob("*.wav"))[0]
        audio = need({"cmd": "add_audio_track", "file": str(loop)}, "audio track")["name"]
        for key in AUDIO_EFFECTS:
            node = node_of(need({"cmd": "insert_device", "track": audio, "key": key}, key), key)
            names = factory(audio, node)
            check(len(names) >= 5, f"{key}'s factory presets ({len(names)})")
            if key in ("tuner", "stereoscope"):
                through = need({"cmd": "effect_chain_check", "track": audio, "hz": 440.0, "amplitude": 0.3, "seconds": 0.8}, "chain")
                check(abs(through["outputDb"] - through["inputDb"]) < 0.01, f"the {key} leaves the sound as it is")
            elif key == "utilitaire":
                need({"cmd": "presets", "track": audio, "nodeId": node, "action": "load", "name": "Trim -6 dB"}, "Trim -6 dB")
                trimmed = need({"cmd": "effect_chain_check", "track": audio, "hz": 440.0, "amplitude": 0.3, "seconds": 0.8}, "chain")
                check(abs(trimmed["outputDb"] - (trimmed["inputDb"] - 6.0)) < 0.3,
                      f"the utility's Trim -6 dB lowers the sound by 6 dB ({trimmed['inputDb']:.1f} -> {trimmed['outputDb']:.1f} dB)")
            else:
                if names:
                    need({"cmd": "presets", "track": audio, "nodeId": node, "action": "load", "name": names[0]}, f"{key} preset")
                played = need({"cmd": "effect_chain_check", "track": audio, "hz": 440.0, "amplitude": 0.3, "seconds": 1.5}, "chain")
                bounded = math.isfinite(played["outputDb"]) and math.isfinite(played["peakDb"]) and played["peakDb"] < 9.0
                check(bounded and played["outputDb"] > -60.0,
                      f"{key} ({names[0] if names else 'default'}) plays a sine, bounded "
                      f"({played['inputDb']:.1f} -> {played['outputDb']:.1f} dB, peak {played['peakDb']:.1f} dB)")
            need({"cmd": "remove_device", "track": audio, "nodeId": node}, f"remove {key}")
        need({"cmd": "remove_track", "track": audio}, "remove the audio track")

        # ---- The MIDI effects: before the instrument, in order, with their presets ----
        keys = need({"cmd": "add_midi_track", "name": "Keys", "instrument": "opale-instrument"}, "keys track")["name"]
        for key in MIDI_EFFECTS:
            node = node_of(need({"cmd": "insert_device", "track": keys, "key": key}, key), key)
            names = factory(keys, node)
            check(len(names) >= 8, f"{key}'s factory presets ({len(names)})")
        tracks = need({"cmd": "status"}, "status").get("midiTracks", [])
        listed = next((entry.get("midiEffects", []) for entry in tracks if entry.get("name") == keys), [])
        check(listed == MIDI_EFFECTS, f"the notes go through the MIDI effects in the order added ({listed})")
        need({"cmd": "remove_track", "track": keys}, "remove the keys track")

        # Note repeat: a held note made into sixteenths; the arpeggiator: a held chord one note at a time.
        hits = need({"cmd": "add_midi_track", "name": "Hits", "instrument": "kick-instrument"}, "hits track")["name"]
        need({"cmd": "add_midi_clip", "track": hits, "name": "held", "startBeats": 0, "lengthBeats": 4}, "clip")
        need({"cmd": "add_notes", "track": hits, "clip": "held", "notes": [{"note": 36, "start": 0, "length": 4, "velocity": 0.9}]}, "notes")
        samples, rate = export("plain")
        plain = onsets(samples, rate, 2.0)
        need({"cmd": "insert_device", "track": hits, "key": "roulement"}, "roulement")
        samples, rate = export("rolled")
        rolled = onsets(samples, rate, 2.0)
        check(plain <= 1 and rolled >= 12, f"note repeat makes a held note into sixteenths ({plain} -> {rolled} hits in 2 s)")
        need({"cmd": "remove_track", "track": hits}, "remove the hits track")

        arp = need({"cmd": "add_midi_track", "name": "Arp", "instrument": "opale-instrument"}, "arp track")["name"]
        need({"cmd": "insert_device", "track": arp, "key": "arpege"}, "arpege")
        need({"cmd": "add_midi_clip", "track": arp, "name": "chord", "startBeats": 0, "lengthBeats": 4}, "clip")
        need({"cmd": "add_notes", "track": arp, "clip": "chord",
              "notes": [{"note": note, "start": 0, "length": 4, "velocity": 0.8} for note in (60, 64, 67)]}, "notes")
        samples, rate = export("arp")
        notes = onsets(samples, rate, 2.0)
        check(notes >= 10, f"the arpeggiator plays a held chord note by note ({notes} notes in 2 s)")
        need({"cmd": "remove_track", "track": arp}, "remove the arp track")

        # ---- The pad rack ----
        rack = need({"cmd": "add_midi_track", "name": "Rack", "instrument": "rack-instrument"}, "rack track")["name"]
        pads = need({"cmd": "rack", "track": rack}, "rack list")
        check(pads.get("rack") and [pad["note"] for pad in pads["pads"]] == [36, 38, 39, 42, 46],
              f"a new rack has its kit ({[(pad['note'], pad['name']) for pad in pads.get('pads', [])]})")
        hats = [pad for pad in pads.get("pads", []) if pad["note"] in (42, 46)]
        check(len(hats) == 2 and all(pad["choke"] == 1 for pad in hats), "its hi-hats cut each other")
        need({"cmd": "add_midi_clip", "track": rack, "name": "beat", "startBeats": 0, "lengthBeats": 4}, "clip")
        beat = [{"note": 36, "start": b, "length": 0.25, "velocity": 0.9} for b in (0, 1, 2, 3)]
        beat += [{"note": 38, "start": b, "length": 0.25, "velocity": 0.8} for b in (1, 3)]
        beat += [{"note": 42, "start": b / 2, "length": 0.125, "velocity": 0.6} for b in range(8)]
        need({"cmd": "add_notes", "track": rack, "clip": "beat", "notes": beat}, "beat")
        samples, rate = export("rack")
        level = rms(samples, rate, 0.0, 2.0)
        check(level > 0.02, f"the rack's pads sound (rms {level:.3f})")
        need({"cmd": "rack", "track": rack, "action": "mix", "note": 36, "mute": True}, "mute the kick")
        samples, rate = export("rack_muted")
        muted = rms(samples, rate, 0.0, 2.0)
        check(muted < level * 0.5, f"a muted pad is quiet ({level:.3f} -> {muted:.3f})")
        need({"cmd": "rack", "track": rack, "action": "mix", "note": 36, "mute": False}, "unmute the kick")
        project = scratch / "Rack.cydaw"
        need({"cmd": "save_project", "path": str(project)}, "save")
        need({"cmd": "new_project"}, "new")
        need({"cmd": "open_project", "path": str(project)}, "open")
        kept = []
        for _ in range(150):
            answer = request({"cmd": "rack", "track": "Rack"})
            if answer.get("ok") and answer["result"].get("pads"):
                kept = [pad["note"] for pad in answer["result"]["pads"]]
                break
            time.sleep(0.2)
        check(kept == [36, 38, 39, 42, 46], f"a saved project keeps the pads ({kept})")
        need({"cmd": "remove_track", "track": "Rack"}, "remove the rack")

        # ---- Silex and Nuée ----
        silex = need({"cmd": "add_midi_track", "name": "Silex", "instrument": "silex-instrument"}, "silex track")["name"]
        heard = need({"cmd": "synth", "track": silex, "note": 36, "seconds": 0.5, "tail": 1.5}, "silex synth")
        check(heard.get("model") == "silex" and heard["rmsDb"] > -45.0 and heard["endDb"] < -60.0,
              f"Silex plays its kick and ends ({heard['rmsDb']:.0f} dB, end {heard['endDb']:.0f} dB)")
        need({"cmd": "remove_track", "track": silex}, "remove Silex")
        nuee = need({"cmd": "add_midi_track", "name": "Nuee", "instrument": "nuee-instrument"}, "nuee track")["name"]
        need({"cmd": "load_sampler", "track": nuee, "file": str(loop).replace(os.sep, "/")}, "Nuée's sound")
        need({"cmd": "add_midi_clip", "track": nuee, "name": "held", "startBeats": 0, "lengthBeats": 4}, "clip")
        need({"cmd": "add_notes", "track": nuee, "clip": "held", "notes": [{"note": 60, "start": 0, "length": 3, "velocity": 0.8}]}, "notes")
        samples, rate = export("nuee")
        grains = rms(samples, rate, 0.2, 1.4)
        check(grains > 0.005 and all(math.isfinite(value) for value in samples),
              f"Nuée plays a loaded sound as grains (rms {grains:.3f})")
    finally:
        try:
            request({"cmd": "quit"})
            process.wait(timeout=30)
        except Exception:  # noqa: BLE001
            process.kill()
        shutil.rmtree(scratch, ignore_errors=True)
    print(f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
