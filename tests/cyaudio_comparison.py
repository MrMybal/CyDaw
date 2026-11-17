"""Paired Windows JUCE/CyAudioCore acceptance and performance, using real executables.

No production code is instrumented. CPU/private memory come from Windows APIs;
audio comparisons decode PCM, ignoring container/header differences. --silent
keeps playback in the actual hardware callback while silencing speakers.
See docs/CyAudioCoreComparison.fr.md for the frozen baseline and QA-only patch.
"""
from __future__ import annotations
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import socket
import statistics
import subprocess
import sys
import time
import traceback
import wave
import xml.etree.ElementTree as ET
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
K = C.WinDLL('kernel32', use_last_error=True)
PS = C.WinDLL('psapi', use_last_error=True)
K.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
K.OpenProcess.restype = W.HANDLE
K.CloseHandle.argtypes = [W.HANDLE]
K.GetProcessTimes.argtypes = [W.HANDLE]+[C.POINTER(W.FILETIME)]*4
K.CreateToolhelp32Snapshot.argtypes = [W.DWORD,W.DWORD]
K.CreateToolhelp32Snapshot.restype = W.HANDLE

class Entry(C.Structure):
    _fields_ = [('size',W.DWORD),('usage',W.DWORD),('pid',W.DWORD),('heap',C.c_size_t),
        ('module',W.DWORD),('threads',W.DWORD),('parent',W.DWORD),('priority',W.LONG),
        ('flags',W.DWORD),('exe',W.WCHAR*260)]
