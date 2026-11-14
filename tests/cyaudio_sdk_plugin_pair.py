"""SDK-update plugin/DJ comparison versus previous Core and frozen JUCE: real ASIO render, drawing suppressed.
100% CPU is one logical core. QA probe counts only the full app render boundary.
"""
import argparse,hashlib,json,statistics,time,ctypes as C
from pathlib import Path
from cyaudio_cpu_diagnostic import CpuSession,scope_deltas,descendants,image
from cyaudio_comparison import ROOT,assets,write_json,process_sample,K,Entry
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
def parents():
 h=K.CreateToolhelp32Snapshot(2,0);mapping={}
 try:
  entry=Entry();entry.size=C.sizeof(entry);ok=K.Process32FirstW(h,C.byref(entry))
  while ok:
   mapping[entry.pid]=entry.parent;ok=K.Process32NextW(h,C.byref(entry))
 finally:K.CloseHandle(h)
 return mapping

def measure(session,roles):
 v=session.measure(a.seconds)
 v['noiseAccepted']=v['systemOtherCorePercent']<=200
 v['bridgeCpu']=sum(p['cpuPercent'] for p in v['processes'] if roles.get(str(p['pid']))=='bridge')
 v['sdkCpu']=sum(p['cpuPercent'] for p in v['processes'] if roles.get(str(p['pid']))=='SDK')
 memory=process_sample(session.process.pid);v['privateMiB']=sum(p['private'] for p in memory.values())/1048576
 return v

assert stable();rows=[]
for iteration in range(a.rounds):
 order=list(m['variants']);order=order[iteration%len(order):]+order[:iteration%len(order)]
 for label in order:
  folder=out/'runs'/f'{iteration+1}-{label}'
  s=CpuSession(Path(m['variants'][label]['exe']),folder,48239,'asio',True,'off')
  r=dict(label=label,round=iteration+1,audio=s.audio,cases={},topologies={});rows.append(r)
  try:
   assert all(not x['enabled'] for x in s.q(cmd='midi_inputs')['devices'])
   s.reset()
   for _ in range(8):
    index=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['index'];s.q(cmd='add_plugin',track=index,file=str(plugin))
   topology=s.q(cmd='status')['audioTracks'];assert len(topology)==8 and all(len(t['plugins'])==1 for t in topology)
   expectedWorkers=16 if label=='CyAudioCore' else 8
   s.wait(lambda:sum(image(pid)=='CyDAWPluginWorker.exe' for pid in descendants(s.process.pid))==expectedWorkers)
   pp=parents();r['workerRoles']={str(pid):('bridge' if pp.get(pid)==s.process.pid else 'SDK') for pid in descendants(s.process.pid) if pid!=s.process.pid}
   r['topologies']['plugins8']=topology
   r['cases']['plugins8Stopped']=measure(s,r.get('workerRoles',{}))
   assert sum(v['image']=='CyDAWPluginWorker.exe' for v in r['cases']['plugins8Stopped']['processes'])==expectedWorkers
   initialMissed=sum(p['missedBlocks'] for t in s.q(cmd='status')['audioTracks'] for p in t['plugins'])
   s.q(cmd='transport',action='play',position=.1);r['cases']['plugins8Playing']=measure(s,r.get('workerRoles',{}))
   v=r['cases']['plugins8Playing'];assert v['positionDelta']>a.seconds*.7
   assert max(t['peakDb'] for t in v['lastStatus']['meters'])>-80
   v['missedBlocksSincePlay']=sum(p['missedBlocks'] for t in v['lastStatus']['audioTracks'] for p in t['plugins'])-initialMissed
   assert all(p['running'] for t in v['lastStatus']['audioTracks'] for p in t['plugins'])
   memory=process_sample(s.process.pid);v['privateMiB']=sum(p['private'] for p in memory.values())/1048576
   s.q(cmd='transport',action='stop');s.reset();s.wait(lambda:len(descendants(s.process.pid))==1)
   s.q(cmd='app_mode',mode='dj');s.q(cmd='dj',action='mixer',decks=4,autoGain=False)
   for deck in 'ABCD':s.q(cmd='dj',action='load',deck=deck,file=str(asset/'long.wav'))
   s.wait(lambda:all(d.get('analysed') for d in s.q(cmd='dj')['decks']),60)
   for deck in 'ABCD':s.q(cmd='dj',action='channel',deck=deck,fader=.7);s.q(cmd='dj',action='play',deck=deck,on=True)
   r['cases']['dj4']=measure(s,r.get('workerRoles',{}));assert all(d['playing'] for d in s.q(cmd='dj')['decks'])
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
  summary[case][label]={metric:dict(median=statistics.median(v[metric] for v in samples),min=min(v[metric] for v in samples),max=max(v[metric] for v in samples),values=[v[metric] for v in samples]) for metric in ['renderUs','renderCallsPerSecond','parentCpu','workerCpu','audioMillionCyclesPerBlock','systemOtherCorePercent','bridgeCpu','sdkCpu','privateMiB']}
 if 'Before' in m['variants']:summary[case]['migrationDeltaPercent']=(summary[case]['CyAudioCore']['renderUs']['median']/summary[case]['Before']['renderUs']['median']-1)*100
 summary[case]['renderDeltaPercent']=(summary[case]['CyAudioCore']['renderUs']['median']/summary[case]['JUCE']['renderUs']['median']-1)*100
write_json(out/'summary.json',dict(performanceAccepted=all(v['noiseAccepted'] for r in rows for v in r['cases'].values()),summary=summary,sourceRevision=m['sourceRevision'],executables=m['variants'],allInputsStable=stable(),protocol=dict(rounds=a.rounds,warmupSeconds=2,measureSeconds=a.seconds,mode='real ASIO Fireface USB, 48000 Hz, 1024 frames, zero input, two silent outputs',ui='drawing suppressed in both QA copies',cpu='100%=one logical core',topologies='8 gain VST3 plugins stopped/playing, then four DJ decks after complete worker shutdown; previous/JUCE 8 worker processes, current 16 (outer IPC + mandatory SDK isolation)',scope='one full-render root timer, no algorithm changes; mean per block, not p95 or physical latency',knownIssues='Cinema DC and CUE ASIO remain open; not tested or masked by this campaign')))
print(json.dumps(summary,indent=2),flush=True)
