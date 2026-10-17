"""Real UI gestures in an isolated silent 1800x1100 CyDAW window.
Checks note editing/undo, automation gestures and launcher desktop drops.
"""
import argparse,json,socket,struct,wave,math,uuid
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument("--port",type=int,required=True);p.add_argument("--output",type=Path,required=True);a=p.parse_args()
folder=a.output.resolve()/uuid.uuid4().hex;folder.mkdir(parents=True)
def q(**c):
    with socket.create_connection(("127.0.0.1",a.port),timeout=10) as s:
        s.settimeout(120);s.sendall((json.dumps(c)+"\n").encode());v=json.loads(s.makefile("rb").readline())
    assert v.get("ok"),v
    return v["result"]
def f(**c):return q(cmd="foundry",**c)
def tab():
    v=f(action="status");return v["tabs"][v["current"]]
def events(*v):return q(cmd="ui_input",events=list(v)+[dict(type="wait",frames=3)])
def click(x,y,twice=False,button=0):
    e=[dict(type="move",x=x,y=y),dict(type="down",button=button),dict(type="up",button=button)];events(*(e+(e[1:] if twice else [])))
def undo():events(dict(type="key",key="ctrl",down=True),dict(type="key",key="z"),dict(type="key",key="ctrl",down=False))
def snapshot(name):q(cmd="snapshot",path=str(folder/(name+".png")))
q(cmd="app_mode",mode="foundry");f(action="new",name="UI verification");f(action="add_track",type="midi");f(action="midi_clip",track=1,at=0)
events(dict(type="wait",frames=3));click(500,950,True)
notes=tab()["tracks"][1]["clips"][0]["notes"];assert len(notes)==1,"creating MIDI clip must open its editor"
before=notes[0]
events(dict(type="move",x=520,y=950),dict(type="down"),dict(type="move",x=740,y=928),dict(type="up"))
after=tab()["tracks"][1]["clips"][0]["notes"][0];assert after["start"]!=before["start"]
undo();assert tab()["tracks"][1]["clips"][0]["notes"][0]==before,"Ctrl+Z restores a dragged note"
click(700,700,True);curve=tab()["masterAutomation"];assert len(curve[0]["points"])==1
before_curve=curve
# The first point is under the pointer where it was added.
events(dict(type="move",x=700,y=700),dict(type="down"),dict(type="move",x=850,y=685),dict(type="up"))
assert tab()["masterAutomation"]!=before_curve,"dragging master automation"
undo();assert tab()["masterAutomation"]==before_curve,"one undo step per curve drag"
click(151,331);click(800,390,True)
assert len(tab()["tracks"][1]["automation"][0]["points"])==1,"A shows an editable track curve"
snapshot("foundry-gestures")
# Same real desktop-drop entry point as the browser payload's handler.
source=folder/"asymmetric.wav";samples=[int((.15*math.sin(i*.12)+.08*i/47999)*32767) for i in range(48000)]
with wave.open(str(source),"wb") as w:w.setparams((1,2,48000,0,"NONE","none"));w.writeframes(struct.pack("<"+"h"*len(samples),*samples))
q(cmd="app_mode",mode="studio");q(cmd="new_project");q(cmd="remove_track",track=0)
q(cmd="add_audio_track",file=str(source));q(cmd="set_view",mode=4);events(dict(type="wait",frames=3))
events(dict(type="file_drop",x=420,y=149,files=[str(source)]))
assert q(cmd="launch_clip",track=0,scene=0)["launched"];q(cmd="stop_launcher")
count=len(q(cmd="status")["midiTracks"])
events(dict(type="file_drop",x=900,y=180,files=[str(source)]))
tracks=q(cmd="status")["midiTracks"];assert len(tracks)==count+1 and "sampler" in tracks[-1]["devices"]
q(cmd="save_project",path=str(folder/"drop.cydaw"))
click(425,149,button=1);snapshot("launcher-reverse-menu");click(510,226);q(cmd="save_project")
def paths(v):
    if isinstance(v,dict):
        for x in v.values():yield from paths(x)
    elif isinstance(v,list):
        for x in v:yield from paths(x)
    elif isinstance(v,str) and "~reverse" in v and v.endswith(".wav"):yield v
files=set(paths(json.loads((folder/"drop.cydaw").read_text())))
assert files,"Reverse launcher menu must replace its event source"
# Compare PCM or floating-point WAVs without extra dependencies.
def values(path):
    data=Path(path).read_bytes();at=12;fmt=None;raw=None
    while at+8<=len(data):
        key=data[at:at+4];size=int.from_bytes(data[at+4:at+8],"little");payload=data[at+8:at+8+size]
        if key==b"fmt ":fmt=payload
        if key==b"data":raw=payload
        at+=8+size+(size%2)
    code,channels,rate,_,_,bits=struct.unpack_from("<HHIIHH",fmt)
    if code==65534:code=int.from_bytes(fmt[24:26],"little")
    size=bits//8
    return [struct.unpack_from("<f",raw,i)[0] if code==3 else int.from_bytes(raw[i:i+size],"little",signed=True)/2**(bits-1) for i in range(0,len(raw),size*channels)]
reverse_file=Path(next(iter(files)));reverse_file=reverse_file if reverse_file.is_absolute() else folder/reverse_file
rev=values(reverse_file);assert max(abs(x-y/32768) for x,y in zip(rev,reversed(samples)))<.0001
snapshot("launcher-drops")
report=dict(midiNoteAdd=True,midiNoteDrag=True,keyboardUndo=True,masterCurveAdd=True,masterCurveDrag=True,curveUndo=True,trackCurve=True,audioSlotDrop=True,newSamplerDrop=True,launcherReverseSamples=True)
(folder/"checks.json").write_text(json.dumps(report,indent=2));print("UI gestures passed:",folder,flush=True)
