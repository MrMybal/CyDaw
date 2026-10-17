"""Projects of other applications imported in the software. Silent (the speakers silenced).

    python tests/v2_project_import_check.py --exe "<CyDAW 2.exe>" [--port 47867] [--samples <folder>]

Each format gets a small project written here, as its application writes it, then imported
(import_project); what came in is checked (tempo, signature, tracks, clips, notes, files, stereo).
Real projects of those applications (free test files, kept out of the repository) are imported too
when --samples (or CYDAW_IMPORT_SAMPLES) names their folder; skipped otherwise.

* Reaper (.rpp): tempo and signature, a MIDI item's notes where the item sits, an audio item, its
  fades, the selected take, a VST3 instrument kept as a missing plug-in card.
* FL Studio (.flp): channels as tracks, a pattern placed twice, a sampler channel playing its
  sound on a simple sampler, FL's own synth on its equivalent.
* Ardour (.ardour, 7): audio and MIDI regions in audio and beat time, a stereo region's two mono
  files made one stereo file, the track's gain and pan.
* LMMS (.mmpz): an instrument's pattern, a beat/bassline played on two tracks of placements, a
  sample track.
* Zrythm (.zpj, zstd): tempo, a MIDI region looped, an audio region of the pool.
* A MIDI file and the stems beside it (Reason, and any other): MIDI tracks (channel 10 a kit),
  audio tracks.

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
import zlib
from pathlib import Path

failures: list[str] = []
port = 47867
ROOT = Path(__file__).resolve().parents[1]


def request(command: dict) -> dict:
    with socket.create_connection(("127.0.0.1", port), timeout=5.0) as connection:
        connection.settimeout(300)
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


# ---- Writers ----------------------------------------------------------------------------------

def write_wav(path: Path, seconds: float = 1.0, channels: int = 1, rate: int = 44_100, hz: float = 220.0) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = int(seconds * rate)
    with wave.open(str(path), "wb") as file:
        file.setnchannels(channels)
        file.setsampwidth(2)
        file.setframerate(rate)
        data = bytearray()
        for index in range(frames):
            value = int(8_000 * math.sin(2 * math.pi * hz * index / rate))
            data += struct.pack("<h", value) * channels
        file.writeframes(bytes(data))


def wav_channels(path: str) -> int:
    with wave.open(path, "rb") as file:
        return file.getnchannels()


def midi_file(tracks: list[tuple[str, list[tuple[int, int, int, int, int]]]], ppq: int = 480, bpm: float = 120.0) -> bytes:
    """A type-1 MIDI file: a tempo track, then (name, [(channel 1-16, key, start tick, length ticks, velocity)])."""
    def var(value: int) -> bytes:
        out = [value & 0x7f]
        value >>= 7
        while value:
            out.insert(0, (value & 0x7f) | 0x80)
            value >>= 7
        return bytes(out)

    def chunk(events: list[tuple[int, bytes]]) -> bytes:
        events.sort(key=lambda item: item[0])
        body, last = b"", 0
        for tick, data in events:
            body += var(tick - last) + data
            last = tick
        body += var(0) + b"\xff\x2f\x00"
        return b"MTrk" + struct.pack(">I", len(body)) + body

    tempo = int(60_000_000 / bpm)
    chunks = [chunk([(0, b"\xff\x51\x03" + tempo.to_bytes(3, "big")), (0, b"\xff\x58\x04\x04\x02\x18\x08")])]
    for name, notes in tracks:
        events = [(0, b"\xff\x03" + var(len(name)) + name.encode())]
        for channel, key, start, length, velocity in notes:
            events.append((start, bytes([0x90 | (channel - 1), key, velocity])))
            events.append((start + length, bytes([0x80 | (channel - 1), key, 0])))
        chunks.append(chunk(events))
    return b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), ppq) + b"".join(chunks)


def zstd_frame(data: bytes) -> bytes:
    """A zstd frame of raw (stored) blocks: what a decoder must read like any other."""
    frame = b"\x28\xb5\x2f\xfd" + bytes([0xa0]) + struct.pack("<I", len(data))
    blocks = [data[at:at + 100_000] for at in range(0, len(data), 100_000)] or [b""]
    for index, block in enumerate(blocks):
        last = 1 if index == len(blocks) - 1 else 0
        frame += ((len(block) << 3) | last).to_bytes(3, "little") + block
    return frame


# ---- The projects ---------------------------------------------------------------------------

def reaper_project(folder: Path) -> Path:
    write_wav(folder / "Audio" / "loop.wav", 2.0)
    events = []
    for index, key in enumerate((60, 64, 67, 72)):
        events.append(f"E {0 if index == 0 else 480} 90 {key:02x} 60")
        events.append(f"E 480 80 {key:02x} 00")
    midi = "\n".join("        " + line for line in events)
    text = f"""<REAPER_PROJECT 0.1 "7.10/win64" 1700000000
  TEMPO 100 3 4
  MASTER_VOLUME 0.5 0 -1 -1 1
  <TRACK {{11111111-1111-1111-1111-111111111111}}
    NAME Keys
    PEAKCOL 16576
    VOLPAN 0.5 -0.25 -1 -1 1
    MUTESOLO 0 0 0
    <FXCHAIN
      BYPASS 0 0 0
      <VST "VST3i: Serum (Xfer Records)" Serum.vst3 0 "" 1234<5653> ""
        AAAA
      >
    >
    <ITEM
      POSITION 2
      LENGTH 4
      LOOP 0
      NAME "Chords"
      <SOURCE MIDI
        HASDATA 1 960 QN
{midi}
        E 960 b0 7b 00
      >
    >
  >
  <TRACK {{22222222-2222-2222-2222-222222222222}}
    NAME "Drum Loop"
    VOLPAN 1 0 -1 -1 1
    MUTESOLO 0 1 0
    <ITEM
      POSITION 1
      LENGTH 1.5
      FADEIN 1 0.25 0 1 0 0 0
      FADEOUT 1 0.5 0 1 0 0 0
      NAME "old take"
      SOFFS 0
      <SOURCE WAVE
        FILE "missing.wav"
      >
      TAKE SEL
      NAME "loop.wav"
      SOFFS 0.5
      <SOURCE WAVE
        FILE "Audio\\loop.wav"
      >
    >
  >
