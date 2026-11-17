"""Matched real audio-engine comparison with ImGui drawing suppressed on both builds.
Instrumented localisation and non-audio-instrumented controls are separate.
"""
import argparse,hashlib,json,time
from pathlib import Path
from cyaudio_cpu_diagnostic import CpuSession,scope_deltas,descendants,image
from cyaudio_comparison import ROOT,assets,write_json

p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--tag',default='engine');p.add_argument('--rounds',type=int,default=3);p.add_argument('--seconds',type=float,default=4);p.add_argument('--controls',action='store_true');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output')
m=json.loads((out/'build-manifest.json').read_text(encoding='utf-8'));asset=assets(out/'assets');rows=[];result=out/(a.tag+'-results.json');assert not result.exists()
variants=m['controls' if a.controls else 'variants'];plugin=ROOT/'build/cyaudio-desktop/CyDAWTestGain_artefacts/Release/VST3/CyDAW Test Gain.vst3'

def fingerprints():
 files=[out/'build-manifest.json',*sorted((out/'probes').glob('*'))]
 for label,v in m['variants'].items():
  source=ROOT if label=='native' else ROOT/'output/juce-core-comparison/juce-source'
  files.extend(source/f for f in v['sourceHashes']);files.extend(sorted((out/label/'sources').glob('*.cpp')))
 for v in [*m['variants'].values(),*m['controls'].values()]:files.append(Path(v['exe']))
 h=hashlib.sha256()
 for f in files:h.update(str(f).encode());h.update(f.read_bytes())
 return h.hexdigest()
initial=fingerprints()
for iteration in range(a.rounds):
 order=['JUCE','native'] if iteration%2==0 else ['native','JUCE']
 for label in order:
  folder=out/a.tag/f'{iteration+1}-{label}';s=CpuSession(Path(variants[label]['exe']),folder,48231,'asio',True,'off');r=dict(label=label,round=iteration+1,instrumented=not a.controls,audio=s.audio,cases={},topologies={});rows.append(r)
  try:
   assert all(not d['enabled'] for d in s.q(cmd='midi_inputs')['devices'])
   for case,n in [('audio32',32),('effects16',16)]:
    s.reset()
    for _ in range(n):
     index=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['index']
     if case=='effects16':
      for key in ['comp-pro','chorus','reverb-pro']:s.q(cmd='insert_device',track=index,key=key)
    topology=s.q(cmd='status')['audioTracks'];assert len(topology)==n
    assert all(not t['plugins'] for t in topology)
    if case=='effects16':assert all(set(t['devices'])=={'comp-pro','chorus','reverb-pro'} for t in topology)
    r['topologies'][case]=topology;s.q(cmd='transport',action='play',position=.1);v=s.measure(a.seconds);assert v['positionDelta']>a.seconds*.7
    assert max(t['peakDb'] for t in v['lastStatus']['meters'])>-80;r['cases'][case]=v;s.q(cmd='transport',action='stop')
   s.reset()
   for _ in range(8):
    index=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['index'];s.q(cmd='add_plugin',track=index,file=str(plugin))
   topology=s.q(cmd='status')['audioTracks'];assert len(topology)==8 and all(len(t['plugins'])==1 for t in topology),topology
   s.wait(lambda:sum(image(pid)=='CyDAWPluginWorker.exe' for pid in descendants(s.process.pid))==8);r['topologies']['plugins8']=topology
   r['cases']['plugins8Stopped']=s.measure(a.seconds);assert sum(v['image']=='CyDAWPluginWorker.exe' for v in r['cases']['plugins8Stopped']['processes'])==8
   s.q(cmd='transport',action='play',position=.1);r['cases']['plugins8Playing']=s.measure(a.seconds);assert r['cases']['plugins8Playing']['positionDelta']>a.seconds*.7;s.q(cmd='transport',action='stop')
   s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
   s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
   for d in 'ABCD':s.q(cmd='dj',action='load',deck=d,file=str(asset/'long.wav'))
   s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
   for d in 'ABCD':s.q(cmd='dj',action='channel',deck=d,fader=.7);s.q(cmd='dj',action='play',deck=d,on=True)
   r['cases']['dj4']=s.measure(a.seconds);r['djState']=s.q(cmd='dj');assert all(d['playing'] for d in r['djState']['decks']);assert len(r['cases']['dj4']['processes'])==1
   for d in 'ABCD':s.q(cmd='dj',action='play',deck=d,on=False)
  finally:
   r['shutdown']=s.close();write_json(result,rows);assert not r['shutdown']['forced'];assert fingerprints()==initial
   for case,v in r['cases'].items():
    v['scopes']=scope_deltas(folder/'scopes.csv',v,m['sites'])
    assert not any(z['name']=='ImGuiHost::frame' for z in v['scopes']),'Drawing not disabled'
    if not a.controls:
     master=next(z for z in v['scopes'] if z['name']=='VirtualBusRouter::getNextAudioBlock');assert 42<master['callsPerSecond']<52,(case,master)
     tids={z['tid'] for z in v['scopes'] if z['name']=='VirtualBusRouter::getNextAudioBlock'}
     v['audioThreads']=[t for t in v['threads'] if t['tid'] in tids]
   write_json(result,rows)
  print(iteration+1,label,{k:dict(cpu=round(v['parentCpu'],2),audioUs=round(next((z['meanInclusiveUs'] for z in v['scopes'] if z['name']=='VirtualBusRouter::getNextAudioBlock'),0),2)) for k,v in r['cases'].items()},flush=True)
write_json(out/(a.tag+'-verification.json'),dict(fingerprint=initial,stable=fingerprints()==initial,allOriginalInputsStable=all(hashlib.sha256(Path(f).read_bytes()).hexdigest()==h for f,h in m['inputs'].items()),executables=variants))
