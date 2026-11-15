"""Build original, portable Foundry/Cinema/DJ demonstration templates locally.

No personal banks or third-party recordings are used. Requires numpy and ffmpeg.
The optional --control-port lets CyDAW attach its native effect graphs and verify
the session formats before paths are made relative for installation.
"""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import shutil
import socket
import subprocess
import wave
import numpy as np

RATE = 48000
DJ_FILES = ('CyDAW - Horizon.wav', 'CyDAW - Reflets.wav',
            'CyDAW - Pulsation.wav', 'CyDAW - Percussions.wav')


def write_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def wav(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if data.ndim == 1:
        data = np.column_stack((data, data))
    data = np.clip(data, -.92, .92)
    with wave.open(str(path), 'wb') as f:
        f.setparams((2, 2, RATE, 0, 'NONE', 'none'))
        f.writeframes((data*32767).astype('<i2').tobytes())


def media(folder, ffmpeg):
    rng = np.random.default_rng(5319)
    def t(seconds): return np.arange(round(seconds*RATE))/RATE
    x = t(2)
    impact = (.28*np.sin(2*np.pi*53*x)*np.exp(-x*5)
              + .12*np.sin(2*np.pi*307*x)*np.exp(-x*8)
              + .08*rng.normal(size=len(x))*np.exp(-x*22))
    wav(folder/'impact.wav', impact)
    x = t(1.5)
    whoosh = .11*rng.normal(size=len(x))*np.sin(np.pi*x/1.5)**3
    whoosh += .07*np.sin(2*np.pi*(100*x+450*x*x))*np.sin(np.pi*x/1.5)**2
    wav(folder/'whoosh.wav', np.column_stack((whoosh, np.roll(whoosh, 170))))
    x = t(12)
    drone = .065*np.sin(2*np.pi*55*x)+.035*np.sin(2*np.pi*82.4069*x)
    drone += .018*np.sin(2*np.pi*110*x)*(1+.3*np.sin(2*np.pi*.21*x))
    drone *= np.minimum(x/.6, 1)*np.minimum((12-x)/1.2, 1)
    wav(folder/'ambience.wav', np.column_stack((drone, np.roll(drone, 240))))
    x = t(.9)
    chime = (.15*np.sin(2*np.pi*880*x)+.075*np.sin(2*np.pi*1320*x))*(1-np.exp(-x*90))*np.exp(-x*6)
    wav(folder/'confirmation.wav', chime)
    x = t(2.5)
    scan = .10*np.sin(2*np.pi*(160*x+360*x*x))*(.6+.4*np.sin(2*np.pi*7*x))
    scan *= np.sin(np.pi*x/2.5)**2
    wav(folder/'scanner.wav', np.column_stack((scan, scan*.85)))
    for number, bpm in enumerate((122, 124, 126, 126)):
        beat = 60/bpm
        seconds = 64*beat
        x = t(seconds)
        left = np.zeros(len(x)); right = np.zeros(len(x))
        def add(at, signal, pan=0):
            start = round(at*RATE); n = min(len(signal), len(x)-start)
            if n > 0:
                left[start:start+n] += signal[:n]*(1-max(0, pan)*.5)
                right[start:start+n] += signal[:n]*(1-max(0, -pan)*.5)
        for b in range(64):
            q = t(.25)
            kick = .48*np.sin(2*np.pi*(48*q+75*(1-np.exp(-q*35))/35))*np.exp(-q*19)
            add(b*beat, kick)
            q = t(.07)
            add((b+.5)*beat, rng.normal(size=len(q))*.07*np.exp(-q*80), .35)
            if b%2:
                q = t(.16)
                add(b*beat, rng.normal(size=len(q))*.08*np.exp(-q*26), -.2)
            if number == 3:
                if b%4 == 2:
                    q = t(.15); add((b+.75)*beat, .13*np.sin(2*np.pi*180*q)*np.exp(-q*38), -.4)
                continue
            if 8 <= b < 56:
                root = (33, 29, 36, 31)[(b//16)%4]
                hz = 440*2**((root-69)/12)
                q = t(beat*.72)
                bass = (.14*np.sin(2*np.pi*hz*q)+.03*np.sin(2*np.pi*2*hz*q))*(1-np.exp(-q*180))*np.exp(-q*5)
                add((b+.25*(number%2))*beat, bass)
                if b%2 == 0:
                    q = t(beat*1.4)
                    chord = sum(np.sin(2*np.pi*hz*2**((24+step)/12)*q) for step in (0,3,7))
                    chord *= .024*(1-np.exp(-q*90))*np.exp(-q*(4+number))
                    add((b+.5)*beat, chord, (-1)**b*.2)
        fade = np.minimum(x/.015, 1)*np.minimum((seconds-x)/.15, 1)
        wav(folder/DJ_FILES[number], np.column_stack((left*fade, right*fade)))
    # A twelve-second reference with visible contact events at the sound cues.
    video = folder/'orbital.mp4'
    filt = ("color=c=0x101923:s=960x540:r=24:d=12,"
            "drawgrid=w=80:h=80:t=1:c=0x263747,"
            "drawbox=x=100:y=110:w=760:h=320:c=0x41566d:t=3,"
            "drawbox=x=110:y=120:w=740:h=300:c=0xef8c42:t=fill:enable='between(t,3,3.12)+between(t,7,7.12)+between(t,10.5,10.62)',"
            "drawtext=text='CYDAW  /  ORBITAL':fontcolor=white:fontsize=38:x=140:y=165,"
            "drawtext=text='CONTACT  03.00  -  07.00  -  10.50':fontcolor=0xa7bcd0:fontsize=23:x=140:y=245,"
            "drawtext=text='%{pts\\:hms}':fontcolor=white:fontsize=25:x=140:y=330")
    subprocess.run([ffmpeg, '-y', '-loglevel', 'error', '-f', 'lavfi', '-i', filt,
                    '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-an', str(video)], check=True)


def build(folder):
    assets = folder/'media'
    counter = 0
    def uid():
        nonlocal counter
        counter += 1; return counter
    def clip(name, file, at, length, gain=0):
        return dict(id=uid(), name=name, file=str((assets/file).resolve()), start=round(at*RATE),
                    offset=0, length=round(length*RATE), fadeIn=round(.008*RATE),
                    fadeOut=round(min(.12, length/4)*RATE), gainDb=gain, notes=[])
    def track(name, clips=(), parent=0, bus=False, gain=0, pan=0):
        return dict(id=uid(), name=name, gainDb=gain, pan=pan, mute=False, solo=False,
                    midi=False, bus=bus, virtualGroup=bus, parentId=parent,
                    containerStart=0, containerLength=12*RATE if parent else 0,
                    clips=list(clips), automation=[])
    def session(cinema, tracks, markers, picture=''):
        return dict(format='CyDAW Cinema session' if cinema else 'CyDAW Foundry session',
                    version=5 if cinema else 3, rate=RATE, tempo=120, span=12*RATE,
                    renderTail=.5, tracks=tracks, markers=[dict(start=round(at*RATE),end=round(end*RATE),name=name)
                    for at,end,name in markers], masterAutomation=[], processing={},
                    picture=dict(file=str((assets/picture).resolve()) if picture else '',
                    start=0,sourceIn=0,duration=12 if picture else 0,fps=24))
    tracks=[track('Impacts - matiere', [clip('Impact sec','impact.wav',a,2,-5) for a in (.5,4,8)]),
            track('Trajectoires', [clip('Passage stereo','whoosh.wav',a,1.5,-7) for a in (2,6,10)]),
            track('Espace - fond', [clip('Drone grave','ambience.wav',0,12,-9)])]
    result=[('foundry/01-impact-et-matiere.cyfoundry',session(False,tracks,[(.5,.5,'Impact'),(4,4,'Variation'),(8,10,'Queue et espace')]))]
    tracks=[track('Confirmations', [clip('Validation','confirmation.wav',a,.9,-2) for a in (.5,3.5,7)]),
            track('Scanner', [clip('Balayage','scanner.wav',a,2.5,-4) for a in (1,5,9)]),
            track('Atmosphere', [clip('Fond numerique','ambience.wav',0,12,-13)])]
    result.append(('foundry/02-interface-futuriste.cyfoundry',session(False,tracks,[(.5,.5,'Valider'),(5,7.5,'Scanner'),(9,11.5,'Sortie')])) )
    for external in (False, True):
        sfx=track('SFX - bus',bus=True,gain=-2)
        ambience=track('Ambiances - bus',bus=True,gain=-3)
        tracks=[sfx, track('Contacts', [clip('Contact image','impact.wav',a,2,-5) for a in (3,7,10.5)],sfx['id']),
                track('Mouvements', [clip('Approche','whoosh.wav',a,1.5,-7) for a in (2,6,9.5)],sfx['id']),
                ambience,track('Station', [clip('Atmosphere continue','ambience.wav',0,12,-8)],ambience['id'])]
        result.append(('cinema/02-transport-externe.cycinema' if external else 'cinema/01-orbital-bruitage.cycinema',
                       session(True,tracks,[(3,3,'Contact 01'),(7,7,'Contact 02'),(10.5,10.5,'Contact 03')],'' if external else 'orbital.mp4')))
    for filename, data in result:
        write_json(folder/filename,data)
    for count,name,bpms,sources in [(2,'01-house-transition', [122,124], [1,2]),(4,'02-club-quatre-platines',[122,124,126,126],[1,2,3,4])]:
        decks=[dict(file=str((assets/DJ_FILES[source-1]).resolve()),bpm=bpm,firstBeat=0,
                    cuePoint=0,hotCues=[b*60/bpm for b in (0,8,16,32,48,56)]+[-1,-1],
                    trimDb=-3,fader=1 if i<2 else 0) for i,(bpm,source) in enumerate(zip(bpms,sources))]
        write_json(folder/'dj'/(name+'.cydjtemplate'),dict(format='CyDAW DJ template',version=1,
                   deckCount=count,crossfader=0,masterDb=-3,decks=decks,processing={}))


def enrich(folder, port):
    def q(**data):
        with socket.create_connection(('127.0.0.1',port),timeout=10) as s:
            s.settimeout(120);s.sendall((json.dumps(data)+'\n').encode());v=json.loads(s.makefile('rb').readline())
        if not v.get('ok'): raise RuntimeError(v)
        return v['result']
    for mode in ('foundry','cinema'):
        q(cmd='app_mode',mode=mode)
        for file in sorted((folder/mode).glob('*.cycinema' if mode=='cinema' else '*.cyfoundry')):
            data=json.loads(file.read_text(encoding='utf-8'))
            q(cmd=mode,action='open',file=str(file.resolve()))
            selected=next((t for t in data['tracks'] if t['bus']),data['tracks'][0])
            for key in ('gain','reverb'):
                q(cmd=mode,action='processing',op='add_effect',inputId=selected['id'],key=key)
            # Parent IDs describe the timeline containers. The processing graph
            # also needs actual sends, as when a subtrack is created in the UI.
            processing=q(cmd=mode,action='processing',op='status')
            keys={t['inputId']:t['key'] for t in processing['tracks'] if t.get('inputId')}
            for track in data['tracks']:
                if track['parentId']:
                    q(cmd=mode,action='processing',op='route',inputId=track['id'],destination=keys[track['parentId']])
            q(cmd=mode,action='save_session',path=str(file.resolve()))
    q(cmd='app_mode',mode='dj')
    q(cmd='dj',action='processing',op='add_effect',key='limiter')
    processing=q(cmd='dj',action='processing',op='status')
    for file in (folder/'dj').glob('*.cydjtemplate'):
        data=json.loads(file.read_text(encoding='utf-8'));data['processing']=processing;write_json(file,data)


def relative_paths(folder):
    import os
    for file in list(folder.glob('foundry/*.cyfoundry'))+list(folder.glob('cinema/*.cycinema'))+list(folder.glob('dj/*.cydjtemplate')):
        data=json.loads(file.read_text(encoding='utf-8'))
        def visit(value):
            if isinstance(value,dict):
                for k,v in value.items():
                    if k=='file' and isinstance(v,str) and v and Path(v).is_absolute():
                        value[k]=os.path.relpath(v,file.parent).replace('\\','/')
                    else:visit(v)
            elif isinstance(value,list):
                for v in value:visit(v)
        visit(data);write_json(file,data)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--ffmpeg',required=True)
    p.add_argument('--control-port',type=int);p.add_argument('--install',type=Path,action='append',default=[])
    a=p.parse_args();folder=a.output.resolve();folder.mkdir(parents=True,exist_ok=False)
    media(folder/'media',a.ffmpeg);build(folder)
    if a.control_port:enrich(folder,a.control_port)
    relative_paths(folder)
    write_json(folder/'manifest.json',dict(format='CyDAW mode examples',version=1,
               media='Original deterministic synthesis and generated video; no external recordings.',
               templates=[str(f.relative_to(folder)) for ext in ('*.cyfoundry','*.cycinema','*.cydjtemplate') for f in sorted(folder.rglob(ext))]))
    for target in a.install:
        for mode in ('foundry','cinema','dj','media'):
            shutil.copytree(folder/mode,target/mode,dirs_exist_ok=True)
    print('Created six mode templates in',folder,flush=True)


if __name__=='__main__':main()
