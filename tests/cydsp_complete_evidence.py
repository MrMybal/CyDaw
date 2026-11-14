"""Numerical evidence and warmed fresh-process processor timings, no audio device.
Run after all builds and other benchmarks have stopped. Raw CSV and PCM stay local.
"""
import argparse,csv,hashlib,io,json,statistics,subprocess
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--before',type=Path,required=True);p.add_argument('--after',type=Path,required=True);p.add_argument('--rounds',type=int,default=5);a=p.parse_args();assert a.rounds>=5
out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);executables={'Before':a.before.resolve(),'After':a.after.resolve()}
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
hashes={str(f):sha(f) for f in executables.values()};captures={}
for label,exe in executables.items():
 pcm=out/(label+'.f32');s=subprocess.check_output([str(exe),'--dump',str(pcm)],text=True);(out/(label+'.csv')).write_text(s,encoding='utf-8');captures[label]=(list(csv.DictReader(io.StringIO(s))),np.fromfile(pcm,dtype='<f4'))
rows=[];old,x=captures['Before'];new,y=captures['After'];assert len(x)==len(y) and len(old)==len(new)
for left,right in zip(old,new):
 assert left['name']==right['name'] and left['offset']==right['offset'] and left['samples']==right['samples'];i,n=int(left['offset']),int(left['samples']);d=y[i:i+n].astype(float)-x[i:i+n].astype(float);rms=float(np.sqrt(np.mean(d*d)))
 assert np.all(np.isfinite(y[i:i+n])) and int(right['callbackAllocations'])==int(left['callbackAllocations'])==0
 rows.append(dict(name=left['name'],samples=n,maxAbsolute=float(np.max(abs(d))),rms=rms,errorDbfs=20*np.log10(max(rms,1e-30)),callbackAllocations=0,beforePeak=float(left['peak']),afterPeak=float(right['peak'])))
(out/'pcm.json').write_text(json.dumps(dict(cases=rows,totalSamples=len(x),executableHashes=hashes),indent=2),encoding='utf-8')
runs=[]
for iteration in range(a.rounds):
 for label in (['Before','After'] if iteration%2==0 else ['After','Before']):
  text=subprocess.check_output([str(executables[label])],text=True);(out/f'{iteration+1}-{label}.csv').write_text(text,encoding='utf-8');cases=list(csv.DictReader(io.StringIO(text)));assert all(int(r['callbackAllocations'])==0 for r in cases);runs.append(dict(round=iteration+1,label=label,cases=cases))
summary=[]
for name in [r['name'] for r in old]:
 entry=dict(name=name)
 for label in executables:
  values=[float(next(r for r in run['cases'] if r['name']==name)['usPer1024']) for run in runs if run['label']==label];entry[label]=dict(median=statistics.median(values),min=min(values),max=max(values),values=values)
 entry['deltaPercent']=(entry['After']['median']/entry['Before']['median']-1)*100;summary.append(entry)
assert all(sha(Path(f))==h for f,h in hashes.items())
(out/'benchmark.json').write_text(json.dumps(dict(cases=summary,runs=runs,protocol=dict(rounds=a.rounds,freshProcesses=True,alternating=True,warmup='28 effect blocks or 16 instrument blocks excluded; parameter changes and release retained',unit='elapsed microseconds / 1024 frames, single voice for instruments',timing='mean of warmed variable blocks per case, then median of five fresh processes',backend='offline, no hardware or UI'),executableHashes=hashes),indent=2),encoding='utf-8')
print('completed',len(rows),'cases',len(x),'samples',flush=True)