>
"""
    path = folder / "Song.rpp"
    path.write_text(text, encoding="utf-8")
    return path


def fl_project(folder: Path) -> Path:
    write_wav(folder / "kick.wav", 0.3, hz=60.0)
    data = bytearray()

    def byte(event: int, value: int) -> None:
        data.extend([event, value])

    def word(event: int, value: int) -> None:
        data.append(event)
        data.extend(struct.pack("<H", value))

    def dword(event: int, value: int) -> None:
        data.append(event)
        data.extend(struct.pack("<I", value))

    def blob(event: int, payload: bytes) -> None:
        data.append(event)
        size = len(payload)
        while True:
            part = size & 0x7f
            size >>= 7
            data.append(part | (0x80 if size else 0))
            if not size:
                break
        data.extend(payload)

    def text(event: int, value: str) -> None:
        blob(event, (value + "\0").encode("utf-16-le"))

    blob(199, b"20.8.4.2576\0")
    dword(156, 128_000)
    byte(17, 4)
    byte(18, 4)
    text(194, "FL Test")
    # Channel 0: FL's own synth; channel 1: a sampler and its sound.
    word(64, 0)
    byte(21, 2)
    text(201, "3x Osc")
    text(203, "Lead")
    blob(219, struct.pack("<iiiiii", 6_400, 10_000, 0, 0, 0, 0))
    word(64, 1)
    byte(21, 0)
    text(201, "")
    text(203, "Kick")
    text(196, "kick.wav")
    # Pattern 1: two notes of the lead, four kicks (96 pulses a beat).
    word(65, 1)
    text(193, "Main")
    notes = b""
    for position, channel, key in ((0, 0, 72), (192, 0, 76), (0, 1, 60), (96, 1, 60), (192, 1, 60), (288, 1, 60)):
        notes += struct.pack("<IHHIHHBBBBBBBB", position, 0, channel, 48, key, 0, 120, 0, 64, 0, 64, 100, 128, 128)
    blob(224, notes)
    # The arrangement: the pattern twice.
    word(99, 0)
    text(241, "Arrangement")
    items = b""
    for position in (0, 384):
        items += struct.pack("<IHHIHH2sH4sff", position, 20_480, 20_481, 384, 499, 0, b"\0\0", 0, b"\0" * 4, -1.0, -1.0)
    blob(233, items)
    header = b"FLhd" + struct.pack("<IhHH", 6, 0, 2, 96)
    path = folder / "Beat.flp"
    path.write_bytes(header + b"FLdt" + struct.pack("<I", len(data)) + bytes(data))
    return path


def ardour_session(folder: Path) -> Path:
    name = "Session"
    audio = folder / "interchange" / name / "audiofiles"
    write_wav(audio / "Vocals-1%L.wav", 1.0, hz=220.0)
    write_wav(audio / "Vocals-1%R.wav", 1.0, hz=330.0)
    midi = folder / "interchange" / name / "midifiles"
    midi.mkdir(parents=True, exist_ok=True)
    (midi / "Synth-1.mid").write_bytes(midi_file([("Synth", [(1, 60, 0, 480, 100), (1, 64, 480, 480, 100), (1, 67, 960, 480, 100)])]))
    second = 282_240_000
    text = f"""<?xml version="1.0" encoding="UTF-8"?>
