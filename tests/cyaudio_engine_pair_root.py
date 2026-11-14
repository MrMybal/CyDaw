"""Add matched full-render root probes to immutable engine QA copies."""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--base',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();base=a.base.resolve();out=a.output.resolve();assert out.is_relative_to(ROOT/'output') and not out.exists();out.mkdir(parents=True)
m=json.loads((base/'build-manifest.json').read_text(encoding='utf-8'));m['rootProbeBase']=str(base);shutil.copytree(base/'probes',out/'probes');sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat');env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
compiler=Path(m['compiler']);linker=compiler.with_name('link.exe')
def run(cmd,cwd,log):
 m['commands'].append(dict(command=[str(v) for v in cmd],cwd=str(cwd),log=str(log)))
 with log.open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=cwd,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:print(log.read_text(encoding='utf-8',errors='replace')[-5000:]);raise RuntimeError(str(log))
for label in ['JUCE','native']:
 source=ROOT if label=='native' else ROOT/'output/juce-core-comparison/juce-source';build=ROOT/('build/cyaudio-native-desktop' if label=='native' else 'build/juce-comparison');folder=out/label;folder.mkdir();shutil.copytree(base/label/'sources',folder/'sources')
 file='src/app/VirtualAudioBus.cpp';original=source/file;text=original.read_text(encoding='utf-8-sig');match=re.search(r'void VirtualBusRouter::getNextAudioBlock\([^;{}]*\)\s*\{',text);assert match
 n=len(m['sites']);m['sites'].append(dict(id=n,name='VirtualBusRouter::getNextAudioBlock',file=file,line=text[:match.start()].count('\n')+1));text=text[:match.end()]+' CPU_QA_SCOPE('+str(n)+'); '+text[match.end():]
 generated=folder/'sources/VirtualAudioBus.cpp';generated.write_text('#include "'+(out/'probes/CpuScopes.h').as_posix()+'"\n'+text,encoding='utf-8')
 recipes={}
 for log in build.glob('*.dir/Release/*.tlog/CL.command.1.tlog'):
  rows=log.read_text(encoding='utf-16').splitlines()
  for i,row in enumerate(rows[:-1]):
   if row.startswith('^') and '|' not in row:recipes[Path(row[1:]).name.lower()]=rows[i+1]
 flags=recipes[original.name.lower()]
 if label=='native':
  for name in ['CyAudioCore','CyDSP']:
   for form in [str(ROOT.parent/name),str(ROOT/'..'/name)]:flags=re.sub(re.escape(form),lambda v:str(ROOT/'output/cyaudio-cpu-diagnostic/2026-11-13/frozen'/name),flags,flags=re.I)
 flags=re.sub(r'/Yu"[^"]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^"]*"|/Fo"[^"]*"|/Fd"[^"]*"','',flags,flags=re.I);flags=re.sub(re.escape(str(original)),lambda v:'"'+str(generated)+'"',flags,flags=re.I)
 obj=folder/'VirtualAudioBus.obj';flags+=' /I"'+str(original.parent)+'" /Fo"'+str(obj)+'" /Fd"'+str(folder/'root.pdb')+'" /Zi /DCYDAW_QA_CPU_SCOPES=1';rsp=folder/'compile.rsp';rsp.write_text(flags,encoding='utf-8');run([str(compiler),'@'+str(rsp)],build,folder/'compile.log')
 link=(base/label/'link.rsp').read_text(encoding='utf-8');found=False
 for f in m['inputs']:
  if Path(f).name.lower()=='virtualaudiobus.obj':
   cached=base/label/'inputs'/(hashlib.sha256(f.lower().encode()).hexdigest()[:12]+'-'+Path(f).name);token='"'+str(cached)+'"'
   if token in link:link=link.replace(token,'"'+str(obj)+'"');found=True
 if not found:link='"'+str(obj)+'" '+link
 for option in ['OUT','PDB','IMPLIB']:link=re.sub('/'+option+r':"[^"]*"','',link,flags=re.I)
 exe=folder/'CyDAW 2.exe';link+=' /OUT:"'+str(exe)+'" /PDB:"'+str(folder/'app.pdb')+'"';rsp=folder/'link.rsp';rsp.write_text(link,encoding='utf-8');run([str(linker),'@'+str(rsp)],build,folder/'link.log')
 for f in (base/label).iterdir():
  if f.is_file() and (f.suffix.lower()=='.dll' or f.name=='CyDAWPluginWorker.exe'):shutil.copy2(f,folder/f.name)
 m['variants'][label]['exe']=str(exe);m['variants'][label]['sha256']=sha(exe);m['variants'][label]['sourceHashes'][file]=sha(original);m['variants'][label]['generatedHashes']['VirtualAudioBus.cpp']=sha(generated)
 print(label,'root probe ready',flush=True)
(out/'build-manifest.json').write_text(json.dumps(m,indent=2),encoding='utf-8')
