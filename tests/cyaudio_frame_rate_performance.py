"""Paired uninstrumented pre/post FPS fix. No source or SDK rebuilds here."""
import argparse,hashlib,json,statistics,time
from pathlib import Path
from cyaudio_cpu_diagnostic import CpuSession
from cyaudio_comparison import ROOT,assets,write_json
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--fixed',type=Path,required=True);p.add_argument('--rounds',type=int,default=3);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
variants={'before':ROOT/'build/cyaudio-native-desktop/CyDAW2_artefacts/Release/CyDAW 2.exe','fixed60':a.fixed.resolve()};asset=assets(out/'assets');rows=[]
hashes={k:hashlib.sha256(f.read_bytes()).hexdigest() for k,f in variants.items()}
for iteration in range(a.rounds):
 order=list(variants)
 if iteration%2:order.reverse()
 for label in order:
  s=CpuSession(variants[label],out/f'{iteration+1}-{label}',48229,'asio');row=dict(label=label,round=iteration+1,audio=s.audio,cases={});rows.append(row)
  try:
   for case,n in [('audio32',32),('effects16',16)]:
    s.reset()
    for _ in range(n):
     index=s.q(cmd='add_audio_track',file=str(asset/'long.wav'))['index']
     if case=='effects16':
      for key in ['comp-pro','chorus','reverb-pro']:s.q(cmd='insert_device',track=index,key=key)
    status=s.q(cmd='status');assert len(status['audioTracks'])==n
    if case=='effects16':assert all(set(t['devices'])=={'comp-pro','chorus','reverb-pro'} for t in status['audioTracks'])
    s.q(cmd='set_view',mode=1);time.sleep(.4);s.q(cmd='set_view',mode=3)
    s.q(cmd='transport',action='play',position=.1);row['cases'][case]=s.measure(5);assert row['cases'][case]['positionDelta']>4;s.q(cmd='transport',action='stop')
  finally:row['shutdown']=s.close();write_json(out/'raw.json',dict(hashes=hashes,runs=rows));assert not row['shutdown']['forced']
  print(iteration+1,label,{k:round(v['parentCpu'],2) for k,v in row['cases'].items()},flush=True)
assert all(hashlib.sha256(f.read_bytes()).hexdigest()==hashes[k] for k,f in variants.items())
summary={}
for label in variants:
 summary[label]={}
 for case in ['audio32','effects16']:
  selected=[r['cases'][case] for r in rows if r['label']==label];cpu=[v['parentCpu'] for v in selected];cycles=[next(z['cycles'] for z in v['processes'] if z['image']=='CyDAW 2.exe')/v['seconds']/1e9 for v in selected]
  summary[label][case]=dict(parentCpuMedian=statistics.median(cpu),min=min(cpu),max=max(cpu),values=cpu,parentGcyclesPerSecond=statistics.median(cycles),cyclesValues=cycles)
write_json(out/'summary.json',dict(summary=summary,protocol=dict(sampleRate=48000,buffer=1024,inputs=0,outputs=2,silent=True,hidden=True,profile='isolated',rounds=a.rounds,warmup=2,seconds=5,order='alternated fresh processes',settings='before=original monitor pacing; fixed60=default60 preference absent',topology='numeric target indices checked',instrumentation=False),hashes=hashes))
print(json.dumps(summary,indent=2),flush=True)
