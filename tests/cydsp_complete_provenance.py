"""Freeze application/dependency identity and add the previous Core QA control."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--manifest',type=Path,required=True);p.add_argument('--previous',type=Path,required=True);p.add_argument('--current-build',type=Path,default=ROOT/'build/cydsp-complete');p.add_argument('--sdk-manifest',type=Path);a=p.parse_args();m=json.loads(a.manifest.read_text(encoding='utf-8'));old=json.loads(a.previous.read_text(encoding='utf-8'));assert 'Before' not in m['variants']
m['variants']['Before']=old['variants']['CyAudioCore']
m['baselineProvenance']=dict(manifest=str(a.previous.resolve()),manifestSha256=hashlib.sha256(a.previous.read_bytes()).hexdigest(),applicationState=old.get('applicationState'),dependencySnapshots=old.get('dependencySnapshots'))
for f,h in old['inputs'].items():
 # Prior application sources may change; its binary/objects and frozen SDK stay immutable.
 if any(Path(f).is_relative_to(ROOT/d) for d in ['src','include']):continue
 if f in m['inputs']:assert m['inputs'][f]==h
 m['inputs'][f]=h
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
# Include all distributed C++ sources/headers, not just the two instrumented TUs.
sourceHashes={str(f):sha(f) for d in ['src','include'] for f in (ROOT/d).rglob('*') if f.is_file() and f.suffix in ['.cpp','.h','.inc','.hpp']}
m['inputs'].update(sourceHashes)
git=lambda *args:subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),*args],cwd=ROOT,text=True,encoding='utf-8')
m['applicationState']=dict(head=git('rev-parse','HEAD').strip(),branch=git('branch','--show-current').strip(),status=git('status','--short'),diffSha256=hashlib.sha256(git('diff','--binary').encode()).hexdigest(),sourceFiles=len(sourceHashes))
m['dependencySnapshots']={}
cache={}
for line in (a.current_build/'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
 if '=' in line and ':' in line.split('=',1)[0]:key,value=line.split('=',1);cache[key.split(':',1)[0]]=value
for name in ['CyAudioCore','CyDSP','CyUI']:
 folder=Path(cache[name+'_SOURCE_DIR'])
 hashes={str(f):sha(f) for d in ['include','src'] for f in (folder/d).rglob('*') if f.is_file()}
 m['inputs'].update(hashes);m['dependencySnapshots'][name]=dict(folder=str(folder),files=len(hashes))
for variant in m['variants'].values():
 variant['runtimeHashes']={str(f):sha(f) for f in Path(variant['exe']).parent.iterdir() if f.is_file() and f.suffix.lower() in ['.exe','.dll']}
 m['inputs'].update(variant['runtimeHashes'])
if a.sdk_manifest:
 m['sdkRevisions']=json.loads(a.sdk_manifest.read_text(encoding='utf-8'));m['inputs'][str(a.sdk_manifest.resolve())]=sha(a.sdk_manifest)

a.manifest.write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8');print('recorded',len(m['inputs']),'immutable inputs')