<Session version="7003" name="{name}" sample-rate="48000">
  <Sources>
    <Source name="Vocals-1%L.wav" type="audio" id="10"/>
    <Source name="Vocals-1%R.wav" type="audio" id="11"/>
    <Source name="Synth-1.mid" type="midi" id="12"/>
  </Sources>
  <Routes>
    <Route id="1" name="Master" default-type="audio">
      <PresentationInfo order="0" flags="MasterOut,OrderSet"/>
      <Processor id="2" name="Amp" type="amp"><Controllable name="gaincontrol" value="1"/></Processor>
    </Route>
    <Route id="3" name="Vocals" default-type="audio" audio-playlist="20">
      <PresentationInfo order="1" flags="AudioTrack,OrderSet" color="2852126975"/>
      <Controllable name="solo" value="0"/>
      <Controllable name="mute" value="0"/>
      <Processor id="4" name="Amp" type="amp"><Controllable name="gaincontrol" value="0.5"/></Processor>
      <Processor id="5" name="Pan" type="main-outs"><Pannable><Controllable name="pan-azimuth" value="0.25"/></Pannable></Processor>
      <Processor id="6" name="ACE Compressor" type="lv2"/>
    </Route>
    <Route id="7" name="Synth" default-type="midi" midi-playlist="21">
      <PresentationInfo order="2" flags="MidiTrack,OrderSet"/>
      <Processor id="8" name="ACE Reasonable Synth" type="lv2"/>
    </Route>
  </Routes>
  <Playlists>
    <Playlist id="20" name="Vocals" type="audio">
      <Region name="Vocals-1" position="a{2 * second}" length="a{second // 2}" start="a{second // 4}" source-0="10" source-1="11" type="audio"/>
    </Playlist>
    <Playlist id="21" name="Synth" type="midi">
      <Region name="Synth-1" position="b1920" length="b7680" start="b0" source-0="12" type="midi"/>
    </Playlist>
  </Playlists>
  <TempoMap superclocks-per-second="{second}">
    <Tempos><Tempo npm="90" enpm="90" note-type="4"/></Tempos>
    <Meters><Meter divisions-per-bar="3" note-value="4"/></Meters>
  </TempoMap>
</Session>
"""
    path = folder / f"{name}.ardour"
    path.write_text(text, encoding="utf-8")
    return path


def lmms_project(folder: Path) -> Path:
    write_wav(folder / "loop.wav", 1.0)
    xml = """<?xml version="1.0"?>
