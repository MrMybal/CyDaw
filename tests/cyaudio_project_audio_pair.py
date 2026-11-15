"""Capture shipped examples through the actual silent 48k/1024 ASIO callback.

Dependencies and banks are read-only. Projects are copied into a new campaign;
long examples are explicitly excerpted, never advertised as complete renders.
Repeat captures separate natural/random variation from cross-engine differences.
No performance claim is made from capture wall time.
"""
from __future__ import annotations
import argparse, copy, hashlib, json, math, os, platform, subprocess, time, traceback
from pathlib import Path
import numpy as np
from cyaudio_comparison import ROOT, Session, profile, pcm, write_json


def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''): h.update(b)
    return h.hexdigest()


def fingerprint():
    files=[]
    for directory in ('src','include','templates'):
        for f in sorted((ROOT/directory).rglob('*')):
            if f.is_file(): files.append(dict(path=str(f.relative_to(ROOT)),sha256=sha(f)))
    return files


def resolve_resources(value, base, resources):
    if isinstance(value,dict):
        for k,v in value.items():
            if isinstance(v,str) and k in ('audioFile','source','file','soundModelFile','samplerMapFile') and v and not v.startswith(('factory:','bank:')):
                f=Path(v.replace('\\','/'))
                if not f.is_absolute(): f=base/f
                if f.is_file():
                    f=f.resolve();value[k]=str(f);resources[str(f)]=sha(f)
                elif k in ('audioFile','source','file') and f.suffix.lower() in ('.wav','.flac','.mp3','.ogg','.aif','.aiff'):
                    resources[str(f)]='MISSING'
            else: resolve_resources(v,base,resources)
    elif isinstance(value,list):
        for v in value: resolve_resources(v,base,resources)


def crop(data,end_beats):
    seconds=end_beats*60/data['tempo']
    data.update(position=0.0,loop=False,loopRangeExplicit=False,loopStart=0.0,loopEnd=seconds)
    for t in data.get('tracks',[]):
        t['audioClips']=[c for c in t.get('audioClips',[]) if c.get('start',0)<seconds]
        for c in t['audioClips']:
            c['duration']=min(c['duration'],seconds-c.get('start',0))
        if 'clipDuration' in t:t['clipDuration']=min(t['clipDuration'],max(0,seconds-t.get('clipStart',0)))
    for t in data.get('midiTracks',[]):
        t['clips']=[c for c in t.get('clips',[]) if c['startBeats']<end_beats]
        for c in t['clips']:
            c['lengthBeats']=min(c['lengthBeats'],end_beats-c['startBeats'])
            c['notes']=[n for n in c.get('notes',[]) if n['startBeats']<c['lengthBeats']]
            for n in c['notes']:n['durationBeats']=min(n['durationBeats'],c['lengthBeats']-n['startBeats'])
            c['playEndBeats']=min(c.get('playEndBeats',c['lengthBeats']),c['lengthBeats'])
    # Legacy flat fallback is not used when modern clips exist, but keep it bounded.
    data['midiNotes']=[n for n in data.get('midiNotes',[]) if n.get('startBeats',0)<end_beats]
    return seconds


def prepare(out, selected):
    cases=[];resources={}
    for source in sorted((ROOT/'templates').glob('*.cydaw')):
        name=source.stem
        if selected and not any(name.startswith(n) for n in selected):continue
        d=json.loads(source.read_text(encoding='utf-8-sig'));original=copy.deepcopy(d)
        number=int(name[:2]);description='complete original musical content'
        if number<=10:
            duration=None
        elif number==11:
            # Eight original phrases, first four beats each, sample/model alternating.
            for i,t in enumerate(d['midiTracks']):
                for c in t['clips']:c['startBeats']=i*4;c['lengthBeats']=4
            duration=crop(d,32);description='eight sample/model phrases, first four beats each, placed consecutively'
        elif number==12:
            d['midiTracks']=d['midiTracks'][:4]
            duration=crop(d,32);description='first four original tracks only (Steinway and Kawai, samples/models), original timing'
        elif number==24:
            # This is an empty instrument rack. Probe it explicitly instead of accepting silence.
            for i,t in enumerate(d['midiTracks']):
                t['clips']=[dict(name='QA notes (not original example)',startBeats=i*4,lengthBeats=4,loop=False,
                    playStartBeats=0,playEndBeats=4,loopStartBeats=0,loopEndBeats=4,
                    notes=[dict(note=n,startBeats=b,durationBeats=.7,velocity=.6) for b,n in [(0,48),(1,60),(2,67)]],gainAutomation=[])]
            duration=crop(d,len(d['midiTracks'])*4);description='generated three-note probe per track, sequential; original rack has no clips'
        else:
            beats=48 if number in (13,23) else 40 if name=='19-effets-studio' else 24 if number in (21,22) else 16
            duration=crop(d,beats);description=f'first {beats} beats, original clips/notes/graphs'
        resolve_resources(d,source.parent,resources)
        project=out/'projects'/(name+'.cydaw');write_json(project,d)
        types=set()
        def visit(x):
            if isinstance(x,dict):
                if isinstance(x.get('type'),str):types.add(x['type'])
                for v in x.values():visit(v)
            elif isinstance(x,list):
                for v in x:visit(v)
        visit(d)
        cases.append(dict(name=name,source=str(source),sourceSha256=sha(source),project=str(project),projectSha256=sha(project),
            selection=description,expectedContentSeconds=duration,originalAudioTracks=len(original.get('tracks',[])),
            originalMidiTracks=len(original.get('midiTracks',[])),audioTracks=len(d.get('tracks',[])),midiTracks=len(d.get('midiTracks',[])),nodeTypes=sorted(types)))
    write_json(out/'cases.json',cases)
    write_json(out/'resources.json',[dict(path=p,sha256=h) for p,h in resources.items()])
    return cases


