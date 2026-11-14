"""Complete DSP campaign: instrument families and audio/MIDI mixtures: real ASIO render, drawing suppressed.
100% CPU is one logical core. QA probe counts only the full app render boundary.
"""
import argparse,hashlib,json,statistics,time
from pathlib import Path
from cyaudio_cpu_diagnostic import CpuSession,scope_deltas,descendants,image
from cyaudio_comparison import ROOT,assets,write_json,process_sample
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
 order=list(m['variants']);order=order[iteration%len(order):]+order[:iteration%len(order)]
 for label in order:
  folder=out/'runs'/f'{iteration+1}-{label}'
  s=CpuSession(Path(m['variants'][label]['exe']),folder,48239,'asio',True,'off')
  r=dict(label=label,round=iteration+1,audio=s.audio,cases={},topologies={});rows.append(r)
  try:
   assert all(not x['enabled'] for x in s.q(cmd='midi_inputs')['devices'])
   groups={
    'analog16':['ambre','argile','alliage','derive'],
    'wavetable16':['colosse','mosaique','nebuleuse','prism'],
    'digital16':['opale','onyx','quartz','saphir'],
    'physical16':['tirette','bronze','jade','silex'],
   }
   cases=list(groups)+['mixed16','variedEffects16','audio32','compressor16','proEffects16']
   for case in cases:
    s.reset();s.q(cmd='set_tempo',bpm=120)
    for t in s.q(cmd='status')['midiTracks']:s.q(cmd='remove_track',track=t['name'])
    audioCount=8 if case=='mixed16' else 32 if case=='audio32' else 16 if case.endswith('16') and case not in groups else 0
    for index in range(audioCount):
     track=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['index']
     effects=['compressor'] if case=='compressor16' else ['comp-pro','chorus','reverb-pro'] if case=='proEffects16' else [['givre'],['empreinte'],['velours'],['amp-sim'],['echo-bande'],['chorus'],['tranche'],['multiband-pro']][index%8] if case=='variedEffects16' else ['compressor','chorus'] if case=='mixed16' else []
     for key in effects:s.q(cmd='insert_device',track=track,key=key)
    models=groups.get(case,['ambre','argile','colosse','mosaique','opale','saphir','jade','tirette'] if case=='mixed16' else [])
    for index,model in enumerate(models):
     name=f'{model}-{index}';s.q(cmd='add_midi_track',name=name,instrument=model+'-instrument')
     s.q(cmd='add_midi_clip',track=name,name='Pattern',startBeats=0,lengthBeats=40)
     notes=[dict(note=(42 if model=='silex' else 48)+j*3,start=0,length=38,velocity=.45) for j in range(4)]
     if model in ['jade','bronze','silex']:notes=[dict(note=(42 if model=='silex' else 48)+j*3,start=beat,length=1.5,velocity=.45) for beat in range(0,40,2) for j in range(4)]
     s.q(cmd='add_notes',track=name,clip='Pattern',notes=notes)
     if case=='mixed16':
      for key in ['compressor','chorus']:s.q(cmd='insert_device',track=name,key=key)
    if case=='mixed16':
     s.q(cmd='add_bus_track',name='Mixed bus')
     s.q(cmd='insert_device',track='Mixed bus',key='reverb-pro')
     for index in range(4):s.q(cmd='routing',track=index,outputs=[dict(destination='Mixed bus',gainDb=-6)])
     for index,model in enumerate(models):s.q(cmd='routing',track=f'{model}-{index}',outputs=[dict(destination='Mixed bus',gainDb=-6)])
    topology=s.q(cmd='status');assert len(topology['audioTracks'])==audioCount+(1 if case=='mixed16' else 0) and len(topology['midiTracks'])==len(models),(case,topology)
    assert all(t['clips'][0]['notes']>=4 for t in topology['midiTracks'])
    assert all(not t['plugins'] for t in topology['audioTracks'])
    r['topologies'][case]=dict(audio=topology['audioTracks'],midi=topology['midiTracks'],requestedModels=models)
    s.q(cmd='transport',action='play',position=.1);v=s.measure(a.seconds);r['cases'][case]=v
    memory=process_sample(s.process.pid);v['privateMiB']=sum(p['private'] for p in memory.values())/1048576;v['workingSetMiB']=sum(p['workingSet'] for p in memory.values())/1048576
    assert v['positionDelta']>a.seconds*.7,(case,v['positionDelta'])
    assert max(t['peakDb'] for t in v['lastStatus']['meters'])>-80,(case,v['lastStatus']['meters'])
    assert len(v['processes'])==1
    s.q(cmd='transport',action='stop')

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
  summary[case][label]={metric:dict(median=statistics.median(v[metric] for v in samples),min=min(v[metric] for v in samples),max=max(v[metric] for v in samples),values=[v[metric] for v in samples]) for metric in ['renderUs','renderCallsPerSecond','parentCpu','workerCpu','audioMillionCyclesPerBlock','systemOtherCorePercent','privateMiB','workingSetMiB']}
 summary[case]['renderDeltaPercent']=(summary[case]['CyAudioCore']['renderUs']['median']/summary[case]['JUCE']['renderUs']['median']-1)*100
 if 'Before' in m['variants']:summary[case]['migrationDeltaPercent']=(summary[case]['CyAudioCore']['renderUs']['median']/summary[case]['Before']['renderUs']['median']-1)*100
write_json(out/'summary.json',dict(summary=summary,sourceRevision=m['sourceRevision'],executables=m['variants'],allInputsStable=stable(),protocol=dict(rounds=a.rounds,warmupSeconds=2,measureSeconds=a.seconds,mode='real ASIO Fireface USB, 48000 Hz, 1024 frames, zero input, two silent outputs',ui='drawing suppressed in both QA copies',cpu='100%=one logical core',topologies='16 instruments in four groups, 8 audio + 8 MIDI with effects, 16 diverse FX, 32 dry tracks and 16 compressor/Pro FX tracks; four scheduled notes per instrument, retriggered physical sounds; Silex may choke voices',scope='one full-render root timer, no algorithm changes; mean per block, not p95 or physical latency',knownIssues='Cinema DC and CUE ASIO remain open; not tested or masked by this campaign')))
print(json.dumps(summary,indent=2),flush=True)
