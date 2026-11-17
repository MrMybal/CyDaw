"""Build tiny scheduling probes with frozen JUCE objects and current adapter.
No application is launched and no audio device is opened. Builds stay in output.
Run AFTER the callback capture campaign, not concurrently with it.
"""
import argparse,csv,hashlib,io,json,os,re,subprocess
import numpy as np
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--kind',choices=['voices','effects'],default='voices');a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output') and not out.exists();out.mkdir(parents=True)
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat');compiler=vc.parents[2]/'Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe';linker=compiler.with_name('link.exe')
env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
source=ROOT/('tests/fixtures/project-audio/VoiceTimingProbe.cpp' if a.kind=='voices' else 'tests/fixtures/project-audio/EffectAudioProbe.cpp');result=dict(source=dict(path=str(source),sha256=sha(source)),variants=[],commands=[])

def run(cmd,cwd,log):
 result['commands'].append(dict(command=[str(x) for x in cmd],cwd=str(cwd),log=str(log)))
 with log.open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError(log.read_text(encoding='utf-8',errors='replace')[-8000:])

def tokens(s):return re.findall(r'(?:[^\s"]|"[^"]*")+',s)

variants=[('JUCE',ROOT/'build/juce-comparison','CyDAW2','juce_audio_basics.cpp'),('CyAudioCore',ROOT/'build/cyaudio-032','cydaw_audio_source_tests','audiosourcetests.cpp')] if a.kind=='voices' else [('JUCE',ROOT/'build/juce-comparison','cydaw_core','builtinprocessors.cpp'),('CyAudioCore',ROOT/'build/cyaudio-032','cydaw_full_dsp_reuse_tests','fulldspreusetests.cpp')]
for label,build,target,oldName in variants:
 folder=out/label;folder.mkdir();recipe=None
 for log in (build/(target+'.dir')/'Release').glob('*.tlog/CL.command.1.tlog'):
  rows=log.read_text(encoding='utf-16').splitlines()
  for i,row in enumerate(rows[:-1]):
   if row.startswith('^') and '|' not in row and row.lower().endswith(oldName.lower()):recipe=(row[1:],rows[i+1])
 assert recipe,label
 original,flags=recipe;flags=re.sub(r'/Yu"[^" ]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^" ]*"|/Fo"[^"]*"|/Fd"[^"]*"','',flags,flags=re.I);flags=re.sub(re.escape(original),lambda _:str(source),flags,flags=re.I)
 obj=folder/'probe.obj';flags+=' /Fo"'+str(obj)+'" /Fd"'+str(folder/'compile.pdb')+'"'
 if label=='JUCE':flags+=' /DCYDAW_PROBE_JUCE=1'
 rsp=folder/'compile.rsp';rsp.write_text(flags,encoding='utf-8');run([compiler,'@'+str(rsp)],build,folder/'compile.log')
 linktarget='CyDAW2' if label=='JUCE' else target
 linklog=next((build/(linktarget+'.dir')/'Release').glob('*.tlog/link.command.1.tlog'));text=' '.join(linklog.read_text(encoding='utf-16').splitlines()[1:]);parts=[];inputs=[]
 for token in tokens(text):
  v=token.strip('"')
  if v.lower().endswith(('.obj','.res')) and not v.startswith('/'):
   if a.kind=='voices' and label=='JUCE' and Path(v).name.lower() in ['juce_audio_basics.obj','juce_core.obj','juce_core_compilationtime.obj']:
    parts.append(token);f=Path(v);f=f if f.is_absolute() else build/f;inputs.append(dict(path=str(f),sha256=sha(f)))
   continue
  if re.match(r'/(OUT|PDB|IMPLIB|SUBSYSTEM|ENTRY):',v,re.I):continue
  parts.append(token)
  if v.lower().endswith('.lib') and not v.startswith('/'):
   f=Path(v);f=f if f.is_absolute() else build/f
   if f.is_file():inputs.append(dict(path=str(f),sha256=sha(f)))
 # Keep the same static library and system library arguments, without application objects.
 exe=folder/'probe.exe';parts += ['"'+str(obj)+'"','/SUBSYSTEM:CONSOLE','/ENTRY:mainCRTStartup','/OUT:"'+str(exe)+'"','/PDB:"'+str(folder/'probe.pdb')+'"']
 rsp=folder/'link.rsp';rsp.write_text(' '.join(parts),encoding='utf-8');run([linker,'@'+str(rsp)],build,folder/'link.log')
 raw=subprocess.check_output([str(exe)]+(['--dump',str(folder/'audio.f32')] if a.kind=='effects' else []),text=True);(folder/'events.jsonl').write_text(raw,encoding='utf-8');row=dict(label=label,exe=str(exe),sha256=sha(exe),frozenObjects=inputs,events=[json.loads(line) for line in raw.splitlines()] if a.kind=='voices' else list(csv.DictReader(io.StringIO(raw))),objectInputsStable=all(sha(Path(f['path']))==f['sha256'] for f in inputs));result['variants'].append(row);print(label,raw if a.kind=='voices' else str(len(row['events']))+' processor captures',flush=True)
if a.kind=='effects':
 result['comparisons']=[];old,new=result['variants'];x=np.fromfile(Path(old['exe']).parent/'audio.f32',dtype='<f4');y=np.fromfile(Path(new['exe']).parent/'audio.f32',dtype='<f4');assert len(x)==len(y)
 for left,right in zip(old['events'],new['events']):
  assert left['name']==right['name'] and left['offset']==right['offset'] and left['samples']==right['samples'];i,n=int(left['offset']),int(left['samples']);u=x[i:i+n].astype(float);v=y[i:i+n].astype(float);d=u-v;rms=float(np.sqrt(np.mean(d*d)));result['comparisons'].append(dict(name=left['name'],samples=n,finite=bool(np.isfinite(v).all()),maxAbs=float(abs(d).max()),errorRmsDbfs=float(20*np.log10(max(rms,1e-30))),equalPcm=bool(np.array_equal(u,v))))
 print(json.dumps(result['comparisons']),flush=True)
(out/('timing-probe.json' if a.kind=='voices' else 'effects-probe.json')).write_text(json.dumps(result,indent=2),encoding='utf-8')
