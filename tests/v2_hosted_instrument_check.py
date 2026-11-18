"""Play a native-note CLAP instrument and verify independent mixer returns.

The DAW is launched in its usual isolated silent-test session. A VST3
instrument can also be supplied for the same keyboard and clip checks.
"""
import argparse
import json
import os
import time
import wave
from pathlib import Path
from tempfile import TemporaryDirectory

import v2_routing_check as session


def command(cmd, **fields):
    return session.need({"cmd": cmd, **fields}, cmd)


def levels(seconds):
    return session.peaks(seconds)


def arrangement(folder):
    command("new_project")
    command("set_tempo", bpm=120)
    changes = [dict(beat=4, bpm=60, numerator=7, denominator=8)]
    point = command("tempo_map", points=changes, beat=7.5)
    assert point == dict(beat=7.5, seconds=5.5, bar=3, numerator=7, denominator=8, bpm=60), point
    point = command("tempo_map", beat=4, anchorSeconds=2.4)
    assert abs(point["seconds"] - 2.4) < 1e-7, point
    assert abs(command("tempo_map", seconds=3.4)["beat"] - 5) < 1e-7
    source = folder / "tone.wav"
    session.write_wav(source, channels=2, seconds=1.5)
    command("add_audio_track", file=str(source))
    original_bytes = source.read_bytes()
    command("add_midi_track", name="Arrange synth", instrument="ambre-instrument")
    command("add_notes", track="Arrange synth", notes=[dict(note=60, start=0.5, length=1, velocity=0.8)])
    stems = folder / "stems"
    stems.mkdir()
    before = command("transport", action="rewind")
    refused = session.request({"cmd": "render_arrangement", "path": str(source), "from": 0.2, "to": 1.2})
    assert not refused["ok"] and source.read_bytes() == original_bytes, refused
    command("render_arrangement", path=str(stems), stems=True, **{"from": 0.2, "to": 1.2})
    after = command("transport")
    assert before == after, (before, after)
    files = sorted(stems.glob("*.wav"))
    assert len(files) >= 2, files
    for prefix in ("tone", "Arrange synth"):
        path = next(path for path in files if path.stem.endswith(prefix))
        with wave.open(str(path), "rb") as wav:
            assert (wav.getframerate(), wav.getnframes(), wav.getsampwidth()) == (48000, 48000, 3)
            assert any(wav.readframes(48000)), path
    frozen = command("freeze_midi", track="Arrange synth", tail=0.2)
    frozen_name = next(track["name"] for track in frozen["audioTracks"] if "Arrange synth - Freeze" in track["name"])
    command("transport", action="play", position=0)
    frozen_levels = levels(0.65)
    command("transport", action="stop")
    assert frozen_levels["Arrange synth"] <= -100 and frozen_levels[frozen_name] > -40, frozen_levels
    command("return_to_midi", track=frozen_name)
    command("transport", action="play", position=0)
    assert levels(0.6)["Arrange synth"] > -40
    command("transport", action="stop")
    copied = folder / "collected"
    copied.mkdir()
    command("collect_media", path=str(copied))
    assert list(copied.glob("*.cydaw"))
    assert any(p.read_bytes() == source.read_bytes() for p in copied.rglob("*.wav"))
    command("new_project")
    original = source.with_name("original-moved.wav")
    source.rename(original)
    command("open_project", path=str(next(copied.glob("*.cydaw"))))
    deadline = time.monotonic() + 15
    while command("status").get("opening"):
        assert time.monotonic() < deadline
        time.sleep(0.05)
    point = command("tempo_map", beat=7.5)
    assert abs(point["seconds"] - 5.9) < 1e-7 and point["numerator"] == 7, point
    command("transport", action="play", position=0)
    assert levels(0.5)["tone"] > -24
    command("transport", action="stop")
    print("tempo map, aligned 48 000-frame stems, reversible freeze, and collected project reopened", flush=True)


