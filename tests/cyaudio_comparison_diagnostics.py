"""Targeted follow-up to paired performance findings; never patches audio code."""
import argparse,ctypes as C,json,math,os,pstats,socket,statistics,subprocess,sys,time
from ctypes import wintypes as W
from pathlib import Path
import xml.etree.ElementTree as ET
import numpy as np
from cyaudio_comparison import Session,profile,pcm,write_json,descendants,K

p=argparse.ArgumentParser();p.add_argument('--juce',type=Path,required=True);p.add_argument('--cyaudiocore',type=Path,required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--panel',type=Path,required=True);p.add_argument('--banks',type=Path,required=True)
a=p.parse_args();a.output=a.output.resolve();a.juce=a.juce.resolve();a.cyaudiocore=a.cyaudiocore.resolve()
asset=a.output/'assets';diag=a.output/'diagnostics';root=Path(__file__).resolve().parents[1]
results={};write=lambda:write_json(diag/'checks.json',results)
K.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD)]
def images(row):
    groups={}
    for pid,seconds in row['cpuSecondsByPid'].items():
        h=K.OpenProcess(0x1000,False,int(pid));buffer=C.create_unicode_buffer(32768);size=W.DWORD(32768)
        try:
            name=Path(buffer.value).name if h and K.QueryFullProcessImageNameW(h,0,buffer,C.byref(size)) else 'unknown'
            groups[name]=groups.get(name,0)+seconds/row['seconds']*100
        finally:
            if h:K.CloseHandle(h)
    row['cpuCorePercentByImage']=groups
    return row

def export(s,path):
    s.reset();s.q(cmd='add_audio_track',file=str(asset/'reference.wav'));s.q(cmd='set_master_gain',gainDb=-6,muted=False)
    s.q(cmd='export_mix',path=str(path));s.wait(lambda:not s.q(cmd='status').get('exporting'),30)
    rate,v=pcm(path);r,reference=pcm(asset/'reference.wav');expected=np.zeros(v.shape);expected[:len(reference)]=reference*10**(-6/20)
    return dict(file=str(path.relative_to(root)),rate=rate,frames=len(v),mean=v.mean(axis=0).tolist(),tailPeak=float(abs(v[-2000:]).max()),maxErrorFromInput=float(abs(v-expected).max()))

for label in ('JUCE','CyAudioCore'):
    folder=diag/label/'workers';s=Session(getattr(a,label.lower()),folder,a.banks,48192)
    result={};results[label]=result
    try:
        result['freshStudio']=export(s,folder/'fresh-studio.wav')
        s.reset();result['idle0']=images(s.measure(4))
        for n in range(8):
            track=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['name']
            s.q(cmd='add_plugin',track=track,file=str(root/'build/cyaudio-desktop/CyDAWTestGain_artefacts/Release/VST3/CyDAW Test Gain.vst3'))
        result['idle8']=images(s.measure(4))
        s.q(cmd='transport',action='play',position=.1);result['playing8']=images(s.measure(4));s.q(cmd='transport',action='stop')
        s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
        s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
        for deck in 'ABCD':s.q(cmd='dj',action='load',deck=deck,file=str(asset/'long.wav'))
        s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
        for deck in 'ABCD':
            s.q(cmd='dj',action='channel',deck=deck,fader=.7);s.q(cmd='dj',action='play',deck=deck,on=True)
        result['djWithoutWorkers']=images(s.measure(4));result['djState']=s.q(cmd='dj')
        for deck in 'ABCD':s.q(cmd='dj',action='play',deck=deck,on=False)
        s.q(cmd='app_mode',mode='cinema');s.q(cmd='cinema',action='new',name='DC follow-up')
        s.q(cmd='cinema',action='video',file=str(asset/'video.mp4'));s.q(cmd='cinema',action='import',track=1,file=str(asset/'long.wav'),at=0)
        s.q(cmd='cinema',action='cursor',at=.1,seconds=True);s.q(cmd='cinema',action='play');time.sleep(1.5)
        result['cinemaPlaying']=s.q(cmd='cinema');assert result['cinemaPlaying']['playing']
        s.q(cmd='cinema',action='stop');result['afterCinemaStudio']=export(s,folder/'after-cinema.wav')
    finally:
        result['shutdown']=s.close();write()
    print('Worker/DC diagnostic',label,'completed',flush=True)

