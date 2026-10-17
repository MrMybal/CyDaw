"""Exercise Foundry MIDI, automation and reverse through real saved/rendered audio.
Attach to an isolated silent instance (--port), write fixtures under --output.
"""
import argparse, json, math, socket, struct, wave, uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument("--port",type=int,required=True);p.add_argument("--output",type=Path,required=True);a=p.parse_args()
folder=a.output.resolve()/uuid.uuid4().hex;folder.mkdir(parents=True)
def need(**c):
    with socket.create_connection(("127.0.0.1",a.port),timeout=10) as s:
        s.settimeout(120);s.sendall((json.dumps(c)+"\n").encode());answer=json.loads(s.makefile("rb").readline())
    assert answer.get("ok"),answer
    return answer["result"]
def f(**c):return need(cmd="foundry",**c)
def tab(v):return v["tabs"][v["current"]]
def export(name,**fields):
    file=folder/(name+".wav");f(action="export",path=str(file),bits=16,**fields)
    with wave.open(str(file),"rb") as w:
        n=w.getnframes();channels=w.getnchannels();size=w.getsampwidth();raw=w.readframes(n)
    values=[int.from_bytes(raw[i:i+size],"little",signed=True)/2**(size*8-1) for i in range(0,len(raw),size*channels)]
    return values

def rms(v):return math.sqrt(sum(x*x for x in v)/max(1,len(v)))
def db(a,b):return 20*math.log10(rms(a)/rms(b))
need(cmd="app_mode",mode="foundry")
f(action="new",name="MIDI and automation")
v=tab(f(action="add_track",type="midi"));assert v["tracks"][1]["midi"]
v=tab(f(action="midi_clip",track=1,at=0));clip=v["tracks"][1]["clips"][0];cid=clip["id"]
notes=[dict(start=2400,length=15000,key=60,velocity=.7),dict(start=24000,length=20000,key=67,velocity=.6),dict(start=48000,length=15000,key=64,velocity=.8)]
v=tab(f(action="notes",id=cid,notes=notes));input_id=v["tracks"][1]["id"]
dry=export("midi-dry");assert max(map(abs,dry))>.001,"MIDI must produce audio"
f(action="automation",track=1,lanes=[dict(binding="",points=[dict(frame=0,value=-18)])])
quiet=export("midi-quiet");delta=db(quiet,dry);assert abs(delta+18)<.15,delta
f(action="automation",track=1,lanes=[])
state=f(action="processing",op="add_effect",inputId=input_id,key="gain")
track=next(t for t in state["tracks"] if t.get("inputId")==input_id)
node=next(n for n in track["graph"]["nodes"] if n["type"]=="gain")
binding=f"node.{node['id']}.gain.db"
f(action="automation",track=1,lanes=[dict(binding=binding,points=[dict(frame=0,value=-12)])])
wet=export("midi-effect");effect_delta=db(wet,dry);assert abs(effect_delta+12)<.15,effect_delta
f(action="automation",track=-1,lanes=[dict(binding="",points=[dict(frame=0,value=-6)])])
master=export("midi-master");master_delta=db(master,wet);assert abs(master_delta+6)<.15,master_delta
saved=folder/"midi.cyfoundry";f(action="save_session",path=str(saved));f(action="close");v=tab(f(action="open",file=str(saved)))
assert v["tracks"][1]["midi"] and len(v["tracks"][1]["clips"][0]["notes"])==3 and len(v["masterAutomation"])==1
reopened=export("midi-reopened");assert max(abs(x-y) for x,y in zip(master,reopened))<.0001,"MIDI/session/graph/automation persistence"
# Selection tails release held notes and must not launch later notes.
selection=export("selection-tail",start=0,end=12000,tail=1)
cid=v["tracks"][1]["clips"][0]["id"]
f(action="notes",id=cid,notes=notes[:1])
selection_alone=export("selection-tail-no-future",start=0,end=12000,tail=1)
assert max(abs(x-y) for x,y in zip(selection,selection_alone))<.0001,"notes after selection must not trigger in its tail"
f(action="automation",track=-1,lanes=[])
tail_dry=export("tail-master-dry",start=0,end=12000,tail=1)
f(action="automation",track=-1,lanes=[dict(binding="",points=[dict(frame=0,value=-6)])])
assert abs(db(selection_alone[12000:24000],tail_dry[12000:24000])+6)<.15,"master automation must cover release tails"
f(action="notes",id=cid,notes=notes)
# Undo and redo must restore actual MIDI notes and curve data.
cid=v["tracks"][1]["clips"][0]["id"]
f(action="notes",id=cid,notes=[]);assert not tab(f(action="status"))["tracks"][1]["clips"][0]["notes"]
f(action="undo");assert len(tab(f(action="status"))["tracks"][1]["clips"][0]["notes"])==3
f(action="redo");assert not tab(f(action="status"))["tracks"][1]["clips"][0]["notes"]
f(action="undo")
# Audio reverse must reverse only the selected clip, with the file left intact.
source=folder/"asymmetric.wav";samples=[int((.15*math.sin(i*.12)+.08*i/47999)*32767) for i in range(48000)]
with wave.open(str(source),"wb") as w:w.setparams((1,2,48000,0,"NONE","none"));w.writeframes(struct.pack("<"+"h"*len(samples),*samples))
v=tab(f(action="open",file=str(source)));cid=v["tracks"][0]["clips"][0]["id"]
f(action="reverse_clip",id=cid);rev=export("reverse")
assert max(abs(x-y/32768) for x,y in zip(rev,reversed(samples)))<.0001,"sample-accurate clip reverse"
f(action="undo");restored=export("reverse-undone");assert max(abs(x-y/32768) for x,y in zip(restored,samples))<.0001
# Track + master gain curves both affect an audio render at the same timeline position.
f(action="automation",track=0,lanes=[dict(binding="",points=[dict(frame=0,value=-18),dict(frame=48000,value=0)])])
f(action="automation",track=-1,lanes=[dict(binding="",points=[dict(frame=0,value=-6)])])
auto=export("audio-ramp");ratios=[]
for at in (12000,24000,36000):
    value=db(auto[at-240:at+240],restored[at-240:at+240]);expected=-18+18*at/48000-6
    assert abs(value-expected)<.15,(at,value,expected);ratios.append(value)
