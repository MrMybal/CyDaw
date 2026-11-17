"""Build minimal matched QA copies: one render-root probe, optional no-frame gate.
Does not edit SDKs or production sources. Requires the frozen JUCE reference.
"""
import argparse,hashlib,json,os,re,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
p.add_argument('--current-build',type=Path,default=ROOT/'build/cyui-audioviews')
a=p.parse_args();out=a.output.resolve();assert out.is_relative_to(ROOT/'output') and not out.exists();out.mkdir(parents=True)
vc=Path('C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
compiler=vc.parents[2]/'Tools/MSVC/14.40.33807/bin/Hostx64/x64/cl.exe';linker=compiler.with_name('link.exe')
env={k.upper():v for k,v in os.environ.items()}
for row in subprocess.check_output('cmd /d /s /c ""'+str(vc)+'" >nul && set"',text=True,encoding='utf-8',errors='replace').splitlines():
 if '=' in row:k,v=row.split('=',1);env[k.upper()]=v
probes=out/'probes';probes.mkdir()
for name in ['CpuScopes.h','CpuScopes.cpp']:shutil.copy2(ROOT/'tests/fixtures/cpu-diagnostic'/name,probes/name)
m=dict(compiler=str(compiler),sites=[dict(id=0,name='ImGuiHost::frame'),dict(id=1,name='VirtualBusRouter::getNextAudioBlock')],variants={},inputs={},commands=[],sourceRevision=subprocess.check_output(['git','-c','safe.directory='+ROOT.as_posix(),'rev-parse','HEAD'],cwd=ROOT,text=True).strip())
def run(cmd,build,log):
 m['commands'].append(dict(command=[str(x) for x in cmd],cwd=str(build),log=str(log)))
 with log.open('w',encoding='utf-8') as f:r=subprocess.run(cmd,cwd=build,env=env,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode:raise RuntimeError(log.read_text(encoding='utf-8',errors='replace')[-5000:])
def tokens(s):return re.findall(r'"[^"\n]*"|[^\s]+',s)
for label,source,build,base in [
 ('JUCE',ROOT/'output/juce-core-comparison/juce-source',ROOT/'build/juce-comparison',ROOT/'output/cyaudio-engine-pair/2026-11-14/JUCE/control'),
 ('CyAudioCore',ROOT,a.current_build.resolve(),None)]:
 folder=out/label;folder.mkdir();generated=folder/'sources';generated.mkdir();recipes={};hashes={}
 for log in build.glob('*.dir/Release/*.tlog/CL.command.1.tlog'):
  rows=log.read_text(encoding='utf-16').splitlines()
  for i,row in enumerate(rows[:-1]):
   if row.startswith('^') and '|' not in row:recipes[Path(row[1:]).name.lower()]=(row[1:],rows[i+1])
 if base:link=(base/'link.rsp').read_text(encoding='utf-8')
 else:link=' '.join((build/'CyDAW2.dir/Release/CyDAW2.tlog/link.command.1.tlog').read_text(encoding='utf-16').splitlines()[1:])
 # All existing object/library inputs must remain byte-identical throughout the campaign.
 for token in tokens(link):
  value=token.strip('"');f=Path(value)
  if not value.lower().endswith(('.obj','.lib','.res')) or value.startswith('/'):continue
  if not f.is_absolute():f=build/f
  if f.is_file():m['inputs'][str(f.resolve())]=sha(f)
 replacements={}
 for file in ['src/app/VirtualAudioBus.cpp']+(['src/v2/ImGuiHost.cpp'] if not base else []):
  original=source/file;text=original.read_text(encoding='utf-8-sig');hashes[str(original)]=sha(original)
  if file.endswith('VirtualAudioBus.cpp'):
   match=re.search(r'void VirtualBusRouter::getNextAudioBlock\([^;{}]*\)\s*\{',text);assert match
   text=text[:match.end()]+' CPU_QA_SCOPE(1); '+text[match.end():]
  else:
   marker='void ImGuiHost::frame()\n{';assert marker in text
   text=text.replace(marker,marker+'\n    if (std::getenv("CYDAW_QA_NO_FRAME")) return;\n    CPU_QA_SCOPE(0);',1)
  target=generated/original.name;target.write_text('#include <cstdlib>\n#include "'+(probes/'CpuScopes.h').as_posix()+'"\n'+text,encoding='utf-8')
  originalToken,flags=recipes[original.name.lower()]
  flags=re.sub(r'/Yu"[^"]*"','/Y-',flags,flags=re.I);flags=re.sub(r'/Fp"[^"]*"|/Fo"[^"]*"|/Fd"[^"]*"','',flags,flags=re.I)
  flags=re.sub(re.escape(originalToken),lambda _:str(target),flags,flags=re.I)
  obj=folder/(original.stem+'.obj');flags+=' /I"'+str(original.parent)+'" /Fo"'+str(obj)+'" /Fd"'+str(obj.with_suffix('.pdb'))+'" /DCYDAW_QA_CPU_SCOPES=1'
  rsp=obj.with_suffix('.rsp');rsp.write_text(flags,encoding='utf-8');run([str(compiler),'@'+str(rsp)],build,obj.with_suffix('.log'))
  replacements[original.stem.lower()]=obj
 observer=folder/'CpuScopes.obj';run([str(compiler),'/c','/nologo','/O2','/MD','/std:c++20','/EHsc','/DCYDAW_QA_CPU_SCOPES=1',str(probes/'CpuScopes.cpp'),'/Fo'+str(observer)],build,folder/'observer.log');replacements['cpuscopes']=observer
 found=set()
 for token in tokens(link):
  stem=Path(token.strip('"')).stem.lower()
  for name,obj in replacements.items():
   if stem==name or stem.endswith('-'+name):link=link.replace(token,'"'+str(obj)+'"');found.add(name)
 for name,obj in replacements.items():
  if name not in found:link='"'+str(obj)+'" '+link
 for option in ['OUT','PDB','IMPLIB']:link=re.sub('/'+option+r':"[^"]*"','',link,flags=re.I)
 exe=folder/'CyDAW 2.exe';link+=' /OUT:"'+str(exe)+'" /PDB:"'+str(folder/'app.pdb')+'"';rsp=folder/'link.rsp';rsp.write_text(link,encoding='utf-8');run([str(linker),'@'+str(rsp)],build,folder/'link.log')
 runtime=base if base else build/'CyDAW2_artefacts/Release'
 for f in runtime.iterdir():
  if f.is_file() and (f.suffix.lower()=='.dll' or f.name=='CyDAWPluginWorker.exe'):shutil.copy2(f,folder/f.name)
 m['variants'][label]=dict(exe=str(exe),sha256=sha(exe),sourceHashes=hashes,generatedHashes={str(f):sha(f) for f in generated.glob('*.cpp')})
 print(label,'ready',flush=True)
assert all(sha(Path(f))==h for f,h in m['inputs'].items())
(out/'build-manifest.json').write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8')