class Capture(Session):
    def __init__(self,exe,folder,banks,port):
        self.exe=exe;self.folder=folder;self.port=port;folder.mkdir(parents=True,exist_ok=True)
        env=profile(folder,banks)
        env['CYDAW_BANKS_DIR']=str(banks.resolve())
        self.log=open(folder/'app.log','w',encoding='utf-8');self.commands=open(folder/'commands.jsonl','w',encoding='utf-8')
        self.process=subprocess.Popen([str(exe),'--silent','--hidden',f'--control-port={port}','--window-size=1500x900','--language=en'],env=env,stdout=self.log,stderr=subprocess.STDOUT)
        try:
            def ready():
                if self.process.poll() is not None:raise RuntimeError('Application exited at startup')
                try:self.q(cmd='ping');return True
                except (OSError,ValueError):return False
            self.wait(ready,60);assert self.q(cmd='status')['speakersSilenced']
            self.q(cmd='device_channels',inputs=0,outputs=2)
            self.audio=self.q(cmd='set_audio_device',type='ASIO',output='ASIO Fireface USB',input='',sampleRate=48000,bufferSize=1024)
            assert self.audio['type']=='ASIO' and self.audio['sampleRate']==48000 and self.audio['bufferSize']==1024 and not self.audio['inputChannels'],self.audio
            assert all(not d['enabled'] for d in self.q(cmd='midi_inputs')['devices'])
        except BaseException:self.close();raise
    def q(self,**data):
        self.commands.write(json.dumps(dict(at=time.perf_counter(),request=data))+'\n');self.commands.flush()
        return super().q(**data)
    def close(self):
        r=super().close();self.commands.close();return r


def stats(x,rate):
    mono=np.max(abs(x),axis=1); active=np.flatnonzero(mono>1e-5)
    return dict(frames=len(x),channels=x.shape[1],sampleRate=rate,finite=bool(np.isfinite(x).all()),peak=float(abs(x).max()),
        rmsDbfs=db(np.sqrt(np.mean(x*x))),dc=x.mean(axis=0).tolist(),clippedSampleValues=int(np.count_nonzero(abs(x)>=1-1/8388608)),
        activeFrames=int(np.count_nonzero(mono>1e-5)),firstActiveFrame=int(active[0]) if len(active) else None,
        lastActiveFrame=int(active[-1]) if len(active) else None,tailPeak=float(abs(x[-min(2000,len(x)):]).max()))


def db(v):return float(20*np.log10(max(float(v),1e-30)))


def compare(a,b):
    ra,x=pcm(a);rb,y=pcm(b);row=dict(rates=[ra,rb],shapes=[list(x.shape),list(y.shape)])
    if ra!=rb or x.shape!=y.shape:return row
    d=x-y;rms=np.sqrt(np.mean(d*d));sx=np.sqrt(np.mean(x*x));sy=np.sqrt(np.mean(y*y))
    row.update(equalPcm=bool(np.array_equal(x,y)),maxAbs=float(abs(d).max()),errorRmsDbfs=db(rms),referenceRmsDbfs=db(sx),
        snrDb=db(sx/max(float(rms),1e-30)),rmsDeltaDb=db(sy)-db(sx),dcDelta=(y.mean(axis=0)-x.mean(axis=0)).tolist(),
        correlation=float(np.corrcoef(x.ravel(),y.ravel())[0,1]) if sx>0 and sy>0 else None)
    # Secondary lag estimate only: raw differences above are always retained.
    if not row['equalPcm'] and sx>1e-8 and sy>1e-8:
        # Four seconds avoid very large transform allocations; zero-mean is ONLY for estimating delay.
        u=x[:min(len(x),ra*4)].mean(axis=1);v=y[:len(u)].mean(axis=1);u=u-u.mean();v=v-v.mean()
        n=1<<(len(u)*2-1).bit_length();c=np.fft.irfft(np.fft.rfft(v,n)*np.conj(np.fft.rfft(u,n)),n)
        lags=np.arange(-2048,2049);lag=int(lags[np.argmax(c[lags%n])]);row['estimatedLagFrames']=lag
        aa=x[:len(x)-lag] if lag>=0 else x[-lag:];bb=y[lag:] if lag>=0 else y[:len(y)+lag]
        e=aa-bb;row['lagAlignedErrorRmsDbfs']=db(np.sqrt(np.mean(e*e)))
    return row