# A WAV imported onto a MIDI track becomes a sampler instrument and a playable note clip.
f(action="new",name="Sampled MIDI")
f(action="add_track",type="midi")
v=tab(f(action="import",track=1,file=str(source),at=0))
assert v["tracks"][1]["clips"][0]["notes"],"sample drop must create MIDI notes"
sample=export("sampled-midi");assert max(map(abs,sample))>.001,"sampled MIDI render"
# A bus created in the shared mixer appears on the timeline and has audible automation.
state=f(action="processing",op="bus",name="Automated bus")
bus=next(t for t in state["tracks"] if t["bus"])
v=tab(f(action="status"));bus_index=next(i for i,t in enumerate(v["tracks"]) if t.get("bus"))
input_id=v["tracks"][1]["id"]
f(action="processing",op="route",inputId=input_id,destination=bus["key"])
f(action="automation",track=bus_index,lanes=[dict(binding="",points=[dict(frame=0,value=-12)])])
bus_audio=export("automated-bus");bus_delta=db(bus_audio,sample);assert abs(bus_delta+12)<.15,bus_delta
f(action="automation",track=bus_index,lanes=[])
sample_session=folder/"sampled-midi.cyfoundry";f(action="save_session",path=str(sample_session));f(action="close");f(action="open",file=str(sample_session))
sample_again=export("sampled-midi-reopened");assert max(abs(x-y) for x,y in zip(sample,sample_again))<.0001
need(cmd="snapshot",path=str(folder/"foundry.png"))
report=dict(busAutomationDb=bus_delta,selectionTailNoFutureNotes=True,masterAutomationInTail=True,midiSamplerImport=True,midiRendered=True,midiGainDb=delta,nativeEffectAutomationDb=effect_delta,masterAutomationDb=master_delta,persistence=True,undoRedo=True,reverseSamples=True,audioRampDb=ratios)
(folder/"checks.json").write_text(json.dumps(report,indent=2));print("MIDI, automation and reverse checks passed:",folder,flush=True)
