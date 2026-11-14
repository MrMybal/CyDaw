"""Current CyAudioCore versus frozen JUCE: real ASIO render, drawing suppressed.
100% CPU is one logical core. QA probe counts only the full app render boundary.
"""
import argparse,hashlib,json,statistics,time
from pathlib import Path
from cyaudio_cpu_diagnostic import CpuSession,scope_deltas,descendants,image
from cyaudio_comparison import ROOT,assets,write_json
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--rounds',type=int,default=3);p.add_argument('--seconds',type=float,default=5)
a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output') and a.rounds>=3 and a.seconds>=4
m=json.loads((out/'build-manifest.json').read_text(encoding='utf-8'));result=out/'results.json';assert not result.exists()
asset=assets(out/'assets');plugin=ROOT/'build/cyaudio-desktop/CyDAWTestGain_artefacts/Release/VST3/CyDAW Test Gain.vst3';assert plugin.is_dir()
files={Path(f):h for f,h in m['inputs'].items()}
for v in m['variants'].values():
 files[Path(v['exe'])]=v['sha256']
 files.update({Path(f):h for f,h in v['sourceHashes'].items()});files.update({Path(f):h for f,h in v['generatedHashes'].items()})
files[plugin/'Contents/x86_64-win/CyDAW Test Gain.vst3']=hashlib.sha256((plugin/'Contents/x86_64-win/CyDAW Test Gain.vst3').read_bytes()).hexdigest()
def stable():return all(hashlib.sha256(f.read_bytes()).hexdigest()==h for f,h in files.items())
assert stable();rows=[]
for iteration in range(a.rounds):
 order=['JUCE','CyAudioCore'] if iteration%2==0 else ['CyAudioCore','JUCE']
 for label in order:
  folder=out/'runs'/f'{iteration+1}-{label}'
  s=CpuSession(Path(m['variants'][label]['exe']),folder,48239,'asio',True,'off')
  r=dict(label=label,round=iteration+1,audio=s.audio,cases={},topologies={});rows.append(r)
  try:
   assert all(not x['enabled'] for x in s.q(cmd='midi_inputs')['devices'])
   for case,count,effects in [('audio32',32,[]),('effects16',16,['comp-pro','chorus','reverb-pro']),('compressor16',16,['compressor'])]:
    s.reset()
    for _ in range(count):
     index=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['index']
     for key in effects:s.q(cmd='insert_device',track=index,key=key)
    topology=s.q(cmd='status')['audioTracks'];assert len(topology)==count and all(not t['plugins'] for t in topology)
    assert all(set(t['devices'])==set(effects) for t in topology),(case,topology)
    r['topologies'][case]=topology
    s.q(cmd='transport',action='play',position=.1);v=s.measure(a.seconds);r['cases'][case]=v
    assert v['positionDelta']>a.seconds*.7 and max(t['peakDb'] for t in v['lastStatus']['meters'])>-80
    s.q(cmd='transport',action='stop')
   s.reset()
   for _ in range(8):
    index=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['index'];s.q(cmd='add_plugin',track=index,file=str(plugin))
   topology=s.q(cmd='status')['audioTracks'];assert len(topology)==8 and all(len(t['plugins'])==1 for t in topology)
   s.wait(lambda:sum(image(pid)=='CyDAWPluginWorker.exe' for pid in descendants(s.process.pid))==8)
   r['topologies']['plugins8']=topology
   r['cases']['plugins8Stopped']=s.measure(a.seconds)
   assert sum(v['image']=='CyDAWPluginWorker.exe' for v in r['cases']['plugins8Stopped']['processes'])==8
   s.q(cmd='transport',action='play',position=.1);r['cases']['plugins8Playing']=s.measure(a.seconds)
   assert r['cases']['plugins8Playing']['positionDelta']>a.seconds*.7
   s.q(cmd='transport',action='stop');s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
   s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
   for deck in 'ABCD':s.q(cmd='dj',action='load',deck=deck,file=str(asset/'long.wav'))
   s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
   for deck in 'ABCD':s.q(cmd='dj',action='channel',deck=deck,fader=.7);s.q(cmd='dj',action='play',deck=deck,on=True)
   r['cases']['dj4']=s.measure(a.seconds);assert all(d['playing'] for d in s.q(cmd='dj')['decks'])
   assert len(r['cases']['dj4']['processes'])==1
   for deck in 'ABCD':s.q(cmd='dj',action='play',deck=deck,on=False)
  finally:
   r['shutdown']=s.close();write_json(result,rows)
   assert not r['shutdown']['forced']
   for case,v in r['cases'].items():
    v['scopes']=scope_deltas(folder/'scopes.csv',v,m['sites'])
    assert not any(z['name']=='ImGuiHost::frame' for z in v['scopes'])
    root=next(z for z in v['scopes'] if z['name']=='VirtualBusRouter::getNextAudioBlock')
    assert 42<root['callsPerSecond']<52,(case,root)
    v['renderUs']=root['meanInclusiveUs'];v['renderCallsPerSecond']=root['callsPerSecond']
    v['audioThreads']=[t for t in v['threads'] if t['tid']==root['tid']]
    v['audioMillionCyclesPerBlock']=sum(t['cycles'] for t in v['audioThreads'])/v['seconds']/root['callsPerSecond']/1e6
   write_json(result,rows);assert stable()
  print(iteration+1,label,{k:dict(renderUs=round(v['renderUs'],2),parentCpu=round(v['parentCpu'],2),workerCpu=round(v['workerCpu'],2)) for k,v in r['cases'].items()},flush=True)
summary={}
for case in rows[0]['cases']:
 summary[case]={}
 for label in m['variants']:
  samples=[r['cases'][case] for r in rows if r['label']==label]
  summary[case][label]={metric:dict(median=statistics.median(v[metric] for v in samples),min=min(v[metric] for v in samples),max=max(v[metric] for v in samples),values=[v[metric] for v in samples]) for metric in ['renderUs','renderCallsPerSecond','parentCpu','workerCpu','audioMillionCyclesPerBlock','systemOtherCorePercent']}
 summary[case]['renderDeltaPercent']=(summary[case]['CyAudioCore']['renderUs']['median']/summary[case]['JUCE']['renderUs']['median']-1)*100
write_json(out/'summary.json',dict(summary=summary,sourceRevision=m['sourceRevision'],executables=m['variants'],allInputsStable=stable(),protocol=dict(rounds=a.rounds,warmupSeconds=2,measureSeconds=a.seconds,mode='real ASIO Fireface USB, 48000 Hz, 1024 frames, zero input, two silent outputs',ui='drawing suppressed in both QA copies',cpu='100%=one logical core',topologies='numeric indices, verified effects per track and 8 actual plugin workers',scope='one full-render root timer, no algorithm changes; mean per block, not p95 or physical latency',knownIssues='Cinema DC and CUE ASIO remain open; not tested or masked by this campaign')))
print(json.dumps(summary,indent=2),flush=True)
