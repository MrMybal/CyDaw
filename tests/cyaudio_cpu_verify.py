"""Record initial provenance or verify finished local CPU campaigns. No SDK mutations."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);p.add_argument('--initial',action='store_true');a=p.parse_args();out=a.output.resolve()
assert out.is_relative_to(ROOT/'output')
out.mkdir(parents=True,exist_ok=True)
read=lambda f:json.loads(f.read_text(encoding='utf-8-sig'))
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
def git(path,*args):return subprocess.check_output(['git','-c','safe.directory='+path.as_posix(),'-C',str(path),*args],text=True,encoding='utf-8').strip()
def state(path):
 h=hashlib.sha256();count=0
 for name in sorted(filter(None,git(path,'ls-files','-z').split('\0'))):
  f=path/name
  if f.is_file():h.update(name.encode());h.update(b'\0');h.update(f.read_bytes());count+=1
 return dict(head=git(path,'rev-parse','HEAD'),status=git(path,'status','--porcelain'),trackedHash=h.hexdigest(),files=count)
if a.initial:
 file=out/'initial-state.json'
 if file.exists():raise SystemExit('Initial snapshot exists; use a fresh campaign directory')
 data={path.name:state(path) for path in [ROOT,ROOT.parent/'CyAudioCore',ROOT.parent/'CyDSP',ROOT.parent/'CyImgui_Interface']}
 file.write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8');print('Initial source snapshot saved');raise SystemExit(0)
from cyaudio_cpu_diagnostic import source_fingerprint
m=read(out/'build-manifest.json');current=source_fingerprint(out)
series={f.stem:read(f) for f in out.glob('*-results.json') if not f.name.startswith('preflight')}
for name,rows in series.items():
 for row in rows:
  assert not row['shutdown']['forced'],(name,row['label'])
  assert not row['audio']['inputChannels'] and len(row['audio']['outputChannels'])==2
  if 'sourceStability' in row:assert row['sourceStability']['stable'] and row['sourceStability']['after']==current,name
variants={label:dict(sha256=sha(Path(v['exe'])),matchesBuild=sha(Path(v['exe']))==v['sha256']) for label,v in m['variants'].items()}
assert all(v['matchesBuild'] for v in variants.values())
for f,expected in m['reusedInputs'].items():assert sha(Path(f))==expected,f
production=git(ROOT,'diff','--name-only','HEAD','--','src','include','CMakeLists.txt','cmake');assert not production,production
live={name:dict(head=git(ROOT.parent/name,'rev-parse','HEAD'),status=git(ROOT.parent/name,'status','--porcelain')) for name in ['CyAudioCore','CyDSP','CyImgui_Interface']}
data=dict(sourceFingerprint=current,head=git(ROOT,'rev-parse','HEAD'),productionSourceDiff=production,variants=variants,microExeSha256=sha(out/'micro/buffers.exe'),frozenArchives={n:sha(out/(n+'.zip')) for n in ['CyAudioCore','CyDSP']},liveDependenciesAfter=live,runtimeHashes={label:{f.name:sha(f) for f in Path(x['exe']).parent.iterdir() if f.is_file() and f.suffix.lower() in ['.dll','.exe']} for label,x in m['variants'].items()},runCounts={name:len(rows) for name,rows in series.items()},forceTerminatedSessions=0)
(out/'final-verification.json').write_text(json.dumps(data,indent=2,ensure_ascii=False)+'\n',encoding='utf-8');print('Final source/binary verification passed',data['runCounts'])
