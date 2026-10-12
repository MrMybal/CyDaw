"""Track formats and routing to the sound card, silently (master and buses muted).

    python tests/v2_routing_check.py --exe "<CyDAW 2.exe>" [--port 47828]

* A track made from a 6-channel file is 5.1; a mono file's is stereo, and
  becomes mono or quad on demand; an instrument track stays stereo.
* Every channel plays: the 5.1 track's meter shows its loudest speaker, the
  mono track's its one channel.
* An output bus of four channels on the card, that does not play the master;
  a track sent straight to it beside the master.
* All of it comes back with the project; a bus removed takes its outputs.
* The spectrometer is among the analysers, publishes its display's values
  and leaves the sound as it is.
* A virtual input with its effects: a track records it before them, the
  project finds it again by name, the software keeps it with its effects.

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
port = 47828


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


def write_wav(path: Path, channels: int, seconds: float = 3.0, rate: int = 48000) -> None:
    """Each channel a sine at its own level: channel k at 0.1 * (k + 1)."""
    frames = int(seconds * rate)
    data = bytearray()
    for frame in range(frames):
        sine = math.sin(2 * math.pi * 220 * frame / rate)
        for channel in range(channels):
            data += struct.pack("<h", int(0.1 * (channel + 1) * sine * 32767))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(channels)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(data))


def audio_tracks() -> dict:
    return {track["name"]: track for track in need({"cmd": "status"}, "status").get("audioTracks", [])}


def bus(name: str) -> dict:
    for entry in need({"cmd": "list_buses"}, "buses")["buses"]:
        if entry["name"] == name:
            return entry
    return {}


def peaks(seconds: float) -> dict:
    loudest: dict = {}
    end = time.time() + seconds
    while time.time() < end:
        for meter in need({"cmd": "status"}, "status")["meters"]:
            loudest[meter["name"]] = max(loudest.get(meter["name"], -120.0), meter["peakDb"])
        time.sleep(0.04)
    return loudest


def scenario(scratch: Path) -> None:
    silent()
    need({"cmd": "new_project"}, "new project")
    silent()
    surround_file = scratch / "surround.wav"
    mono_file = scratch / "mono.wav"
    write_wav(surround_file, 6)
    write_wav(mono_file, 1)
    need({"cmd": "add_audio_track", "file": str(surround_file)}, "5.1 track")
    need({"cmd": "add_audio_track", "file": str(mono_file)}, "mono track")
    need({"cmd": "add_midi_track", "name": "Keys"}, "instrument track")
    tracks = audio_tracks()
    check(tracks["surround"].get("channels") == 6 and tracks["surround"].get("format") == "5.1",
          "a 6-channel file makes a 5.1 track")
    check(tracks["mono"].get("channels") == 2, "a mono file's track is stereo")
    need({"cmd": "set_track", "track": "mono", "format": "quad"}, "quad")
    check(audio_tracks()["mono"].get("channels") == 4, "a track turns quad")
    need({"cmd": "set_track", "track": "mono", "format": "mono"}, "mono")
    check(audio_tracks()["mono"].get("channels") == 1, "a track turns mono")
    refused = request({"cmd": "set_track", "track": "Keys", "format": "quad"})
    check(not refused.get("ok"), "an instrument track stays stereo")

    # An output of four channels on the card, without the master, muted.
    need({"cmd": "add_bus", "direction": "output", "name": "Direct"}, "output bus")
    need({"cmd": "set_bus", "bus": "Direct", "format": "quad", "deviceChannels": [2, 3, 4, 5], "carriesMaster": False,
          "muted": True}, "bus set")
    direct = bus("Direct")
    check(direct.get("channels") == 4 and direct.get("deviceChannels") == [2, 3, 4, 5]
          and direct.get("carriesMaster") is False, "an output bus of four channels on the card's outputs 3-6")
    need({"cmd": "routing", "track": "surround",
          "outputs": [{"destination": "master"}, {"hardware": "Direct", "gainDb": -3}]}, "routing")
    outputs = audio_tracks()["surround"]["outputs"]
    check(len(outputs) == 2 and outputs[1].get("hardware") == "Direct" and abs(outputs[1]["gainDb"] + 3) < 0.01,
          "a track sent straight to the card beside the master")

    # Every channel plays (the meters read the tracks, nothing is heard).
    silent()
    need({"cmd": "transport", "action": "play", "position": 0.0}, "play")
    time.sleep(0.4)
    levels = peaks(1.2)
    need({"cmd": "transport", "action": "stop"}, "stop")
    # The 5.1 track's loudest speaker is its sixth channel (0.6: -4.4 dB).
    check(-6.0 < levels.get("surround", -120.0) < -3.0, f"the 5.1 track plays every channel ({levels.get('surround')})")
    check(-22.0 < levels.get("mono", -120.0) < -18.0, f"the mono track plays its channel ({levels.get('mono')})")

    # Saved and read back.
    project = scratch / "routing.cydaw"
    need({"cmd": "save_project", "path": str(project)}, "save")
    need({"cmd": "new_project"}, "new project")
    silent()
    need({"cmd": "open_project", "path": str(project)}, "open")
    silent()
    tracks = audio_tracks()
    check(tracks["surround"].get("channels") == 6 and tracks["mono"].get("channels") == 1,
          "the formats come back with the project")
    outputs = tracks["surround"]["outputs"]
    check(any(output.get("hardware") == "Direct" for output in outputs), "the output to the card comes back")
    direct = bus("Direct")
    check(direct.get("channels") == 4 and direct.get("deviceChannels") == [2, 3, 4, 5]
          and direct.get("carriesMaster") is False, "the bus comes back with its channels")

    # The card's channels are listed (and left as they are).
    card = need({"cmd": "device_channels"}, "device channels")
    check(isinstance(card.get("outputChannels"), list) and isinstance(card.get("inputChannels"), list),
          "the card's open channels are listed")

    need({"cmd": "remove_bus", "bus": "Direct"}, "remove bus")
    outputs = audio_tracks()["surround"]["outputs"]
    check(all("hardware" not in output for output in outputs), "a bus removed takes the outputs sent to it")
    need({"cmd": "set_view", "mode": 11}, "routing panel")
    check(request({"cmd": "show_dialog", "name": "soundcard"}).get("ok", False), "the card's inputs and outputs open in the audio settings")

    # The spectrometer: an analyser that leaves the sound alone.
    devices = {device["key"]: device for device in need({"cmd": "list_devices"}, "devices")["devices"]}
    spectrometer = devices.get("spectrometer-analyzer", {})
    check(spectrometer.get("category", "").split("/")[0].strip() in ("Analyseurs", "Analyzers"),
          "the spectrometer is among the analysers")
    need({"cmd": "insert_device", "track": "surround", "key": "spectrometer-analyzer"}, "spectrometer")
    keys = [parameter["key"] for parameter in need({"cmd": "list_parameters", "track": "surround"}, "parameters")["parameters"]]
    check(any(key.endswith("spectrometer.floor") for key in keys) and any(key.endswith("spectrometer.history") for key in keys),
          "the spectrometer publishes its display's values")
    silent()
    need({"cmd": "transport", "action": "play", "position": 0.0}, "play")
    time.sleep(0.4)
    levels = peaks(1.2)
    need({"cmd": "transport", "action": "stop"}, "stop")
    check(-6.0 < levels.get("surround", -120.0) < -3.0, f"the sound passes the spectrometer untouched ({levels.get('surround')})")

    # A virtual input of the software with its effects (a microphone's
    # limiter): a track records it before them; the project names it, the
    # software keeps it with its effects.
    need({"cmd": "add_bus", "direction": "input", "name": "Micro"}, "virtual input")
    need({"cmd": "set_bus", "bus": "Micro", "format": "mono", "deviceChannels": [0]}, "virtual input set")
    need({"cmd": "insert_device", "track": "bus:Micro", "key": "limiter"}, "its limiter")
    micro = bus("Micro")
    check(micro.get("channels") == 1 and any("LIMIT" in effect.upper() for effect in micro.get("effects", [])),
          "a virtual input has its effects")
    need({"cmd": "set_track", "track": "mono", "inputBus": "Micro", "inputAfterEffects": False}, "track input")
    track = audio_tracks()["mono"]
    check(track.get("inputBus") == micro.get("id") and track.get("inputAfterEffects") is False,
          "a track records the input before its effects")
    project = scratch / "inputs.cydaw"
    need({"cmd": "save_project", "path": str(project)}, "save")
    need({"cmd": "new_project"}, "new project")
    silent()
    micro = bus("Micro")
    check(any("LIMIT" in effect.upper() for effect in micro.get("effects", [])),
          "the virtual input and its effects stay with the software, not the project")
    need({"cmd": "open_project", "path": str(project)}, "open")
    silent()
    track = audio_tracks()["mono"]
    check(track.get("inputBus") == micro.get("id") and track.get("inputAfterEffects") is False,
          "the project finds its input again, taken before its effects")
    need({"cmd": "set_view", "mode": 3, "bus": "Micro"}, "the input's effects in the panels")
    need({"cmd": "remove_bus", "bus": "Micro"}, "remove the input")
    check(audio_tracks()["mono"].get("inputBus") != micro.get("id"), "a removed input leaves its tracks on another")


def launch(exe: str, virtual_io: Path | None = None) -> subprocess.Popen:
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1")
    if virtual_io is not None:
        environment["CYDAW_VIRTUAL_IO_FILE"] = str(virtual_io)
    process = subprocess.Popen([str(Path(exe).resolve()), f"--control-port={port}", "--silent",
                                "--window-size=1500x900"], env=environment, stdin=subprocess.DEVNULL,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(200):
        try:
            request({"cmd": "ping"})
            break
        except OSError:
            time.sleep(0.2)
    return process


def close(process: subprocess.Popen) -> None:
    try:
        request({"cmd": "quit"})
    except OSError:
        pass
    try:
        process.wait(timeout=20)
    except subprocess.TimeoutExpired:
        process.kill()


def persistence(exe: str, scratch: Path) -> None:
    """The virtual inputs and their effects come back when the software
    starts again (from its own file, here a test's)."""
    virtual_io = scratch / "VirtualIO.json"
    process = launch(exe, virtual_io)
    try:
        silent()
        need({"cmd": "add_bus", "direction": "input", "name": "Guitare"}, "virtual input")
        need({"cmd": "insert_device", "track": "bus:Guitare", "key": "comp-pro"}, "its compressor")
    finally:
        close(process)
    check(virtual_io.is_file(), "the software writes its virtual inputs and outputs")
    process = launch(exe, virtual_io)
    try:
        silent()
        guitar = bus("Guitare")
        check(bool(guitar) and any("COMP" in effect.upper() for effect in guitar.get("effects", [])),
              "the virtual input and its effects come back when the software starts again")
    finally:
        close(process)


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=47828)
    arguments = parser.parse_args()
    port = arguments.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-routing-"))
    try:
        process = launch(arguments.exe)
        try:
            scenario(scratch)
        finally:
            close(process)
        persistence(arguments.exe, scratch)
    except Exception as error:  # noqa: BLE001 - reported as a failure
        failures.append(str(error))
        print("FAIL", error, flush=True)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print("failures:", len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