K.Process32FirstW.argtypes = K.Process32NextW.argtypes = [W.HANDLE,C.POINTER(Entry)]
class Memory(C.Structure):
    _fields_ = [('cb',W.DWORD),('faults',W.DWORD)]+[(n,C.c_size_t) for n in (
        'peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]
PS.GetProcessMemoryInfo.argtypes = [W.HANDLE,C.POINTER(Memory),W.DWORD]

def descendants(pid):
    handle=K.CreateToolhelp32Snapshot(2,0)
    if handle in (None,C.c_void_p(-1).value):raise C.WinError(C.get_last_error())
    pairs=[]
    try:
        entry=Entry();entry.size=C.sizeof(entry)
        ok=K.Process32FirstW(handle,C.byref(entry))
        while ok:
            pairs.append((entry.pid,entry.parent));ok=K.Process32NextW(handle,C.byref(entry))
    finally:K.CloseHandle(handle)
    found={pid}
    for _ in range(8):
        newer=found|{p for p,parent in pairs if parent in found}
        if newer==found:break
        found=newer
    return found

def process_sample(pid):
    result={}
    for child in descendants(pid):
        h=K.OpenProcess(0x410,False,child)
        if not h:continue
        try:
            values=[W.FILETIME() for _ in range(4)]
            if not K.GetProcessTimes(h,*(C.byref(v) for v in values)):continue
            seconds=sum((v.dwHighDateTime<<32)+v.dwLowDateTime for v in values[2:])*1e-7
            m=Memory();m.cb=C.sizeof(m)
            if not PS.GetProcessMemoryInfo(h,C.byref(m),m.cb):raise C.WinError(C.get_last_error())
            result[child]={'cpu':seconds,'private':m.private,'workingSet':m.ws}
        finally:K.CloseHandle(h)
    return result

def write_json(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')

def profile(folder,banks):
    roaming=folder/'Roaming';local=folder/'Local';settings=roaming/'CyDAW'
    settings.mkdir(parents=True,exist_ok=True);local.mkdir(parents=True,exist_ok=True)
    locations=settings/'Locations.json'
    dirs={k:str(folder/k) for k in ('cache','presets','banks','devices','mixes','foundry','pluginExports')}
    if banks:dirs['banks']=str(banks.resolve())
    write_json(locations,{'format':'CyDAW Locations','version':1,'folders':dirs})
    # Same empty session + device setup for scripts which restore preferences.
    device=ET.Element('DEVICESETUP',{'deviceType':'ASIO','audioOutputDeviceName':'ASIO Fireface USB',
        'audioInputDeviceName':'','audioDeviceRate':'48000','audioDeviceBufferSize':'1024',
        'audioDeviceInChans':'0','audioDeviceOutChans':'11','useDefaultInputChannels':'0','useDefaultOutputChannels':'0'})
    for suffix in ('layout','processing'):
        props=ET.Element('PROPERTIES')
        ET.SubElement(props,'VALUE',{'name':'audioDeviceState','val':ET.tostring(device,encoding='unicode')})
        ET.ElementTree(props).write(settings/f'CyDAW.{suffix}.settings',encoding='utf-8',xml_declaration=True)
    env=dict(os.environ,APPDATA=str(roaming),LOCALAPPDATA=str(local),CYDAW_BENCH_PROFILE=str(roaming),
        CYDAW_LOCATIONS_FILE=str(locations),CYDAW_NO_RECOVERY='1',CYDAW_CONTROL_TOKEN='',
        CYDAW_DJ_COLLECTION_FILE=str(folder/'dj.json'),CYDAW_DJ_PROCESSING_FILE=str(folder/'dj-processing.json'),
        CYDAW_VIRTUAL_IO_FILE=str(folder/'virtual-io.json'),CYDAW_SINGLE_VIEWPORT='1',
        CYDAW_SAMPLES_SETTINGS=str(settings/'CyDAW.samples.settings'))
    return env

class Session:
    def __init__(self,exe,folder,banks,port):
        self.port=port;self.folder=folder;self.exe=exe
        env=profile(folder,banks)
        self.log=open(folder/'app.log','w',encoding='utf-8')
        start=time.perf_counter()
        self.process=subprocess.Popen([str(exe),'--silent',f'--control-port={port}',
            '--window-size=1500x900','--language=en'],env=env,stdout=self.log,stderr=subprocess.STDOUT)
        try:
            while time.perf_counter()-start<60:
                if self.process.poll() is not None:raise RuntimeError('Application exited during startup')
                try:
                    self.q(cmd='ping');break
                except (OSError,ValueError):time.sleep(.05)
            else:raise TimeoutError('Startup')
            self.startup=time.perf_counter()-start
            assert self.q(cmd='status')['speakersSilenced']
            self.q(cmd='device_channels',inputs=0,outputs=2)
            self.audio=self.q(cmd='set_audio_device',type='ASIO',output='ASIO Fireface USB',input='',sampleRate=48000,bufferSize=1024)
            assert self.audio['type']=='ASIO' and self.audio['sampleRate']==48000 and self.audio['bufferSize']==1024,self.audio
            assert len(self.audio['inputChannels'])==0 and len(self.audio['outputChannels'])==2,self.audio
        except BaseException:
            self.close();raise
    def q(self,**data):
        with socket.create_connection(('127.0.0.1',self.port),timeout=10) as s:
            s.settimeout(180);s.sendall((json.dumps(data)+'\n').encode())
            v=json.loads(s.makefile('rb').readline())
        if not v.get('ok'):raise RuntimeError(str(v))
        return v.get('result',{})
    def wait(self,fn,timeout=30):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            value=fn()
            if value:return value
            time.sleep(.05)
        raise TimeoutError('Condition not reached')
    def close(self):
        start=time.perf_counter();forced=False
        try:self.q(cmd='quit');self.process.wait(timeout=20)
        except Exception:
            if self.process.poll() is None:
                forced=True
                # Only the test-owned process tree, never an unrelated app.
                subprocess.run(['taskkill','/PID',str(self.process.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                self.process.wait(timeout=10)
        self.log.close()
        return dict(ms=(time.perf_counter()-start)*1000,forced=forced)
    def reset(self):
        self.q(cmd='transport',action='stop');self.q(cmd='app_mode',mode='studio');self.q(cmd='new_project')
        self.q(cmd='set_master_gain',gainDb=-18,muted=False)
    def measure(self,seconds=6):
        time.sleep(1.25)
        first=process_sample(self.process.pid);last=first;peak=0;latencies=[];states=[]
        assert self.process.pid in first, 'Cannot read the measured process CPU/memory'
        start=time.perf_counter()
        while time.perf_counter()-start<seconds:
            t=time.perf_counter();state=self.q(cmd='status');latencies.append((time.perf_counter()-t)*1000)
            states.append(state);last=process_sample(self.process.pid)
            peak=max(peak,sum(x['private'] for x in last.values()))
            time.sleep(.2)
        elapsed=time.perf_counter()-start
        assert self.process.pid in last and peak>0, 'Invalid process samples'
        cpu=sum(max(0,v['cpu']-first.get(pid,v)['cpu']) for pid,v in last.items())
        return {'seconds':elapsed,'cpuCorePercent':cpu/elapsed*100,'cpuMachinePercent':cpu/elapsed*100/os.cpu_count(),
            'peakPrivateMiB':peak/1048576,'requestMedianMs':statistics.median(latencies),
            'requestP95Ms':float(np.percentile(latencies,95)),'requestMaxMs':max(latencies),
            'positionDelta':states[-1].get('position',0)-states[0].get('position',0),
            'lastMeters':states[-1].get('meters',[]),'processes':len(last),
            'rootPid':self.process.pid,'cpuSecondsByPid':{str(pid):max(0,v['cpu']-first.get(pid,v)['cpu']) for pid,v in last.items()}}
    def timed(self,command,repeats=5):
        self.q(**command) # untimed warmup, including kernel initialization
        rows=[]
        for _ in range(repeats):
            t=time.perf_counter();result=self.q(**command);rows.append({'ms':(time.perf_counter()-t)*1000,'result':result})
        return rows

def assets(folder):
    folder.mkdir(parents=True,exist_ok=True)
    for name,seconds in (('long',12),('reference',3)):
        file=folder/(name+'.wav')
        if not file.exists():
            t=np.arange(48000*seconds,dtype=np.float64)/48000
            left=.15*np.sin(t*math.tau*220)+.08*np.sin(t*math.tau*731)
            right=.14*np.sin(t*math.tau*330)+.07*np.sin(t*math.tau*997)
            data=np.stack((left,right),axis=1)
            data[:480]=0;data[-480:]=0
            with wave.open(str(file),'wb') as f:
                f.setparams((2,2,48000,0,'NONE','PCM'));f.writeframes(np.round(data*32767).astype('<i2').tobytes())
    video=folder/'video.mp4'
    if not video.exists():
        import imageio_ffmpeg
        subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(),'-y','-loglevel','error','-f','lavfi','-i',
            'testsrc2=size=1280x720:rate=24:duration=12','-c:v','libx264','-pix_fmt','yuv420p',str(video)],check=True)
    return folder

def performance(args):
    asset=assets(args.output/'assets');rows=[]
    for iteration in range(args.rounds):
        order=('JUCE','CyAudioCore') if iteration%2==0 else ('CyAudioCore','JUCE')
        for label in order:
            folder=args.output/'performance'/f'{iteration+1}-{label}'
            s=Session(getattr(args,label.lower()),folder,args.banks,48191)
            row={'label':label,'round':iteration+1,'startupReadyMs':s.startup*1000,'audioSettings':s.audio,'cases':{}}
            rows.append(row)
            try:
                time.sleep(3)
                s.reset();row['cases']['idle']=s.measure()
                # Actual source graph, including its background readers and mixer.
                for count,effects,name in ((32,False,'audio32'),(16,True,'effects16')):
                    s.reset()
                    for n in range(count):
                        track=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['name']
                        if effects:
                            for key in ('comp-pro','chorus','reverb-pro'):s.q(cmd='insert_device',track=track,key=key)
                    s.q(cmd='transport',action='play',position=.1)
                    row['cases'][name]=s.measure()
                    assert row['cases'][name]['positionDelta']>4,'Timeline did not advance'
                    s.q(cmd='transport',action='stop')
                    if count==32:
                        project=folder/'32-tracks.cydaw'
                        row['projectSave']=s.timed(dict(cmd='save_project',path=str(project)),3)
                        t=time.perf_counter();s.q(cmd='open_project',path=str(project));s.wait(lambda:not s.q(cmd='status').get('opening',False))
                        row['projectOpenMs']=(time.perf_counter()-t)*1000
                # Same deterministic in-memory DSP contract, independent of drivers.
                s.reset();track=s.q(cmd='add_midi_track',name='Offline',instrument='ambre-instrument')['name']
                row['offlineSynth']=s.timed(dict(cmd='synth',track=track,init=True,notes=[48,55,60,64,67],seconds=2,tail=1))
                s.reset();track=s.q(cmd='add_audio_track',file=str(asset/'reference.wav'))['name']
                for key in ('comp-pro','chorus','reverb-pro'):s.q(cmd='insert_device',track=track,key=key)
                row['offlineEffect']=s.timed(dict(cmd='effect_chain_check',track=track,hz=440,amplitude=.25,seconds=3))
                # Real-time voice allocator, not only a stand-alone synth class.
                s.reset();s.q(cmd='set_tempo',bpm=120)
                for n in range(8):
                    track=s.q(cmd='add_midi_track',name=f'Voices {n}',instrument='ambre-instrument')['name']
                    s.q(cmd='add_midi_clip',track=track,name='Held',startBeats=0,lengthBeats=32)
                    s.q(cmd='add_notes',track=track,clip='Held',notes=[dict(note=48+i,start=0,length=30,velocity=.5) for i in range(8)])
                s.q(cmd='transport',action='play',position=0)
                row['cases']['voices64']=s.measure();s.q(cmd='transport',action='stop')
                # Same third-party VST3 binary through each version's actual worker.
                s.reset();start=time.perf_counter()
                for n in range(8):
                    track=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['name']
                    s.q(cmd='add_plugin',track=track,file=str(args.vst3.resolve()))
                row['pluginLoad8Ms']=(time.perf_counter()-start)*1000
                s.q(cmd='transport',action='play',position=.1)
                row['cases']['plugins8']=s.measure();assert row['cases']['plugins8']['processes']>=9, 'Eight plugin workers must be running'
                s.q(cmd='transport',action='stop')
                # Four actual decks, including key-lock and waveform work.
                s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
                for deck in 'ABCD':s.q(cmd='dj',action='load',deck=deck,file=str(asset/'long.wav'))
                s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
                for deck in 'ABCD':
                    s.q(cmd='dj',action='channel',deck=deck,fader=.7)
                    s.q(cmd='dj',action='play',deck=deck,on=True)
                row['cases']['dj4']=s.measure()
                row['djState']=s.q(cmd='dj')
                for deck in 'ABCD':s.q(cmd='dj',action='play',deck=deck,on=False)
                # Destructive processing + actual decoder/writer; compare PCM later.
                s.q(cmd='app_mode',mode='foundry');s.q(cmd='foundry',action='open',file=str(asset/'long.wav'))
                s.q(cmd='foundry',action='play');row['cases']['foundry']=s.measure();row['foundryState']=s.q(cmd='foundry')
                assert row['foundryState']['playing'] and row['foundryState']['position']>0,row['foundryState']
                s.q(cmd='foundry',action='stop')
                row['processing']={}
                for effect in ('Remove Hum','Tape Echo','Room Reverb'):
                    out=folder/(effect.replace(' ','-')+'.wav')
                    row['processing'][effect]=s.timed(dict(cmd='processing',render=effect,file=str(asset/'reference.wav'),out=str(out)),3)
                # Native 720p preview/filmstrip, linked timecode and seeks.
                s.q(cmd='app_mode',mode='cinema');s.q(cmd='cinema',action='new',name='Comparison')
                s.q(cmd='cinema',action='video',file=str(asset/'video.mp4'))
                def cinema_tab():
                    state=s.q(cmd='cinema');return state['tabs'][state['current']]
                s.wait(lambda:cinema_tab().get('thumbnailCount',0)>0)
                row['cinemaSeek']=[]
                for at in (.1,2.5,.5,3.5,1,2,.25,3)*2:
                    t=time.perf_counter();s.q(cmd='cinema',action='cursor',at=at,seconds=True)
                    state=s.q(cmd='cinema');row['cinemaSeek'].append((time.perf_counter()-t)*1000)
                s.q(cmd='cinema',action='import',track=1,file=str(asset/'long.wav'),at=0)
                s.q(cmd='cinema',action='cursor',at=.1,seconds=True);s.q(cmd='cinema',action='play')
                row['cases']['cinema']=s.measure();row['cinemaState']=s.q(cmd='cinema')
                assert row['cinemaState']['playing'] and row['cinemaState']['position']>0,row['cinemaState']
                s.q(cmd='cinema',action='stop')
                # Deterministic real callback export, gain only; no random DSP.
                s.reset();s.q(cmd='add_audio_track',file=str(asset/'reference.wav'));s.q(cmd='set_master_gain',gainDb=-6,muted=False)
                out=folder/'studio.wav';t=time.perf_counter();s.q(cmd='export_mix',path=str(out))
                s.wait(lambda:not s.q(cmd='status').get('exporting'),30)
                row['studioExportMs']=(time.perf_counter()-t)*1000
                row['result']='passed'
            except Exception:
                row['result']='failed';row['error']=traceback.format_exc();print(label,row['error'],flush=True)
            finally:
                row['shutdown']=s.close();write_json(args.output/'performance.json',rows)
            print('Performance',iteration+1,label,row['result'],flush=True)
    return rows

def functional(args):
    panel=json.loads(args.panel.read_text(encoding='utf-8-sig'))
    rows=[]
    if args.resume and (args.output/'functional.json').exists():rows=json.loads((args.output/'functional.json').read_text(encoding='utf-8'))
    tests=[t for t in panel['tests'] if '--exe' in t.get('command',[])]
    for i,test in enumerate(tests):
        order=('JUCE','CyAudioCore') if i%2==0 else ('CyAudioCore','JUCE')
        for label in order:
            if any(r['name']==test['name'] and r['label']==label for r in rows):continue
            folder=args.output/'functional'/label/test['name'];env=profile(folder,args.banks)
            env['CYDAW_REQUIRE_FACTORY_SAMPLES']='1' if args.banks else '0'
            command=list(test['command']);command[0]=sys.executable
            command[command.index('--exe')+1]=str(getattr(args,label.lower()))
            if '--output' in command:command[command.index('--output')+1]=str(folder/'artifacts')
            # Retain exact shared test code/arguments, only swap the product executable.
            start=time.perf_counter()
            with open(folder/'test.log','w',encoding='utf-8') as log:
                child=subprocess.Popen(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT)
                try:code=child.wait(timeout=420)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                    child.wait(timeout=15);code=-999
            text=(folder/'test.log').read_text(encoding='utf-8',errors='replace')
            row=dict(name=test['name'],label=label,exitCode=code,seconds=time.perf_counter()-start,
                passedAssertions=sum(l.startswith('ok ') or l.startswith('ok\t') for l in text.splitlines()),
                failedLines=[l for l in text.splitlines() if l.startswith(('FAIL','Traceback','AssertionError','RuntimeError'))],
                skipped=code==77,command=command,log=str((folder/'test.log').relative_to(ROOT)))
            rows.append(row);write_json(args.output/'functional.json',rows)
            print('Functional',test['name'],label,code,round(row['seconds'],2),flush=True)
    return rows

def pcm(path):
    with wave.open(str(path),'rb') as f:
        width=f.getsampwidth();channels=f.getnchannels();rate=f.getframerate();raw=f.readframes(f.getnframes())
    if width==3:
        b=np.frombuffer(raw,dtype=np.uint8).reshape(-1,3).astype(np.int32)
        values=b[:,0]|(b[:,1]<<8)|(b[:,2]<<16);values=(values^0x800000)-0x800000
        values=values.astype(np.float64)/8388608
    elif width in (2,4):values=np.frombuffer(raw,dtype='<i'+str(width)).astype(np.float64)/(2**(width*8-1))
    else:raise ValueError(('Unsupported width',width))
    return rate,values.reshape(-1,channels)

def audio_comparison(args):
    results=[]
    for iteration in range(1,args.rounds+1):
        a=args.output/'performance'/f'{iteration}-JUCE';b=args.output/'performance'/f'{iteration}-CyAudioCore'
        for name in ('studio.wav','Remove-Hum.wav','Tape-Echo.wav','Room-Reverb.wav'):
            if not (a/name).exists() or not (b/name).exists():continue
            ra,x=pcm(a/name);rb,y=pcm(b/name)
            row=dict(round=iteration,file=name,rates=[ra,rb],shapes=[list(x.shape),list(y.shape)])
            if ra==rb and x.shape==y.shape:
                error=x-y;peak=float(np.max(np.abs(error)));rms=float(np.sqrt(np.mean(error**2)))
                centered=(x-x.mean(axis=0))-(y-y.mean(axis=0))
                row.update(dcMeanJUCE=x.mean(axis=0).tolist(),dcMeanCore=y.mean(axis=0).tolist(),meanCenteredMaxAbs=float(np.max(np.abs(centered))),meanCenteredErrorRmsDb=20*math.log10(max(float(np.sqrt(np.mean(centered**2))),1e-15)),maxAbs=peak,errorRmsDb=20*math.log10(max(rms,1e-15)),
                    signalRmsDb=[20*math.log10(max(float(np.sqrt(np.mean(v*v))),1e-15)) for v in (x,y)],
                    correlation=float(np.corrcoef(x.ravel(),y.ravel())[0,1]),equalPcm=bool(np.array_equal(x,y)))
            results.append(row)
    write_json(args.output/'audio.json',results);return results

def main():
    p=argparse.ArgumentParser();p.add_argument('--juce',type=Path,required=True);p.add_argument('--cyaudiocore',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--panel',type=Path);p.add_argument('--banks',type=Path)
    p.add_argument('--vst3',type=Path,default=ROOT/'build/cyaudio-desktop/CyDAWTestGain_artefacts/Release/VST3/CyDAW Test Gain.vst3');p.add_argument('--rounds',type=int,default=3);p.add_argument('--mode',choices=['functional','performance','audio','all'],default='all');p.add_argument('--resume',action='store_true')
    args=p.parse_args();args.output=args.output.resolve();args.output.mkdir(parents=True,exist_ok=True)
    args.juce=args.juce.resolve();args.cyaudiocore=args.cyaudiocore.resolve()
    if args.mode in ('functional','all') and not args.panel:p.error('--panel is required for the shared functional tests')
    if args.rounds<3:p.error('At least three alternating paired rounds are required')
    write_json(args.output/'environment.json',dict(platform=platform.platform(),python=sys.version,logicalCpus=os.cpu_count(),
        processor=platform.processor(),executables={k:dict(path=str(getattr(args,k)),sha256=hashlib.sha256(getattr(args,k).read_bytes()).hexdigest(),bytes=getattr(args,k).stat().st_size) for k in ('juce','cyaudiocore')},
        cpuConvention='100% = one logical CPU fully busy; host + descendant workers; peak sampled private committed bytes',
        methodology='Alternating paired sequential runs, Release x64 LTO off, same ASIO Fireface 48kHz/1024, warmup excluded, silent hidden UI at 1500x900; OS filesystem cache not flushed. API latency is not visible-window FPS. Functional wall times include Python/assertions/sleeps and are not DSP speed.',
        baselineProfilePatch='tests/fixtures/juce-benchmark-profile.patch; only absolute preferences paths, no audio or DSP edits'))
    if args.mode in ('functional','all'):functional(args)
    if args.mode in ('performance','all'):performance(args)
    if args.mode in ('audio','performance','all'):audio_comparison(args)
if __name__=='__main__':main()
