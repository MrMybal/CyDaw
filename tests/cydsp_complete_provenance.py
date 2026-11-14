"""Freeze application/dependency identity and add the previous Core QA control."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--previous',type=Path,required=True);a=p.parse_args();m=json.loads(a.manifest.read_text(encoding='utf-8'));old=json.loads(a.previous.read_text(encoding='utf-8'));assert 'Before' not in m['variants']
m['variants']['Before']=old['variants']['CyAudioCore']
for f,h in old['inputs'].items():
 if f in m['inputs']:assert m['inputs'][f]==h
 m['inputs'][f]=h
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
# Include all distributed C++ sources/headers, not just the two instrumented TUs.
sourceHashes={str(f):sha(f) for d in ['src','include'] for f in (ROOT/d).rglob('*') if f.is_file() and f.suffix in ['.cpp','.h','.inc','.hpp']}
m['inputs'].update(sourceHashes)
git=lambda *args:subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),*args],cwd=ROOT,text=True,encoding='utf-8')
m['applicationState']=dict(head=git('rev-parse','HEAD').strip(),branch=git('branch','--show-current').strip(),status=git('status','--short'),diffSha256=hashlib.sha256(git('diff','--binary').encode()).hexdigest(),sourceFiles=len(sourceHashes))
m['dependencySnapshots']={}
for name,folder in [('CyAudioCore',ROOT/'output/cydsp-numerical/2026-11-14/sdk/CyAudioCore'),('CyDSP',ROOT/'output/cydsp-numerical/2026-11-14/sdk/CyDSP'),('CyUI',ROOT/'output/cyui-audioviews/2026-11-14/CyUI')]:
 hashes={str(f):sha(f) for d in ['include','src'] for f in (folder/d).rglob('*') if f.is_file()}
 m['inputs'].update(hashes);m['dependencySnapshots'][name]=dict(folder=str(folder),files=len(hashes))
a.manifest.write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8');print('recorded',len(m['inputs']),'immutable inputs')