# Same original CUE assertion, matched smaller WASAPI quantum: no tolerance change.
for label in ('JUCE','CyAudioCore'):
    folder=diag/label/'cue-wasapi';env=profile(folder,a.banks)
    for suffix in ('layout','processing'):
        file=Path(env['APPDATA'])/'CyDAW'/f'CyDAW.{suffix}.settings';tree=ET.parse(file);node=tree.find('./VALUE[@name="audioDeviceState"]')
        device=ET.fromstring(node.attrib['val']);device.set('deviceType','Windows Audio');device.set('audioOutputDeviceName','Haut-parleurs (RME Fireface UCX)');device.set('audioDeviceBufferSize','480')
        node.set('val',ET.tostring(device,encoding='unicode'));tree.write(file,encoding='utf-8',xml_declaration=True)
    command=[sys.executable,str(root/'tests/v2_dj_check.py'),'--exe',str(getattr(a,label.lower())),'--port','48198']
    with open(folder/'test.log','w',encoding='utf-8') as log:
        child=subprocess.Popen(command,env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT)
        configuration=None
        for _ in range(200):
            if child.poll() is not None:break
            try:
                with socket.create_connection(('127.0.0.1',48198),timeout=.5) as connection:
                    connection.sendall(b'{"cmd":"audio_settings"}\n');configuration=json.loads(connection.makefile('rb').readline())['result'];break
            except (OSError,ValueError,KeyError):time.sleep(.1)
        try:code=child.wait(timeout=240)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);child.wait();code=-999
    assert configuration and configuration['type']=='Windows Audio' and configuration['bufferSize']==480,configuration
    results[label]['cueWASAPI480']=dict(exitCode=code,audioSettings=configuration,log=str((folder/'test.log').relative_to(root)));write()
    print('Original DJ / WASAPI 480',label,code,flush=True)

panel=json.loads(a.panel.read_text(encoding='utf-8-sig'))
for iteration in (1,2):
    for testname in ('cydaw2_sample_search_check','cydaw2_tempo_audition_check'):
        test=next(t for t in panel['tests'] if t['name']==testname)
        order=('JUCE','CyAudioCore') if iteration==1 else ('CyAudioCore','JUCE')
        for label in order:
            folder=diag/label/f'{testname}-{iteration}';env=profile(folder,a.banks)
            command=list(test['command']);command[0]=sys.executable;command[command.index('--exe')+1]=str(getattr(a,label.lower()))
            prof=folder/'trace.prof'
            if iteration==1:command=[sys.executable,'-m','cProfile','-o',str(prof),*command[1:]]
            start=time.perf_counter()
            with open(folder/'test.log','w',encoding='utf-8') as log:
                child=subprocess.Popen(command,env=env,cwd=root,stdout=log,stderr=subprocess.STDOUT)
                try:code=child.wait(timeout=300)
                except subprocess.TimeoutExpired:
                    subprocess.run(['taskkill','/PID',str(child.pid),'/T','/F'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);child.wait();code=-999
            row=dict(iteration=iteration,seconds=time.perf_counter()-start,exitCode=code,profiled=iteration==1,log=str((folder/'test.log').relative_to(root)))
            if prof.exists():
                stats=pstats.Stats(str(prof));row['timingBreakdown']=[]
                for (file,line,name),(primitive,calls,own,cumulative,callers) in stats.stats.items():
                    if (name in ('wait','_wait') and file.endswith('subprocess.py')) or (name=='request' and Path(file).name.startswith('v2_')) or (name=='rmtree' and file.endswith('shutil.py')):
                        row['timingBreakdown'].append(dict(file=Path(file).name,line=line,function=name,calls=calls,cumulativeSeconds=cumulative,ownSeconds=own))
            results[label].setdefault('repeats',{}).setdefault(testname,[]).append(row);write()
            print('Repeat',iteration,testname,label,code,round(row['seconds'],2),flush=True)