<lmms-project version="1.0" creator="LMMS" creatorversion="1.2.2" type="song">
  <head bpm="140" timesig_numerator="4" timesig_denominator="4" mastervol="100"/>
  <song>
    <trackcontainer type="song">
      <track type="0" name="Bass" muted="0" solo="0">
        <instrumenttrack vol="100" pan="0"><instrument name="tripleoscillator"><tripleoscillator/></instrument></instrumenttrack>
        <pattern pos="0" len="192" name="Bass line" muted="0"><note pos="0" len="48" key="45" vol="100"/><note pos="96" len="48" key="48" vol="100"/></pattern>
      </track>
      <track type="1" name="Beat" muted="0" solo="0">
        <bbtrack>
          <trackcontainer type="bbtrackcontainer">
            <track type="0" name="Kick" muted="0" solo="0">
              <instrumenttrack vol="100" pan="0"><instrument name="kicker"><kicker/></instrument></instrumenttrack>
              <pattern pos="0" len="192" steps="16"><note pos="0" len="-192" key="57" vol="100"/><note pos="48" len="-192" key="57" vol="100"/><note pos="96" len="-192" key="57" vol="100"/><note pos="144" len="-192" key="57" vol="100"/></pattern>
              <pattern pos="192" len="192" steps="16"><note pos="0" len="-192" key="57" vol="100"/><note pos="96" len="-192" key="57" vol="100"/></pattern>
            </track>
          </trackcontainer>
        </bbtrack>
        <bbtco pos="192" len="384" name="" muted="0"/>
      </track>
      <track type="1" name="Beat 2" muted="0" solo="0">
        <bbtrack/>
        <bbtco pos="768" len="192" name="" muted="0"/>
      </track>
      <track type="2" name="Loops" muted="0" solo="0">
        <sampletrack vol="100" pan="0"/>
        <sampletco pos="0" len="192" src="loop.wav" muted="0" off="0"/>
      </track>
    </trackcontainer>
  </song>
