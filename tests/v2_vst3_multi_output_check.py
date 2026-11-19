"""Validate three VST3 output buses in the silent CyDaw test session.

On Windows, build the fixture against a local Steinberg SDK checkout from
the repo root, then run the test with Python 3.12 or later:

    cmake -S tests/fixtures/vst3-multiout -B build/vst3-multiout -DVST3_SDK_DIR=<sdk>
    cmake --build build/vst3-multiout --config Release --target CyDawMultiOutput
    python tests/v2_vst3_multi_output_check.py --exe <CyDaw.exe> --vst3 <fixture.vst3> --report <results.json>

The fixture emits a different frequency on each bus so the test can check
for crosstalk, independent faders and saved routing with no external plugin.
"""
import argparse
import json
import math
import os
from pathlib import Path
import tempfile
import time
import wave

import v2_routing_check as session


def command(cmd, **fields):
    return session.need({"cmd": cmd, **fields}, cmd)


def capture(folder, sources):
    folder.mkdir()
    command("render_arrangement", path=str(folder), stems=True, **{"from": 0.20, "to": 0.65})
    result = {}
    for name, frequency, channels in sources:
        path = next(p for p in folder.glob("*.wav") if p.stem.endswith(name))
        with wave.open(str(path), "rb") as stream:
            assert (stream.getnchannels(), stream.getframerate(), stream.getsampwidth(), stream.getnframes()) == (
                channels, 48000, 3, 21600
            ), (path, stream.getparams())
            stream.setpos(4096)
            frames = stream.readframes(4096)
        frame_bytes = 3 * channels
        samples = [int.from_bytes(frames[i:i + 3], "little", signed=True) / 8388608
                   for i in range(0, len(frames), frame_bytes)]
        amplitudes = {}
        for _, freq, _ in sources:
            phase_step = 2 * math.pi * freq / 48000
            cosine = sum(s * math.cos(phase_step * i) for i, s in enumerate(samples))
            sine = sum(s * math.sin(phase_step * i) for i, s in enumerate(samples))
            magnitude = math.hypot(cosine, sine) * 2 / len(samples)
            amplitudes[str(freq)] = round(20 * math.log10(max(1e-12, magnitude)), 3)
        signal = amplitudes[str(frequency)]
        assert signal > -46 and all(value < -95 for key, value in amplitudes.items() if key != str(frequency)), (
            name, amplitudes
        )
        result[name] = {"channels": channels, "dbfs_by_hz": amplitudes, "signal_dbfs": signal}
    return result


def run(args, folder):
    name = "VST3 Test Keys"
    command("new_project")
    command("add_midi_track", name=name)
    loaded = command("add_plugin", track=name, file=str(args.vst3.resolve()))
    assert loaded["ports"] == [
        {"name": "Main", "channels": 2, "main": True},
        {"name": "Aux Stereo", "channels": 2, "main": False},
        {"name": "Aux Mono", "channels": 1, "main": False},
    ], loaded
    command("instrument_returns", track=name)
    routing = command("routing", track=name)["outputs"]
    returned = {route.get("instrumentBus", 0): route["destination"] for route in routing}
    assert set(returned) == {0, 2, 3} and returned[0] == "master", routing
    sources = [(name, 375, 2), (returned[2], 750, 2), (returned[3], 1500, 1)]
    command("instrument_returns", track=name)
    assert command("routing", track=name)["outputs"] == routing

    assert command("status")["speakersSilenced"]
    mixers = {track["name"]: track["channels"] for track in command("status")["audioTracks"]}
    assert [mixers[returned[2]], mixers[returned[3]]] == [2, 1], mixers
    command("play_midi_note", note=60, on=True)
    keyboard_all = session.peaks(0.5)
    command("play_midi_note", note=60, on=False)
    keyboard = {track: keyboard_all[track] for track, _, _ in sources}
    assert all(value > -32 for value in keyboard.values()), keyboard

    command("add_notes", track=name, notes=[{"note": 62, "start": 0.25, "length": 3.75, "velocity": 0.9}])
    command("transport", action="play", position=0)
    initial_meters = session.peaks(0.6)
    command("transport", action="stop")
    initial_meters = {track: initial_meters[track] for track, _, _ in sources}
    assert all(value > -32 for value in initial_meters.values()), initial_meters
    baseline = capture(folder / "baseline", sources)
    command("set_track", track=returned[2], gainDb=-12)
    stereo = capture(folder / "stereo-minus-12", sources)
    command("set_track", track=returned[3], gainDb=-6)
    mono = capture(folder / "mono-minus-6", sources)

    def changes(before, after, expected):
        actual = {track: round(after[track]["signal_dbfs"] - values["signal_dbfs"], 3)
                  for track, values in before.items()}
        assert all(abs(actual[track] - change) < .15 for track, change in expected.items()), (actual, expected)
        return actual

    stereo_change = changes(baseline, stereo, {name: 0, returned[2]: -12, returned[3]: 0})
    mono_change = changes(stereo, mono, {name: 0, returned[2]: 0, returned[3]: -6})

    project = folder / "three-outputs.cydaw"
    command("save_project", path=str(project))
    command("new_project")
    reopened = command("open_project", path=str(project))
    deadline = time.monotonic() + 15
    while command("status").get("opening"):
        assert time.monotonic() < deadline
        time.sleep(.05)
    assert command("instrument_ports", track=name)["ports"] == loaded["ports"], reopened.get("warnings")
    assert command("routing", track=name)["outputs"] == routing
    after_load = capture(folder / "reopened", sources)
    changes(mono, after_load, {track: 0 for track, _, _ in sources})
    command("transport", action="play", position=0)
    reopened_meters = session.peaks(0.6)
    command("transport", action="stop")
    reopened_meters = {track: reopened_meters[track] for track, _, _ in sources}
    assert all(value > -38 for value in reopened_meters.values()), reopened_meters

    return {
        "daw": str(args.exe.resolve()),
        "instrument": str(args.vst3.resolve()),
        "ports": loaded["ports"],
        "routing": routing,
        "keyboard_mixer_peaks_dbfs": keyboard,
        "clip_mixer_peaks_dbfs": initial_meters,
        "clip_after_reopening_mixer_peaks_dbfs": reopened_meters,
        "offline_baseline": baseline,
        "offline_stereo_fader_minus_12_changes_db": stereo_change,
        "offline_mono_fader_minus_6_changes_db": mono_change,
        "offline_after_reopen": after_load,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--vst3", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--port", type=int, default=47956)
    args = parser.parse_args()
    session.port = args.port
    with tempfile.TemporaryDirectory(prefix="cydaw-vst3-multiout-") as root:
        folder = Path(root)
        locations = folder / "locations.json"
        locations.write_text(json.dumps({
            "format": "CyDAW Locations",
            "version": 1,
            "folders": {key: str(folder / key) for key in (
                "cache", "devices", "pluginExports", "presets", "mixes", "foundry", "banks"
            )},
        }), encoding="utf-8")
        os.environ.update(APPDATA=str(folder / "Roaming"), LOCALAPPDATA=str(folder / "Local"),
                          CYDAW_CONTROL_TOKEN="", CYDAW_LOCATIONS_FILE=str(locations),
                          CYDAW_DJ_COLLECTION_FILE=str(folder / "dj.json"))
        process = session.launch(str(args.exe), folder / "virtual-io.json")
        try:
            result = run(args, folder)
        finally:
            session.close(process)
    report = json.dumps(result, indent=2, ensure_ascii=True)
    args.report.write_text(report + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