def scenario(plugin, project, multiple):
    name = "External keys"
    command("new_project")
    command("add_midi_track", name=name)
    loaded = command("add_plugin", track=name, file=str(plugin.resolve()))
    assert loaded["ports"][0]["main"], loaded
    if multiple:
        assert len(loaded["ports"]) == 2, loaded
        command("instrument_returns", track=name)
        outputs = command("routing", track=name)["outputs"]
        returned = next(route["destination"] for route in outputs if route.get("instrumentBus") == 2)
        command("instrument_returns", track=name)
        assert command("routing", track=name)["outputs"] == outputs
    else:
        returned = None

    assert command("status")["speakersSilenced"]
    command("play_midi_note", note=60, on=True)
    loud = levels(0.45)
    command("play_midi_note", note=60, on=False)
    assert loud[name] > -40, loud
    if returned:
        assert 10 < loud[name] - loud[returned] < 14, loud
    print(f"{plugin.suffix} keyboard levels {loud}", flush=True)

    command("add_notes", track=name, notes=[dict(note=62, start=0.25, length=3.75, velocity=0.9)])
    command("transport", action="play", position=0)
    before = levels(0.4)
    command("transport", action="stop")
    assert before[name] > -40, before
    command("save_project", path=str(project))
    command("new_project")
    opened = command("open_project", path=str(project))
    deadline = time.monotonic() + 15
    while command("status").get("opening"):
        assert time.monotonic() < deadline
        time.sleep(0.05)
    reopened = command("instrument_ports", track=name)
    assert reopened.get("ports") == loaded["ports"], (reopened, opened.get("warnings"))
    command("transport", action="play", position=0)
    loud = levels(0.7)
    command("transport", action="stop")
    assert loud[name] > -40, loud
    if returned:
        assert 10 < loud[name] - loud[returned] < 14, loud
        command("set_track", track=returned, gainDb=-12)
        time.sleep(0.4)
        command("transport", action="play", position=0)
        lower = levels(0.6)
        command("transport", action="stop")
        assert 22 < lower[name] - lower[returned] < 26, lower
        print(f"{plugin.suffix} auxiliary fader -12 dB {lower}", flush=True)
        stems = project.parent / "plugin-stems"
        stems.mkdir()
        command("render_arrangement", path=str(stems), stems=True, **{"from": 0, "to": 0.8})
        for track in (name, returned.rsplit(" ", 1)[-1]):
            path = next(p for p in stems.glob("*.wav") if p.stem.endswith(track))
            with wave.open(str(path), "rb") as wav:
                assert wav.getnframes() == 38400 and any(wav.readframes(38400)), path
    print(f"{plugin.suffix} clip levels after reopening {loud}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--clap", required=True, type=Path)
    parser.add_argument("--vst3", type=Path)
    parser.add_argument("--arrangement", action="store_true")
    parser.add_argument("--port", type=int, default=47947)
    args = parser.parse_args()
    session.port = args.port
    with TemporaryDirectory(prefix="cydaw-hosted-instrument-") as temp:
        folder = Path(temp)
        locations = folder / "locations.json"
        locations.write_text(json.dumps(dict(format="CyDAW Locations", version=1, folders={
            key: str(folder / key) for key in ("cache", "devices", "pluginExports", "presets", "mixes", "foundry", "banks")
        })), encoding="utf-8")
        os.environ.update(APPDATA=str(folder / "Roaming"), LOCALAPPDATA=str(folder / "Local"),
                          CYDAW_CONTROL_TOKEN="", CYDAW_LOCATIONS_FILE=str(locations),
                          CYDAW_DJ_COLLECTION_FILE=str(folder / "dj.json"))
        process = session.launch(args.exe, folder / "virtual-io.json")
        try:
            scenario(args.clap, folder / "clap.cydaw", True)
            if args.vst3:
                scenario(args.vst3, folder / "vst3.cydaw", False)
            if args.arrangement:
                arrangement(folder)
        finally:
            session.close(process)


if __name__ == "__main__":
    main()