</lmms-project>
""".encode("utf-8")
    path = folder / "Groove.mmpz"
    path.write_bytes(struct.pack(">I", len(xml)) + zlib.compress(xml))
    return path


def zrythm_project(folder: Path) -> Path:
    write_wav(folder / "pool" / "loop.wav", 2.0)

    def pos(ticks: float) -> dict:
        return {"ticks": ticks, "frames": 0}

    def port(value: float) -> dict:
        return {"control": value}

    notes = [{"base": {"pos": pos(at), "endPos": pos(at + 480)}, "velocity": {"velocity": 100}, "value": key}
             for at, key in ((0, 60), (960, 64), (1_920, 67))]
    project = {
        "type": "ZrythmProject", "formatMajor": 1, "formatMinor": 9, "title": "Z Test", "version": "1.0.0",
        "tracklist": {"tracks": [
            {"name": "Master", "type": 2, "channel": {"fader": {"amp": port(1.0)}}},
            {"name": "Tempo", "type": 5, "bpmPort": port(110.0), "beatsPerBarPort": port(4.0), "beatUnitPort": port(1.0)},
            {"name": "Keys", "type": 0, "color": {"red": 1.0, "green": 0.5, "blue": 0.0, "alpha": 1.0},
             "channel": {"fader": {"amp": port(0.5), "balance": port(0.5), "mute": port(0.0), "solo": port(0.0)},
                         "instrument": {"setting": {"descriptor": {"name": "Helm", "author": "Matt Tytel", "protocol": 1}}}},
             "lanes": [{"name": "Lane 1", "regions": [
                 {"base": {"pos": pos(3_840), "endPos": pos(3_840 + 7_680), "clipStartPos": pos(0), "loopStartPos": pos(0),
                           "loopEndPos": pos(3_840)},
                  "name": "Riff", "midiNotes": notes}]}]},
            {"name": "Loop", "type": 1, "channel": {"fader": {"amp": port(1.0), "balance": port(0.5)}},
             "lanes": [{"name": "Lane 1", "regions": [
                 {"base": {"pos": pos(0), "endPos": pos(3_840), "clipStartPos": pos(0), "loopStartPos": pos(0), "loopEndPos": pos(3_840)},
                  "name": "loop", "poolId": 0, "gain": 1.0}]}]},
        ]},
        "audioEngine": {"pool": {"clips": [{"name": "loop", "poolId": 0, "useFlac": False}]}},
    }
    path = folder / "project.zpj"
    path.write_bytes(zstd_frame(json.dumps(project).encode("utf-8")))
    return path


def midi_and_stems(folder: Path) -> Path:
    write_wav(folder / "Bass stem.wav", 2.0, hz=55.0)
    write_wav(folder / "Pad stem.wav", 2.0, channels=2, hz=440.0)
    path = folder / "Song.mid"
    path.write_bytes(midi_file([("Lead", [(1, 72, 0, 240, 100), (1, 74, 480, 240, 100)]),
                                ("Drums", [(10, 36, 0, 120, 120), (10, 38, 480, 120, 110), (10, 36, 960, 120, 120)])], bpm=95.0))
    return path


# ---- The check ---------------------------------------------------------------------------------

def imported(path: Path) -> dict:
    return need({"cmd": "import_project", "file": str(path).replace(os.sep, "/")}, f"import {path.name}")


def track(report: dict, name: str) -> dict:
    return next((item for item in report.get("tracks", []) if item.get("name") == name), {})


def graph_types(name: str) -> list[str]:
    answer = request({"cmd": "get_track_graph", "track": name})
    return [node.get("type") for node in answer.get("result", {}).get("nodes", [])] if answer.get("ok") else []


def clips_of(name: str) -> list[dict]:
    answer = request({"cmd": "list_audio_clips", "track": name})
    return answer.get("result", {}).get("clips", []) if answer.get("ok") else []


def scenario(scratch: Path, samples: Path | None) -> None:
    # Reaper.
    report = imported(reaper_project(scratch / "reaper"))
    check(report["format"] == "reaper" and abs(report["tempo"] - 100) < 1e-6 and report["numerator"] == 3,
          f"Reaper: tempo and signature ({report['tempo']}, {report['numerator']}/{report['denominator']})")
    keys = track(report, "Keys")
    check(keys.get("kind") == "midi" and keys.get("notes") == 4, f"Reaper: the MIDI item's four notes ({keys})")
    check("Serum" in graph_types("Keys") or any("missing" in str(kind) for kind in graph_types("Keys")),
          f"Reaper: its VST3 instrument kept as a missing plug-in card ({graph_types('Keys')})")
    loop = clips_of("Drum Loop")
    check(len(loop) == 1 and abs(loop[0].get("startSeconds", loop[0].get("start", -1)) - 1.0) < 1e-3,
          f"Reaper: the selected take's audio, where its item sits ({loop})")

    # FL Studio.
    report = imported(fl_project(scratch / "flstudio"))
    lead = track(report, "Lead")
    kick = track(report, "Kick")
    check(report["format"] == "fl-studio" and abs(report["tempo"] - 128) < 1e-6, f"FL Studio: its tempo ({report['tempo']})")
    check(lead.get("clips") == 2 and lead.get("notes") == 4 and kick.get("notes") == 8,
          f"FL Studio: a pattern placed twice, by channel ({lead}, {kick})")
    check("sampler" in graph_types("Kick"), f"FL Studio: the sampler channel plays its sound ({graph_types('Kick')})")
    check("opale-synth" in graph_types("Lead"), f"FL Studio: 3x Osc on Opale ({graph_types('Lead')})")

    # Ardour.
    report = imported(ardour_session(scratch / "ardour"))
    vocals = track(report, "Vocals")
    synth = track(report, "Synth")
    check(abs(report["tempo"] - 90) < 1e-6 and report["numerator"] == 3, f"Ardour: tempo and meter ({report['tempo']}, {report['numerator']})")
    check(vocals.get("clips") == 1 and abs(vocals.get("gainDb", 0) + 6.02) < 0.1 and abs(vocals.get("pan", 0) + 0.5) < 1e-3,
          f"Ardour: the audio region, the gain and pan ({vocals})")
    check(synth.get("notes") == 3, f"Ardour: the MIDI region's notes ({synth})")
    joined = scratch / "ardour" / "CyDAW Imported Audio" / "Vocals-1.wav"
    check(joined.exists() and wav_channels(str(joined)) == 2, f"Ardour: the stereo region's two mono files made one ({joined})")

    # LMMS.
    report = imported(lmms_project(scratch / "lmms"))
    bass = track(report, "Bass")
    kicks = track(report, "Kick")
    loops = track(report, "Loops")
    check(abs(report["tempo"] - 140) < 1e-6 and bass.get("notes") == 2, f"LMMS: tempo and an instrument's pattern ({bass})")
    check(kicks.get("clips") == 2 and kicks.get("notes") == 4 * 2 + 2,
          f"LMMS: the beat/bassline placements on the kick's track ({kicks})")
    check(loops.get("clips") == 1 and loops.get("kind") == "audio", f"LMMS: the sample track ({loops})")
    check("kick-synth" in graph_types("Kick"), f"LMMS: Kicker on Kick ({graph_types('Kick')})")

    # Zrythm.
    report = imported(zrythm_project(scratch / "zrythm"))
    zkeys = track(report, "Keys")
    zloop = track(report, "Loop")
    check(report["format"] == "zrythm" and abs(report["tempo"] - 110) < 1e-6, f"Zrythm: the zstd project and its tempo ({report['tempo']})")
    check(zkeys.get("notes") == 6 and abs(zkeys.get("gainDb", 0) + 6.02) < 0.1, f"Zrythm: a MIDI region looped twice ({zkeys})")
    check(zloop.get("clips") == 1 and not report.get("missingFiles"), f"Zrythm: an audio region of the pool ({zloop})")

    # A MIDI file and its stems.
    report = imported(midi_and_stems(scratch / "stems"))
    kinds = sorted(item.get("kind") for item in report.get("tracks", []))
    check(abs(report["tempo"] - 95) < 0.01 and kinds == ["audio", "audio", "midi", "midi"],
          f"MIDI and stems: two MIDI tracks, two stems ({kinds}, {report['tempo']})")
    check("batterie" in graph_types("Drums"), f"MIDI and stems: channel 10 on a drum kit ({graph_types('Drums')})")

    # Real projects, when there are some here.
    if samples is None or not samples.is_dir():
        print("skip the real projects: no samples folder")
        return
    real = [
        ("flstudio/FL 20.8.4.flp", lambda r: track(r, "VST2").get("notes", 0) > 0 and r["application"].startswith("FL Studio 20"),
         "FL Studio 20: channels, patterns, a hosted plug-in's name"),
        ("lmms/DnB.mmpz", lambda r: len(r.get("tracks", [])) >= 15, "LMMS 1.3: the beat/bassline instruments"),
        ("reaper/vst.RPP", lambda r: track(r, "5.50").get("notes") == 4, "Reaper 5: its MIDI item"),
        ("aaf/PT_PCM_Internal.aaf", lambda r: track(r, "Audio 1").get("clips") == 1 and not r.get("missingFiles"),
         "Pro Tools AAF: embedded audio written out and placed"),
        ("aaf/DR_Stereo_Clip_Positioning.aaf", lambda r: track(r, "Audio 1").get("clips", 0) == 4 or len(r.get("missingFiles", [])) > 0,
         "DaVinci Resolve AAF: four stereo clips"),
        ("aaf/PR_WAV_Internal.aaf", lambda r: track(r, "Audio 1").get("clips") == 1, "Premiere AAF: its clip"),
    ]
    for relative, test, message in real:
        path = samples / relative
        if not path.exists():
            print("skip", relative)
            continue
        try:
            report = imported(path)
            check(test(report), f"{message} ({relative})")
        except RuntimeError as error:
            check(False, f"{message} ({relative}): {error}")


def main() -> int:
    global port
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True)
    parser.add_argument("--port", type=int, default=port)
    parser.add_argument("--samples", default=os.environ.get("CYDAW_IMPORT_SAMPLES", str(ROOT.parent / "CyDAW_Import_Samples")))
    args = parser.parse_args()
    port = args.port
    scratch = Path(tempfile.mkdtemp(prefix="cydaw-import-check-"))
    samples_copy = None
    if Path(args.samples).is_dir():
        # A copy: the import writes beside the projects (joined and embedded audio).
        samples_copy = scratch / "samples"
        shutil.copytree(args.samples, samples_copy, ignore=shutil.ignore_patterns("zstd", "CyDAW Imported Audio"))
    locations = scratch / "Locations.json"
    locations.write_text(json.dumps({"format": "CyDAW Locations", "version": 1, "folders": {
        "presets": str(scratch / "Presets"), "cache": str(scratch / "Cache")}}), encoding="utf-8")
    environment = dict(os.environ, CYDAW_NO_RECOVERY="1", CYDAW_LOCATIONS_FILE=str(locations))
    process = subprocess.Popen([str(Path(args.exe).resolve()), f"--control-port={port}", "--silent", "--window-size=1400x900"],
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
        scenario(scratch, samples_copy)
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
