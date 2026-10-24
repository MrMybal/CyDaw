"""Run against an isolated, silent CyDAW instance; --port and --output are required.
Checks actual audio through Foundry track/bus/master graphs and DJ recordings.
"""
import argparse
import json
import math
import socket
import struct
import time
import uuid
import wave
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--port", type=int, required=True)
parser.add_argument("--output", type=Path, required=True)
a = parser.parse_args()
folder = a.output.resolve() / uuid.uuid4().hex
folder.mkdir(parents=True)

def request(**c):
    with socket.create_connection(("127.0.0.1", a.port), timeout=10) as s:
        s.settimeout(120)
        s.sendall((json.dumps(c) + "\n").encode())
        return json.loads(s.makefile("rb").readline())

def need(**c):
    r = request(**c)
    assert r.get("ok"), r
    return r["result"]

def foundry(**c):
    return need(cmd="foundry", **c)

def processing(**c):
    return foundry(action="processing", **c)

def dj(**c):
    return need(cmd="dj", **c)

def peak(file):
    with wave.open(str(file), "rb") as w:
        raw = w.readframes(w.getnframes())
        size = w.getsampwidth()
        return max(abs(int.from_bytes(raw[i:i+size], "little", signed=True)) / 2**(size*8-1)
                   for i in range(0, len(raw), size)), w.getnframes()

def gain_binding(state, **selector):
    track = next(t for t in state["tracks"] if all(t.get(k) == v for k, v in selector.items()))
    node = next(n for n in track["graph"]["nodes"] if n["type"] == "gain")
    return f"node.{node['id']}.gain.db"

# An untouched Studio project must stay untouched while the two modes build their mixes.
# It plays tones: only on an instance whose speakers are silenced (--silent).
assert need(cmd="status")["speakersSilenced"], "Use an isolated silent instance (--silent)"
before = need(cmd="status")["audioTracks"]
tone = folder / "tone.wav"
with wave.open(str(tone), "wb") as w:
    w.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
    w.writeframes(b"".join(struct.pack("<hh", int(6500*math.sin(i*.131)), int(6500*math.sin(i*.131)))
                          for i in range(48000*8)))
source_peak, source_frames = peak(tone)
foundry(action="open", file=str(tone))
state = processing()
input_id = state["tracks"][0]["inputId"]
source_key = state["tracks"][0]["key"]
state = processing(op="add_effect", inputId=input_id, key="gain")
processing(op="parameter", inputId=input_id, binding=gain_binding(state, inputId=input_id), value=-6)
state = processing(op="bus", name="Processing test bus")
bus = next(t for t in state["tracks"] if t["bus"])
state = processing(op="add_effect", trackKey=bus["key"], key="gain")
processing(op="parameter", trackKey=bus["key"], binding=gain_binding(state, key=bus["key"]), value=-6)
processing(op="route", inputId=input_id, destination=bus["key"])
assert not request(cmd="foundry", action="processing", op="route", trackKey=bus["key"], destination=source_key)["ok"]
processing(op="mix", gain=-6)
file = folder / "processed.wav"
foundry(action="export", path=str(file))
processed_peak, count = peak(file)
assert count == source_frames
attenuation = 20*math.log10(processed_peak/source_peak)
assert abs(attenuation+18) < .15, attenuation
# Playback uses the same chain, before the silenced common speaker output.
foundry(action="play")
time.sleep(.35)
state = foundry(action="status")
assert state["playing"] and abs(state["outputPeakDb"] - 20*math.log10(processed_peak)) < .5, state
foundry(action="stop")
# Saving and truly reopening retains graphs, routing, stable IDs and the master.
session = folder / "processed.cyfoundry"
foundry(action="save_session", path=str(session))
foundry(action="close")
foundry(action="open", file=str(session))
state = processing()
assert state["masterGain"] == -6 and len(state["tracks"]) == 2
again = folder / "reopened.wav"
foundry(action="export", path=str(again))
assert abs(peak(again)[0]-processed_peak) < 1e-5
with_tail = folder / "tail.wav"
foundry(action="export", path=str(with_tail), tail=.125)
assert peak(with_tail)[1] == source_frames+6000
# A second document starts with its own neutral chain.
second = folder / "independent.wav"
second.write_bytes(tone.read_bytes())
foundry(action="open", file=str(second))
assert processing()["masterGain"] == 0
need(cmd="app_mode", mode="foundry")
need(cmd="snapshot", path=str(folder / "foundry.png"))

# DJ recordings must hear additional Studio modules, independently of Foundry.
dj(action="mixer", autoGain=False, crossfader=.5)
dj(action="load", deck="A", file=str(tone))
for _ in range(200):
    if dj(action="status")["decks"][0]["state"] == "ready":
        break
    time.sleep(.05)
else:
    raise AssertionError("deck did not load")
dj(action="channel", deck="A", trimDb=0, side=1, effect=dict(on=False))
dj(action="keylock", deck="A", on=False)

def record(name):
    dj(action="seek", deck="A", seconds=0)
    dj(action="play", deck="A", on=True)
    time.sleep(.3)
    file = folder / name
    dj(action="record", on=True, path=str(file))
    time.sleep(.8)
    dj(action="record", on=False)
    dj(action="play", deck="A", on=False)
    return peak(file)[0]

dry = record("dj-dry.wav")
state = dj(action="processing", op="add_effect", inputId=1, key="gain")
dj(action="processing", op="parameter", inputId=1, binding=gain_binding(state, inputId=1), value=-12)
wet = record("dj-processed.wav")
difference = 20*math.log10(wet/dry)
assert abs(difference+12) < .2, difference
need(cmd="app_mode", mode="dj")
need(cmd="snapshot", path=str(folder / "dj.png"))
assert need(cmd="status")["audioTracks"] == before, "processing sessions must not add Studio tracks"
report = dict(foundryTrackBusMasterDb=attenuation, foundryRealtime=True, persistence=True,
              independentDocuments=True, cycleRejected=True, exportTail=True,
              djRecordedDifferenceDb=difference, studioUnchanged=True)
(folder / "checks.json").write_text(json.dumps(report, indent=2), encoding="utf8")
print("Foundry and DJ processing checks passed:", folder, flush=True)