def analyze(out):
    runs=json.loads((out/'runs.json').read_text(encoding='utf-8'));cases=json.loads((out/'cases.json').read_text(encoding='utf-8'));rows=[]
    for case in cases:
        captures=[r for r in runs if r['name']==case['name'] and r.get('result')=='captured'];row=dict(name=case['name'],cross=[],repeat={})
        for i in sorted({r['round'] for r in captures}):
            pair={r['label']:r for r in captures if r['round']==i}
            if len(pair)==2:row['cross'].append(dict(round=i,**compare(Path(pair['JUCE']['wav']),Path(pair['CyAudioCore']['wav']))))
        for label in ('JUCE','CyAudioCore'):
            rr=[r for r in captures if r['label']==label]
            if len(rr)>=2:row['repeat'][label]=compare(Path(rr[0]['wav']),Path(rr[1]['wav']))
        rows.append(row)
    write_json(out/'audio-comparison.json',rows)
    print('Analyzed',len(rows),'cases',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--juce',type=Path);p.add_argument('--core',type=Path);p.add_argument('--banks',type=Path,default=Path('D:/CyDAW/Banks'));p.add_argument('--rounds',type=int,default=2);p.add_argument('--port',type=int,default=48265);p.add_argument('--select',nargs='*');p.add_argument('--analyze',action='store_true');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output')
    if a.analyze:analyze(out);return
    assert a.juce and a.core and a.rounds>=1
    out.mkdir(parents=True,exist_ok=True);assert not (out/'manifest.json').exists(),'Use a NEW campaign to retain evidence'
    cases=prepare(out,a.select);exe={'JUCE':a.juce.resolve(),'CyAudioCore':a.core.resolve()};files=list(exe.values())
    files += [f for e in exe.values() for f in e.parent.glob('*.dll')]
    inputs=[dict(path=str(f),sha256=sha(f)) for f in files]
    before=fingerprint();manifest=dict(baseRevision=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'rev-parse','HEAD'],text=True).strip(),
        gitStatus=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'status','--short'],text=True),platform=platform.platform(),python=platform.python_version(),executables=inputs,
        baseline='JUCE cf4228d, frozen /O2 production audio plus isolated preferences patch; no DSP edits',core='current production binary, no QA instrumentation',rounds=a.rounds,
        protocol='fresh process per project and engine and round; alternating order; actual ASIO callback 48000Hz/1024, speakers silenced, zero audio/MIDI inputs; original gains; no normalization; PCM comparisons, plus within-engine repeats; no CPU performance claim',sourceBefore=before)
    write_json(out/'manifest.json',manifest);runs=[]
    try:
        for iteration in range(1,a.rounds+1):
            for j,case in enumerate(cases):
                order=['JUCE','CyAudioCore'] if (iteration+j)%2 else ['CyAudioCore','JUCE']
                for label in order:
                    folder=out/f'round-{iteration}'/case['name']/label;row=dict(name=case['name'],round=iteration,label=label);s=None
                    try:
                        s=Capture(exe[label],folder,a.banks,a.port);row['audio']=s.audio
                        row['loaded']=s.q(cmd='open_project',path=case['project']);assert not row['loaded'].get('warnings'),row['loaded'].get('warnings')
                        assert row['loaded']['speakersSilenced'] and not row['loaded']['playing']
                        row['expectedTrackCount']=case['audioTracks']+case['midiTracks']
                        assert len(row['loaded']['audioTracks'])==case['audioTracks'] and len(row['loaded']['midiTracks'])==case['midiTracks']
                        wav=folder/'render.wav';row['wav']=str(wav)
                        s.q(cmd='export_mix',path=str(wav));s.wait(lambda:not s.q(cmd='status').get('exporting'),max(90,row['loaded'].get('length',0)+45))
                        row['after']=s.q(cmd='status');rate,x=pcm(wav);row['stats']=stats(x,rate);row['wavSha256']=sha(wav)
                        assert row['stats']['finite'] and row['stats']['activeFrames']>0,row['stats']
                        row['result']='captured'
                    except BaseException:
                        row['result']='failed';row['error']=traceback.format_exc()
                    finally:
                        if s:row['shutdown']=s.close()
                        runs.append(row);write_json(out/'runs.json',runs)
                    print(iteration,case['name'],label,row['result'],json.dumps(row.get('stats',{})),flush=True)
    finally:
        manifest['sourceStable']=before==fingerprint();manifest['executableInputsStable']=all(sha(Path(r['path']))==r['sha256'] for r in inputs);write_json(out/'manifest.json',manifest)
    analyze(out)


if __name__=='__main__':main()
